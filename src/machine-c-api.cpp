// Copyright 2021 Cartesi Pte. Ltd.
//
// This file is part of the machine-emulator. The machine-emulator is free
// software: you can redistribute it and/or modify it under the terms of the GNU
// Lesser General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// The machine-emulator is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
// for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with the machine-emulator. If not, see http://www.gnu.org/licenses/.
//

#include <alloca.h>
#include <cstring>
#include <string>
#include <exception>
#include <stdexcept>
#include <future>
#include <optional>
#include <regex>
#include <ios>
#include <filesystem>
#include <any>

#include "machine-c-api.h"
#include "riscv-constants.h"
#include "machine-config.h"
#include "machine.h"
#include "i-virtual-machine.h"
#include "virtual-machine.h"
#include "machine-c-api-internal.h"


static char *copy_cstr(const char *str) {
    auto size = strlen(str) + 1;
    auto *copy = new char[size];
    strncpy(copy, str, size);
    return copy;
}

static char *get_error_message_unknown() {
    return copy_cstr("Unknown error");
}

static char *get_error_message(const std::exception &ex) {
    return copy_cstr(ex.what());
}

std::string null_to_empty(const char *s) {
    return std::string{s != nullptr ? s : ""};
}

int cm_result_failure(char **err_msg) try {
    throw;
} catch (std::exception &e) {
    *err_msg = get_error_message(e);
    try {
        throw;
    } catch (std::invalid_argument &ex) {
        return CM_ERROR_INVALID_ARGUMENT;
    } catch (std::domain_error &ex) {
        return CM_ERROR_DOMAIN_ERROR;
    } catch (std::length_error &ex) {
        return CM_ERROR_LENGTH_ERROR;
    } catch (std::out_of_range &ex) {
        return CM_ERROR_OUT_OF_RANGE;
    } catch (std::future_error &ex) {
        return CM_ERROR_FUTURE_ERROR;
    } catch (std::logic_error &ex) {
        return CM_ERROR_LOGIC_ERROR;
    } catch (std::bad_optional_access &ex) {
        return CM_ERROR_BAD_OPTIONAL_ACCESS;
    } catch (std::range_error &ex) {
        return CM_ERROR_RANGE_ERROR;
    } catch (std::overflow_error &ex) {
        return CM_ERROR_OVERFLOW_ERROR;
    } catch (std::underflow_error &ex) {
        return CM_ERROR_UNDERFLOW_ERROR;
    } catch (std::regex_error &ex) {
        return CM_ERROR_REGEX_ERROR;
    } catch (std::ios_base::failure &ex) {
        return CM_ERROR_SYSTEM_IOS_BASE_FAILURE;
    } catch (std::filesystem::filesystem_error &ex) {
        return CM_ERROR_FILESYSTEM_ERROR;
    } catch (std::runtime_error &ex) {
        return CM_ERROR_RUNTIME_ERROR;
    } catch (std::bad_typeid &ex) {
        return CM_ERROR_BAD_TYPEID;
    } catch (std::bad_any_cast &ex) {
        return CM_ERROR_BAD_ANY_CAST;
    } catch (std::bad_cast &ex) {
        return CM_ERROR_BAD_CAST;
    } catch (std::bad_weak_ptr &ex) {
        return CM_ERROR_BAD_WEAK_PTR;
    } catch (std::bad_function_call &ex) {
        return CM_ERROR_BAD_FUNCTION_CALL;
    } catch (std::bad_array_new_length &ex) {
        return CM_ERROR_BAD_ARRAY_NEW_LENGTH;
    } catch (std::bad_alloc &ex) {
        return CM_ERROR_BAD_ALLOC;
    } catch (std::bad_exception &ex) {
        return CM_ERROR_BAD_EXCEPTION;
    } catch (std::exception &e) {
        return CM_ERROR_EXCEPTION;
    }
} catch (...) {
    *err_msg = get_error_message_unknown();
    return CM_ERROR_UNKNOWN;
}

inline int cm_result_success(char **err_msg) {
    *err_msg = nullptr;
    return 0;
}

inline int cm_result_unknown_error(char **err_msg) {
    *err_msg = get_error_message_unknown();
    return CM_ERROR_UNKNOWN;
}



// --------------------------------------------
// String conversion (strdup equivalent with new)
// --------------------------------------------
static char *convert_to_c(const std::string &cpp_str) {
    return copy_cstr(cpp_str.c_str());
}

// --------------------------------------------
// Machine pointer conversion functions
// --------------------------------------------
static cartesi::i_virtual_machine *convert_from_c(cm_machine *m) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<cartesi::i_virtual_machine *>(m);
}

static const cartesi::i_virtual_machine *convert_from_c(const cm_machine *m) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<const cartesi::i_virtual_machine *>(m);
}

// --------------------------------------------
// Processor configuration conversion functions
// --------------------------------------------
static cartesi::processor_config convert_from_c(const cm_processor_config *c_config) {
    if (c_config == nullptr) {
        throw std::invalid_argument("Invalid processor configuration");
    }
    cartesi::processor_config new_cpp_config{};
    //Both C and C++ structs contain only aligned uint64_t values
    //so it is safe to do copy
    static_assert(sizeof(cm_processor_config) == sizeof(new_cpp_config));
    memcpy(&new_cpp_config.x, c_config, sizeof(cm_processor_config));
    return new_cpp_config;
}

static cm_processor_config convert_to_c(const cartesi::processor_config &cpp_config) {
    cm_processor_config new_c_config{};
    static_assert(sizeof(new_c_config) == sizeof(cpp_config));
    memcpy(&new_c_config, &cpp_config.x, sizeof(cm_processor_config));
    return new_c_config;
}


// --------------------------------------------
// Ram configuration conversion functions
// --------------------------------------------
static cartesi::ram_config convert_from_c(const cm_ram_config *c_config) {
    if (c_config == nullptr) {
        throw std::invalid_argument("Invalid ram configuration");
    }
    cartesi::ram_config new_cpp_ram_config{};
    new_cpp_ram_config.length = c_config->length;
    new_cpp_ram_config.image_filename = null_to_empty(c_config->image_filename);
    return new_cpp_ram_config;
}

static cm_ram_config convert_to_c(const cartesi::ram_config &cpp_config) {
    cm_ram_config new_c_ram_config{};
    new_c_ram_config.length = cpp_config.length;
    new_c_ram_config.image_filename = convert_to_c(cpp_config.image_filename);
    return new_c_ram_config;
}

// --------------------------------------------
// Rom configuration conversion functions
// --------------------------------------------

static cartesi::rom_config convert_from_c(const cm_rom_config *c_config) {
    if (c_config == nullptr) {
        throw std::invalid_argument("Invalid rom configuration");
    }
    cartesi::rom_config new_cpp_rom_config{};
    new_cpp_rom_config.bootargs = null_to_empty(c_config->bootargs);
    new_cpp_rom_config.image_filename = null_to_empty(c_config->image_filename);
    return new_cpp_rom_config;
}

static cm_rom_config convert_to_c(const cartesi::rom_config &cpp_config) {
    cm_rom_config new_c_rom_config{};
    new_c_rom_config.bootargs = convert_to_c(cpp_config.bootargs);
    new_c_rom_config.image_filename = convert_to_c(cpp_config.image_filename);
    return new_c_rom_config;
}


// ----------------------------------------------
// Flash drive configuration conversion functions
// ----------------------------------------------
static cartesi::flash_drive_config convert_from_c(const cm_flash_drive_config *c_config) {
    if (c_config == nullptr) {
        throw std::invalid_argument("Invalid flash drive configuration");
    }
    cartesi::flash_drive_config new_cpp_flash_drive_config{
            c_config->start,
            c_config->length,
            c_config->shared,
            c_config->image_filename
    };
    return new_cpp_flash_drive_config;
}

static cm_flash_drive_config convert_to_c(const cartesi::flash_drive_config &cpp_config) {
    cm_flash_drive_config new_c_flash_drive_config{};
    new_c_flash_drive_config.start = cpp_config.start;
    new_c_flash_drive_config.length = cpp_config.length;
    new_c_flash_drive_config.shared = cpp_config.shared;
    new_c_flash_drive_config.image_filename = convert_to_c(cpp_config.image_filename);
    return new_c_flash_drive_config;
}


// ----------------------------------------------
// Clint configuration conversion functions
// ----------------------------------------------
static cartesi::clint_config convert_from_c(const cm_clint_config *c_config) {
    if (c_config == nullptr) {
        throw std::invalid_argument("Invalid clint configuration");
    }
    cartesi::clint_config new_cpp_clint_config{};
    new_cpp_clint_config.mtimecmp = c_config->mtimecmp;
    return new_cpp_clint_config;
}

static cm_clint_config convert_to_c(const cartesi::clint_config &cpp_config) {
    cm_clint_config new_c_clint_config{};
    memset(&new_c_clint_config, 0, sizeof(cm_clint_config));
    new_c_clint_config.mtimecmp = cpp_config.mtimecmp;
    return new_c_clint_config;
}

// ----------------------------------------------
// HTIF configuration conversion functions
// ----------------------------------------------
static cartesi::htif_config convert_from_c(const cm_htif_config *c_config) {
    if (c_config == nullptr) {
        throw std::invalid_argument("Invalid htif configuration");
    }
    cartesi::htif_config new_cpp_htif_config{};
    new_cpp_htif_config.fromhost = c_config->fromhost;
    new_cpp_htif_config.tohost = c_config->tohost;
    new_cpp_htif_config.console_getchar = c_config->console_getchar;
    new_cpp_htif_config.yield_progress = c_config->yield_progress;
    new_cpp_htif_config.yield_rollup = c_config->yield_rollup;

    return new_cpp_htif_config;
}

static cm_htif_config convert_to_c(const cartesi::htif_config &cpp_config) {
    cm_htif_config new_c_htif_config{};
    memset(&new_c_htif_config, 0, sizeof(cm_htif_config));
    new_c_htif_config.fromhost = cpp_config.fromhost;
    new_c_htif_config.tohost = cpp_config.tohost;
    new_c_htif_config.console_getchar = cpp_config.console_getchar;
    new_c_htif_config.yield_progress = cpp_config.yield_progress;
    new_c_htif_config.yield_rollup = cpp_config.yield_rollup;
    return new_c_htif_config;
}


// ----------------------------------------------
// HTIF configuration conversion functions
// ----------------------------------------------
static cartesi::dhd_config convert_from_c(const cm_dhd_config *c_config) {
    if (c_config == nullptr) {
        throw std::invalid_argument("Invalid dhd configuration");
    }
    cartesi::dhd_config new_cpp_dhd_config{};
    new_cpp_dhd_config.tstart = c_config->tstart;
    new_cpp_dhd_config.tlength = c_config->tlength;
    new_cpp_dhd_config.image_filename = null_to_empty(c_config->image_filename);
    new_cpp_dhd_config.dlength = c_config->dlength;
    new_cpp_dhd_config.hlength = c_config->hlength;

    static_assert(sizeof(new_cpp_dhd_config.h) == sizeof(c_config->h));
    memcpy(&new_cpp_dhd_config.h, &c_config->h, sizeof(uint64_t) * CM_MACHINE_DHD_H_REG_COUNT);

    return new_cpp_dhd_config;
}

static cm_dhd_config convert_to_c(const cartesi::dhd_config &cpp_config) {
    cm_dhd_config new_c_dhd_config{};
    new_c_dhd_config.tstart = cpp_config.tstart;
    new_c_dhd_config.tlength = cpp_config.tlength;
    new_c_dhd_config.image_filename = convert_to_c(cpp_config.image_filename);
    new_c_dhd_config.dlength = cpp_config.dlength;
    new_c_dhd_config.hlength = cpp_config.hlength;

    static_assert(sizeof(new_c_dhd_config.h) == sizeof(cpp_config.h));
    memcpy(&new_c_dhd_config.h, &cpp_config.h, sizeof(uint64_t) * CM_MACHINE_DHD_H_REG_COUNT);
    return new_c_dhd_config;
}


// ----------------------------------------------
// Runtime configuration conversion functions
// ----------------------------------------------
cartesi::machine_runtime_config convert_from_c(const cm_machine_runtime_config *c_config) {
    if (c_config == nullptr) {
        throw std::invalid_argument("Invalid machine runtime configuration");
    }
    cartesi::machine_runtime_config new_cpp_machine_runtime_config{
            cartesi::dhd_runtime_config{null_to_empty(c_config->dhd.source_address)},
            cartesi::concurrency_config{c_config->concurrency.update_merkle_tree}
    };

    return new_cpp_machine_runtime_config;
}

// ----------------------------------------------
// Machine configuration conversion functions
// ----------------------------------------------
cartesi::machine_config convert_from_c(const cm_machine_config *c_config) {
    if (c_config == nullptr) {
        throw std::invalid_argument("Invalid machine configuration");
    }

    cartesi::machine_config new_cpp_machine_config{};
    new_cpp_machine_config.processor = convert_from_c(&c_config->processor);
    new_cpp_machine_config.ram = convert_from_c(&c_config->ram);
    new_cpp_machine_config.rom = convert_from_c(&c_config->rom);
    new_cpp_machine_config.clint =  convert_from_c(&c_config->clint);
    new_cpp_machine_config.htif = convert_from_c(&c_config->htif);
    new_cpp_machine_config.dhd = convert_from_c(&c_config->dhd);

    for (size_t i = 0; i < c_config->flash_drive_count; ++i) {
        new_cpp_machine_config.flash_drive.push_back(convert_from_c(&(c_config->flash_drive[i])));
    }

    return new_cpp_machine_config;
}

static const cm_machine_config *convert_to_c(const cartesi::machine_config &cpp_config) {
    auto *new_machine_config = new cm_machine_config{};

    new_machine_config->processor = convert_to_c(cpp_config.processor);
    new_machine_config->ram = convert_to_c(cpp_config.ram);
    new_machine_config->rom = convert_to_c(cpp_config.rom);
    new_machine_config->flash_drive_count = cpp_config.flash_drive.size();
    new_machine_config->flash_drive = new cm_flash_drive_config[cpp_config.flash_drive.size()];
    memset(new_machine_config->flash_drive, 0, sizeof(cm_flash_drive_config) * new_machine_config->flash_drive_count);
    for (size_t i = 0; i < new_machine_config->flash_drive_count; ++i) {
        new_machine_config->flash_drive[i] = convert_to_c(cpp_config.flash_drive[i]);
    }
    new_machine_config->clint = convert_to_c(cpp_config.clint);
    new_machine_config->htif = convert_to_c(cpp_config.htif);
    new_machine_config->dhd = convert_to_c(cpp_config.dhd);

    return new_machine_config;
}


// ----------------------------------------------
// Hash conversion functions
// ----------------------------------------------

cartesi::machine_merkle_tree::hash_type convert_from_c(const cm_hash* c_hash) {
    if (c_hash == nullptr) {
        throw std::invalid_argument("Invalid hash");
    }
    cartesi::machine_merkle_tree::hash_type cpp_hash; //In emulator this is std::array<unsigned char, hash_size>;
    memcpy(cpp_hash.data(), c_hash, sizeof(cm_hash));
    return cpp_hash;
}


// ----------------------------------------------
// Merkle tree proof conversion functions
// ----------------------------------------------

/// \brief Converts log2_size to index into siblings array
static int cm_log2_size_to_index(int log2_size, int log2_root_size) {
    // We know log2_root_size > 0, so log2_root_size-1 >= 0
    int index = log2_root_size - 1 - log2_size;
    return index;
}

static cm_merkle_tree_proof *convert_to_c(const cartesi::machine_merkle_tree::proof_type &proof) {
    auto *new_merkle_tree_proof = new cm_merkle_tree_proof{};

    new_merkle_tree_proof->log2_root_size = proof.get_log2_root_size();
    new_merkle_tree_proof->log2_target_size = proof.get_log2_target_size();
    new_merkle_tree_proof->target_address = proof.get_target_address();

    memcpy(&new_merkle_tree_proof->root_hash, static_cast<const uint8_t *>(proof.get_root_hash().data()),
            sizeof(cm_hash));
    memcpy(&new_merkle_tree_proof->target_hash, static_cast<const uint8_t *>(proof.get_target_hash().data()),
            sizeof(cm_hash));

    new_merkle_tree_proof->sibling_hashes_count =
            new_merkle_tree_proof->log2_root_size - new_merkle_tree_proof->log2_target_size;
    new_merkle_tree_proof->sibling_hashes = new cm_hash[new_merkle_tree_proof->sibling_hashes_count];
    memset(new_merkle_tree_proof->sibling_hashes, 0, sizeof(cm_hash) * new_merkle_tree_proof->sibling_hashes_count);


    for (size_t log2_size = new_merkle_tree_proof->log2_target_size;
         log2_size < new_merkle_tree_proof->log2_root_size; ++log2_size) {
        int current_index = cm_log2_size_to_index(log2_size, new_merkle_tree_proof->log2_root_size);
        const cartesi::machine_merkle_tree::hash_type sibling_hash = proof.get_sibling_hash(log2_size);
        memcpy(&(new_merkle_tree_proof->sibling_hashes[current_index]),
                static_cast<const uint8_t *>(sibling_hash.data()), sizeof(cm_hash));
    }

    return new_merkle_tree_proof;
}

static cartesi::machine_merkle_tree::proof_type convert_from_c(const cm_merkle_tree_proof *c_proof) {
    if (c_proof == nullptr) {
        throw std::invalid_argument("Invalid proof");
    }
    cartesi::machine_merkle_tree::proof_type cpp_proof(c_proof->log2_root_size, c_proof->log2_target_size);
    cpp_proof.set_target_address(c_proof->target_address);

    cpp_proof.set_root_hash(convert_from_c(&c_proof->root_hash));
    cpp_proof.set_target_hash(convert_from_c(&c_proof->target_hash));

    for (int log2_size = cpp_proof.get_log2_target_size();
         log2_size < cpp_proof.get_log2_root_size(); ++log2_size) {
        const int current_index = cm_log2_size_to_index(log2_size, cpp_proof.get_log2_root_size());
        cartesi::machine_merkle_tree::hash_type cpp_sibling_hash = convert_from_c(&c_proof->sibling_hashes[current_index]);
        cpp_proof.set_sibling_hash(cpp_sibling_hash, log2_size);
    }

    return cpp_proof;

}

// ----------------------------------------------
// Access log conversion functions
// ----------------------------------------------

static CM_ACCESS_TYPE convert_to_c(const cartesi::access_type type) {
    if (type == cartesi::access_type::read) {
        return CM_ACCESS_READ;
    } else {
        return CM_ACCESS_WRITE;
    }
}

static cartesi::access_type convert_from_c(const CM_ACCESS_TYPE c_type) {
    if (c_type == CM_ACCESS_READ) {
        return cartesi::access_type::read;
    } else {
        return cartesi::access_type::write;
    }
}

static cartesi::access_log::type convert_from_c(const cm_access_log_type *type) {
    if (type == nullptr) {
        throw std::invalid_argument("Invalid access log type");
    }
    cartesi::access_log::type cpp_type(type->proofs, type->annotations);
    return cpp_type;
}

static cm_access convert_to_c(const cartesi::access &cpp_access) {
    cm_access new_access{};
    new_access.type = convert_to_c(cpp_access.get_type());
    new_access.address = cpp_access.get_address();
    new_access.log2_size = cpp_access.get_log2_size();
    new_access.read_data_size = cpp_access.get_read().size();
    if (new_access.read_data_size > 0) {
        new_access.read_data = new uint8_t[new_access.read_data_size];
        memcpy(new_access.read_data, cpp_access.get_read().data(), new_access.read_data_size);
    } else {
        new_access.read_data = nullptr;
    }
    new_access.written_data_size = cpp_access.get_written().size();
    if (new_access.written_data_size > 0) {
        new_access.written_data = new uint8_t[new_access.written_data_size];
        memcpy(new_access.written_data, cpp_access.get_written().data(), new_access.written_data_size);
    } else {
        new_access.written_data = nullptr;
    }

    if (cpp_access.get_proof()) {
        new_access.proof = convert_to_c(*cpp_access.get_proof());
    } else {
        new_access.proof = nullptr;
    }

    return new_access;
}

static cartesi::access convert_from_c(const cm_access *c_access) {
    if (c_access == nullptr) {
        throw std::invalid_argument("Invalid access");
    }
    cartesi::access cpp_access{};
    cpp_access.set_type(convert_from_c(c_access->type));
    cpp_access.set_log2_size(c_access->log2_size);
    cpp_access.set_address(c_access->address);
    if (c_access->proof != nullptr) {
        cartesi::machine_merkle_tree::proof_type proof = convert_from_c(c_access->proof);
        cpp_access.set_proof(proof);
    }

    if (c_access->read_data_size > 0) {
        cpp_access.set_read(cartesi::access_data{c_access->read_data, c_access->read_data + c_access->read_data_size});
    }

    if (c_access->written_data_size > 0) {
        cpp_access.set_written(cartesi::access_data{c_access->written_data,
                                                    c_access->written_data + c_access->written_data_size});
    }

    return cpp_access;
}

static void cm_cleanup_access(cm_access *access) {
    if (access == nullptr) {
        return;
    }
    cm_delete_proof(access->proof);
    delete [] access->written_data;
    delete [] access->read_data;
}


static CM_BRACKET_TYPE convert_to_c(const cartesi::bracket_type type) {
    if (type == cartesi::bracket_type::begin) {
        return CM_BRACKET_BEGIN;
    } else {
        return CM_BRACKET_END;
    }
}

static cartesi::bracket_type convert_from_c(const CM_BRACKET_TYPE c_type) {
    if (c_type == CM_BRACKET_BEGIN) {
        return cartesi::bracket_type::begin;
    } else {
        return cartesi::bracket_type::end;
    }
}


static cm_bracket_note convert_to_c(const cartesi::bracket_note &cpp_bracket_note) {
    cm_bracket_note new_bracket_note{};
    new_bracket_note.type = convert_to_c(cpp_bracket_note.type);
    new_bracket_note.where = cpp_bracket_note.where;
    new_bracket_note.text = convert_to_c(cpp_bracket_note.text);
    return new_bracket_note;
}

static cartesi::bracket_note convert_from_c(const cm_bracket_note *c_bracket_note) {
    if (c_bracket_note == nullptr) {
        throw std::invalid_argument("Invalid bracket note");
    }
    cartesi::bracket_note cpp_bracket_note{};
    cpp_bracket_note.type = convert_from_c(c_bracket_note->type);
    cpp_bracket_note.where = c_bracket_note->where;
    cpp_bracket_note.text = null_to_empty(c_bracket_note->text);
    return cpp_bracket_note;
}

static void cm_cleanup_bracket_note(cm_bracket_note *bracket_note) {
    if (bracket_note == nullptr) {
        return;
    }
    delete [] bracket_note->text;
}

static cm_access_log *convert_to_c(const cartesi::access_log &cpp_access_log) {
    auto *new_access_log = new cm_access_log{};

    new_access_log->accesses_count = cpp_access_log.get_accesses().size();
    new_access_log->accesses = new cm_access[new_access_log->accesses_count];
    for (size_t  i = 0; i < new_access_log->accesses_count; ++i) {
        new_access_log->accesses[i] = convert_to_c(cpp_access_log.get_accesses()[i]);
    }

    new_access_log->brackets_count = cpp_access_log.get_brackets().size();
    new_access_log->brackets = new cm_bracket_note[new_access_log->brackets_count];
    for (size_t  i = 0; i < new_access_log->brackets_count; ++i) {
        new_access_log->brackets[i] = convert_to_c(cpp_access_log.get_brackets()[i]);
    }

    new_access_log->notes_count = cpp_access_log.get_notes().size();
    new_access_log->notes = new const char *[new_access_log->notes_count];
    for (size_t  i = 0; i < new_access_log->notes_count; ++i) {
        new_access_log->notes[i] = convert_to_c(cpp_access_log.get_notes()[i]);
    }

    new_access_log->log_type.annotations = cpp_access_log.get_log_type().has_annotations();
    new_access_log->log_type.proofs = cpp_access_log.get_log_type().has_proofs();

    return new_access_log;
}

static cartesi::access_log convert_from_c(const cm_access_log *c_acc_log) {
    if (c_acc_log == nullptr) {
        throw std::invalid_argument("Invalid access log");
    }

    std::vector <cartesi::access> accesses;
    for (size_t i = 0; i < c_acc_log->accesses_count; ++i) {
        accesses.push_back(convert_from_c(&c_acc_log->accesses[i]));
    }
    std::vector <cartesi::bracket_note> brackets;
    for (size_t i = 0; i < c_acc_log->brackets_count; ++i) {
        brackets.push_back(convert_from_c(&c_acc_log->brackets[i]));
    }

    std::vector <std::string> notes;
    for (size_t i = 0; i < c_acc_log->notes_count; ++i) {
        notes.push_back(null_to_empty(c_acc_log->notes[i]));
    }
    cartesi::access_log new_cpp_acc_log(accesses, brackets, notes, convert_from_c(&c_acc_log->log_type));
    return new_cpp_acc_log;
}


// -----------------------------------------------------
// Public API functions for generation of default configs
// -----------------------------------------------------
const cm_machine_config *cm_new_default_machine_config(void) {
    cartesi::machine_config cpp_config = cartesi::machine::get_default_config();

    return convert_to_c(cpp_config);
}

void cm_delete_machine_config(const cm_machine_config *config) {

    if (config == nullptr) {
        return;
    }

    delete[] config->dhd.image_filename;
    for (size_t i = 0; i < config->flash_drive_count; ++i) {
     delete[] config->flash_drive[i].image_filename;
    }
    delete[] config->flash_drive;
    delete[] config->rom.image_filename;
    delete[] config->rom.bootargs;
    delete[] config->ram.image_filename;

    delete config;
}

static inline cartesi::i_virtual_machine *create_virtual_machine(const cartesi::machine_config &c,
                                                                 const cartesi::machine_runtime_config &r) {
    return new cartesi::virtual_machine(c, r);

}

static inline cartesi::i_virtual_machine *load_virtual_machine(const char *dir,
                                                               const cartesi::machine_runtime_config &r) {
    return new cartesi::virtual_machine(null_to_empty(dir), r);
}

static inline cartesi::i_virtual_machine *create_grpc_virtual_machine(const char* /* address */,
                                                                      const cartesi::machine_config& /* c */,
                                                                      const cartesi::machine_runtime_config& /* r */) {
    //todo Implement
    throw std::runtime_error("Not implemented");
    return nullptr;
}

static inline cartesi::i_virtual_machine *load_grpc_virtual_machine(const char* /* address */,
                                                                    const char* /* dir */,
                                                                    const cartesi::machine_runtime_config& /* r */) {
    //todo Implement
    throw std::runtime_error("Not implemented");
    return nullptr;
}

int cm_create_machine(const cm_machine_config *config, const cm_machine_runtime_config *runtime_config,
                      cm_machine **new_machine, char **err_msg) try {
    const cartesi::machine_config c = convert_from_c(config);
    const cartesi::machine_runtime_config r = convert_from_c(runtime_config);
    *new_machine = static_cast<cm_machine *>(create_virtual_machine(c, r));
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}

int cm_load_machine(const char *dir, const cm_machine_runtime_config *runtime_config,
                               cm_machine **new_machine, char **err_msg) try {
    const cartesi::machine_runtime_config r = convert_from_c(runtime_config);
    *new_machine = static_cast<cm_machine *>(load_virtual_machine(dir, r));
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}

int cm_create_grpc_machine(const cm_machine_config *config, const cm_machine_runtime_config *runtime_config,
                           const char *address, cm_machine **new_machine, char **err_msg) try {
    const cartesi::machine_config c = convert_from_c(config);
    const cartesi::machine_runtime_config r = convert_from_c(runtime_config);
    *new_machine = static_cast<cm_machine *>(create_grpc_virtual_machine(address, c, r));
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_load_grpc_machine(const char *dir, const cm_machine_runtime_config *runtime_config,
                                    const char *address, cm_machine **new_machine, char **err_msg) try {
    const cartesi::machine_runtime_config r = convert_from_c(runtime_config);
    *new_machine = static_cast<cm_machine *>(load_grpc_virtual_machine(address, dir, r));
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


void cm_delete_machine(cm_machine *m) {
    if (m == nullptr) {
        return;
    }
    auto *cpp_machine = convert_from_c(m);
    delete cpp_machine;
}

int cm_store(cm_machine *m, const char *dir, char **err_msg) try {
    auto *cpp_machine = convert_from_c(m);
    cpp_machine->store(null_to_empty(dir));
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_machine_run(cm_machine *m, uint64_t mcycle_end, char **err_msg) try {
    auto *cpp_machine = convert_from_c(m);
    cpp_machine->run(mcycle_end);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_step(cm_machine *m, cm_access_log_type log_type, bool one_based,
            cm_access_log **access_log, char **err_msg) try {
    auto *cpp_machine = convert_from_c(m);
    cartesi::access_log::type cpp_log_type{log_type.proofs, log_type.annotations};
    cartesi::access_log cpp_access_log = cpp_machine->step(cpp_log_type, one_based);
    *access_log = convert_to_c(cpp_access_log);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


void cm_delete_access_log(cm_access_log *acc_log) {
    if (acc_log == nullptr) {
        return;
    }

    for (size_t i = 0; i < acc_log->notes_count; ++i) {
        delete[] acc_log->notes[i];
    }
    delete[] acc_log->notes;
    for (size_t i = 0; i < acc_log->brackets_count; ++i) {
        cm_cleanup_bracket_note(&acc_log->brackets[i]);
    }
    delete[] acc_log->brackets;
    for (size_t i = 0; i < acc_log->accesses_count; ++i) {
        cm_cleanup_access(&acc_log->accesses[i]);
    }
    delete[] acc_log->accesses;
    delete acc_log;
}

int cm_verify_access_log(const cm_access_log *log, const cm_machine_runtime_config *runtime_config,
                         bool one_based, char **err_msg) try {
    const cartesi::access_log cpp_log = convert_from_c(log);
    const cartesi::machine_runtime_config cpp_runtime_config = convert_from_c(runtime_config);
    cartesi::machine::verify_access_log(cpp_log, cpp_runtime_config, one_based);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_verify_state_transition(const cm_hash *root_hash_before,
                               const cm_access_log *log, const cm_hash *root_hash_after,
                               const cm_machine_runtime_config *runtime_config, bool one_based,
                               char **err_msg) try {
    const cartesi::machine::hash_type cpp_root_hash_before = convert_from_c(root_hash_before);
    const cartesi::machine::hash_type cpp_root_hash_after = convert_from_c(root_hash_after);
    const cartesi::access_log cpp_log = convert_from_c(log);
    const cartesi::machine_runtime_config cpp_runtime_config = convert_from_c(runtime_config);
    cartesi::machine::verify_state_transition(cpp_root_hash_before, cpp_log, cpp_root_hash_after,
                                              cpp_runtime_config, one_based);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_update_merkle_tree(cm_machine *m, char **err_msg) try {
    auto *cpp_machine = convert_from_c(m);
    bool result = cpp_machine->update_merkle_tree();
    if (result) {
        return cm_result_success(err_msg);
    } else {
        *err_msg = get_error_message_unknown();
        return CM_ERROR_UNKNOWN;
    }
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_get_proof(const cm_machine *m, uint64_t address, int log2_size,
                 cm_merkle_tree_proof **proof, char **err_msg) try {
    const auto *cpp_machine = convert_from_c(m);
    const cartesi::machine_merkle_tree::proof_type cpp_proof = cpp_machine->get_proof(address, log2_size);
    *proof = convert_to_c(cpp_proof);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


void cm_delete_proof(cm_merkle_tree_proof *proof) {
    if (proof == nullptr) {
        return;
    }
    delete[] proof->sibling_hashes;
    delete proof;
}


int cm_get_root_hash(const cm_machine *m, cm_hash *hash, char **err_msg) try {
    const auto *cpp_machine = convert_from_c(m);
    cartesi::machine_merkle_tree::hash_type cpp_hash;
    cpp_machine->get_root_hash(cpp_hash);
    memcpy(hash, static_cast<const uint8_t *>(cpp_hash.data()), sizeof(cm_hash));
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_verify_merkle_tree(const cm_machine *m, bool *result, char **err_msg) try {
    const auto *cpp_machine = convert_from_c(m);
    *result = cpp_machine->verify_merkle_tree();
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_read_csr(const cm_machine *m, CM_PROC_CSR r, uint64_t *val, char **err_msg) try {
    const auto *cpp_machine = convert_from_c(m);
    auto cpp_csr = static_cast<cartesi::machine::csr>(r);
    *val = cpp_machine->read_csr(cpp_csr);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_write_csr(cm_machine *m, CM_PROC_CSR w, uint64_t val, char **err_msg) try {
    auto *cpp_machine = convert_from_c(m);
    auto cpp_csr = static_cast<cartesi::machine::csr>(w);
    cpp_machine->write_csr(cpp_csr, val);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


uint64_t cm_get_csr_address(CM_PROC_CSR w) {
    auto cpp_csr = static_cast<cartesi::machine::csr>(w);
    return cartesi::machine::get_csr_address(cpp_csr);
}

int cm_read_word(const cm_machine *m, uint64_t word_address, uint64_t *word_value, char **err_msg) try {
    const auto *cpp_machine = convert_from_c(m);
    uint64_t cpp_word_value{0};
    if (cpp_machine->read_word(word_address, cpp_word_value)) {
        *word_value = cpp_word_value;
        return cm_result_success(err_msg);
    } else {
        return cm_result_unknown_error(err_msg);
    }
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_read_memory(const cm_machine *m, uint64_t address, unsigned char *data, uint64_t length, char **err_msg) try {
    const auto *cpp_machine = convert_from_c(m);
    cpp_machine->read_memory(address, data, length);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_write_memory(cm_machine *m, uint64_t address, const unsigned char *data, size_t length, char **err_msg) try {
    auto *cpp_machine = convert_from_c(m);
    cpp_machine->write_memory(address, data, length);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_read_x(const cm_machine *m, int i, uint64_t *val, char **err_msg) try {
    const auto *cpp_machine = convert_from_c(m);
    *val = cpp_machine->read_x(i);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_write_x(cm_machine *m, int i, uint64_t val, char **err_msg) try {
    auto *cpp_machine = convert_from_c(m);
    cpp_machine->write_x(i, val);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


uint64_t cm_get_x_address(int i) {
    return cartesi::machine::get_x_address(i);
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define IMPL_MACHINE_READ_WRITE(field) \
int cm_read_##field(const cm_machine *m, uint64_t *val, char **err_msg) try { \
    const auto *cpp_machine = convert_from_c(m); \
    *val = cpp_machine->read_##field();\
    return cm_result_success(err_msg);    \
} catch (...) {                        \
    return cm_result_failure(err_msg);    \
} \
int cm_write_##field(cm_machine *m, uint64_t val, char **err_msg) try { \
    auto *cpp_machine = convert_from_c(m); \
    cpp_machine->write_##field(val);   \
    return cm_result_success(err_msg);    \
} catch (...) {                        \
    return cm_result_failure(err_msg);                                   \
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define IMPL_MACHINE_READ(field) \
int cm_read_##field(const cm_machine *m, uint64_t *val, char **err_msg) try { \
    const auto *cpp_machine = convert_from_c(m); \
    *val = cpp_machine->read_##field();\
    return cm_result_success(err_msg);    \
} catch (...) {                        \
    return cm_result_failure(err_msg);    \
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define IMPL_MACHINE_WRITE(field) \
int cm_write_##field(cm_machine *m, uint64_t val, char **err_msg) try { \
    auto *cpp_machine = convert_from_c(m); \
    cpp_machine->write_##field(val);   \
    return cm_result_success(err_msg);    \
} catch (...) {                        \
    return cm_result_failure(err_msg);                                   \
}

IMPL_MACHINE_READ_WRITE(pc)
IMPL_MACHINE_READ(mvendorid)
IMPL_MACHINE_READ(marchid)
IMPL_MACHINE_READ(mimpid)
IMPL_MACHINE_READ_WRITE(mcycle)
IMPL_MACHINE_READ_WRITE(minstret)
IMPL_MACHINE_READ_WRITE(mstatus)
IMPL_MACHINE_READ_WRITE(mtvec)
IMPL_MACHINE_READ_WRITE(mscratch)
IMPL_MACHINE_READ_WRITE(mepc)
IMPL_MACHINE_READ_WRITE(mcause)
IMPL_MACHINE_READ_WRITE(mtval)
IMPL_MACHINE_READ_WRITE(misa)
IMPL_MACHINE_READ_WRITE(mie)
IMPL_MACHINE_READ_WRITE(mip)
IMPL_MACHINE_READ_WRITE(medeleg)
IMPL_MACHINE_READ_WRITE(mideleg)
IMPL_MACHINE_READ_WRITE(mcounteren)
IMPL_MACHINE_READ_WRITE(stvec)
IMPL_MACHINE_READ_WRITE(sscratch)
IMPL_MACHINE_READ_WRITE(sepc)
IMPL_MACHINE_READ_WRITE(scause)
IMPL_MACHINE_READ_WRITE(stval)
IMPL_MACHINE_READ_WRITE(satp)
IMPL_MACHINE_READ_WRITE(scounteren)
IMPL_MACHINE_READ_WRITE(ilrsc)
IMPL_MACHINE_READ_WRITE(iflags)
IMPL_MACHINE_READ_WRITE(htif_tohost)
IMPL_MACHINE_READ(htif_tohost_dev)
IMPL_MACHINE_READ(htif_tohost_cmd)
IMPL_MACHINE_READ(htif_tohost_data)
IMPL_MACHINE_READ_WRITE(htif_fromhost)
IMPL_MACHINE_WRITE(htif_fromhost_data)
IMPL_MACHINE_READ_WRITE(htif_ihalt)
IMPL_MACHINE_READ_WRITE(htif_iconsole)
IMPL_MACHINE_READ_WRITE(htif_iyield)
IMPL_MACHINE_READ_WRITE(clint_mtimecmp)
IMPL_MACHINE_READ_WRITE(dhd_tstart)
IMPL_MACHINE_READ_WRITE(dhd_tlength)
IMPL_MACHINE_READ_WRITE(dhd_dlength)
IMPL_MACHINE_READ_WRITE(dhd_hlength)




uint64_t cm_packed_iflags(int PRV, int Y, int H) {
    return cartesi::machine_state::packed_iflags(PRV, Y, H);
}

int cm_read_dhd_h(const cm_machine *m, int i, uint64_t *val, char **err_msg) try {
    const auto *cpp_machine = convert_from_c(m);
    *val = cpp_machine->read_dhd_h(i);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_write_dhd_h(cm_machine *m, int i, uint64_t val, char **err_msg) try {
    auto *cpp_machine = convert_from_c(m);
    cpp_machine->write_dhd_h(i, val);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


uint64_t cm_get_dhd_h_address(int i) {
    return cartesi::machine::get_dhd_h_address(i);
}

int cm_read_iflags_Y(const cm_machine *m, bool *val, char **err_msg) try {
    const auto *cpp_machine = convert_from_c(m);
    *val = cpp_machine->read_iflags_Y();
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_reset_iflags_Y(cm_machine *m, char **err_msg) try {
    auto *cpp_machine = convert_from_c(m);
    cpp_machine->reset_iflags_Y();
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_set_iflags_Y(cm_machine *m, char **err_msg) try {
    auto *cpp_machine = convert_from_c(m);
    cpp_machine->set_iflags_Y();
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_read_iflags_H(const cm_machine *m, bool *val, char **err_msg) try {
    const auto *cpp_machine = convert_from_c(m);
    *val = cpp_machine->read_iflags_H();
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_set_iflags_H(cm_machine *m, char **err_msg) try {
    auto *cpp_machine = convert_from_c(m);
    cpp_machine->set_iflags_H();
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_dump_pmas(const cm_machine *m, char **err_msg) try {
    const auto *cpp_machine = convert_from_c(m);
    cpp_machine->dump_pmas();
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_verify_dirty_page_maps(const cm_machine *m, bool *result, char **err_msg) try {
    const auto *cpp_machine = convert_from_c(m);
    *result = cpp_machine->verify_dirty_page_maps();
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_get_initial_config(const cm_machine *m, const cm_machine_config **config, char **err_msg) try {
    const auto *cpp_machine = convert_from_c(m);
    cartesi::machine_config cpp_config = cpp_machine->get_initial_config();
    *config = convert_to_c(cpp_config);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}

int cm_get_default_config(const cm_machine_config **config, char **err_msg) try {
    const cartesi::machine_config cpp_config = cartesi::machine::get_default_config();
    *config = convert_to_c(cpp_config);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


int cm_replace_flash_drive(cm_machine *m, const cm_flash_drive_config *new_flash, char **err_msg) try {
    auto *cpp_machine = convert_from_c(m);
    cartesi::flash_drive_config cpp_flash_config = convert_from_c(new_flash);
    cpp_machine->replace_flash_drive(cpp_flash_config);
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}


void cm_delete_error_message(const char* err_msg) {
    delete[] err_msg;
}

void cm_delete_machine_runtime_config(const cm_machine_runtime_config *config) {
    if (config == nullptr) {
        return;
    }

    delete[] config->dhd.source_address;
    delete config;
}

int cm_destroy(cm_machine *m, char **err_msg) try {
    cartesi::i_virtual_machine *cpp_machine = reinterpret_cast<cartesi::i_virtual_machine *>(m);
    cpp_machine->destroy();
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}

int cm_snapshot(cm_machine *m, char **err_msg) try {
    cartesi::i_virtual_machine *cpp_machine = reinterpret_cast<cartesi::i_virtual_machine *>(m);
    cpp_machine->snapshot();
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}

int cm_rollback(cm_machine *m, char **err_msg) try {
    cartesi::i_virtual_machine *cpp_machine = reinterpret_cast<cartesi::i_virtual_machine *>(m);
    cpp_machine->rollback();
    return cm_result_success(err_msg);
} catch (...) {
    return cm_result_failure(err_msg);
}
