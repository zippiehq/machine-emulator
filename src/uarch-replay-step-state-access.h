// Copyright Cartesi and individual authors (see AUTHORS)
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along
// with this program (see COPYING). If not, see <https://www.gnu.org/licenses/>.
//

#ifndef UARCH_REPLAY_STATE_ACCESS_H
#define UARCH_REPLAY_STATE_ACCESS_H

/// \file
/// \brief State access implementation that replays recorded state accesses

#include <boost/container/static_vector.hpp>
#include <cassert>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include "i-uarch-step-state-access.h"
#include "shadow-state.h"
#include "uarch-bridge.h"

namespace cartesi {

class uarch_replay_step_state_access : public i_uarch_step_state_access<uarch_replay_step_state_access> {
    using tree_type = machine_merkle_tree;
    using hash_type = tree_type::hash_type;
    using hasher_type = tree_type::hasher_type;
    using proof_type = tree_type::proof_type;

    ///< Access log generated by step
    const std::vector<access> &m_accesses;
    ///< Whether to verify proofs in access log
    bool m_verify_proofs;
    ///< Next access
    unsigned m_next_access;
    ///< Add to indices reported in errors
    int m_one_based;

    ///< Root hash before next access
    hash_type m_root_hash;
    ///< Hasher needed to verify proofs
    hasher_type m_hasher;

public:
    /// \brief Constructor from log of word accesses.
    /// \param log Access log to be replayed
    /// \param verify_proofs Whether to verify proofs in access log
    /// \param initial_hash  Initial root hash
    /// \param one_based Whether to add one to indices reported in errors
    explicit uarch_replay_step_state_access(const access_log &log, bool verify_proofs, const hash_type &initial_hash,
        bool one_based) :
        m_accesses(log.get_accesses()),
        m_verify_proofs(verify_proofs),
        m_next_access{0},
        m_one_based{one_based},
        m_root_hash{initial_hash},
        m_hasher{} {
        if (m_accesses.empty()) {
            throw std::invalid_argument{"the access log has no accesses"};
        }
        if (m_verify_proofs) {
            if (!log.get_log_type().has_proofs()) {
                throw std::invalid_argument{"log has no proofs"};
            }
        }
    }

    /// \brief No copy constructor
    uarch_replay_step_state_access(const uarch_replay_step_state_access &) = delete;
    /// \brief No copy assignment
    uarch_replay_step_state_access &operator=(const uarch_replay_step_state_access &) = delete;
    /// \brief No move constructor
    uarch_replay_step_state_access(uarch_replay_step_state_access &&) = delete;
    /// \brief No move assignment
    uarch_replay_step_state_access &operator=(uarch_replay_step_state_access &&) = delete;
    /// \brief Default destructor
    ~uarch_replay_step_state_access() = default;

    void finish(void) {
        if (m_next_access != m_accesses.size()) {
            throw std::invalid_argument{"too many word accesses in log"};
        }
    }

    void get_root_hash(hash_type &hash) const {
        hash = m_root_hash;
    }

private:
    auto access_to_report(void) const {
        return m_next_access + m_one_based;
    }

    static void get_hash(hasher_type &hasher, const access_data &data, hash_type &hash) {
        get_merkle_tree_hash(hasher, data.data(), data.size(), sizeof(uint64_t), hash);
    }

    /// \brief Checks a logged word read and advances log.
    /// \param paligned Physical address in the machine state,
    /// aligned to 64-bits.
    /// \param text Textual description of the access.
    /// \returns Value read.
    uint64_t check_read_word(uint64_t paligned, const char *text) {
        return get_word_access_data(check_read(paligned, 3, text));
    }

    /// \brief Checks a logged read and advances log.
    /// \param paligned Physical address in the machine state,
    /// aligned to the access size.
    /// \param log2_size Log2 of access size.
    /// \param text Textual description of the access.
    /// \returns Value read.
    const access_data &check_read(uint64_t paligned, int log2_size, const char *text) {
        if (m_next_access >= m_accesses.size()) {
            throw std::invalid_argument{"too few accesses in log"};
        }
        const auto &access = m_accesses[m_next_access];
        if ((paligned & ((UINT64_C(1) << log2_size) - 1)) != 0) {
            throw std::invalid_argument{"access address not aligned to size"};
        }
        if (access.get_address() != paligned) {
            std::ostringstream err;
            err << "expected access " << access_to_report() << " to read " << text << " at address 0x" << std::hex
                << paligned << "(" << std::dec << paligned << ")";
            throw std::invalid_argument{err.str()};
        }
        if (log2_size < 3 || log2_size > 63) {
            throw std::invalid_argument{"invalid access size"};
        }
        if (access.get_log2_size() != log2_size) {
            throw std::invalid_argument{"expected access " + std::to_string(access_to_report()) + " to read 2^" +
                std::to_string(log2_size) + " bytes from " + text};
        }
        if (access.get_type() != access_type::read) {
            throw std::invalid_argument{"expected access " + std::to_string(access_to_report()) + " to read " + text};
        }
        if (!access.get_read().has_value()) {
            throw std::invalid_argument{
                "missing read " + std::string(text) + " data at access " + std::to_string(access_to_report())};
        }
        const auto &value_read = access.get_read().value(); // NOLINT(bugprone-unchecked-optional-access)
        if (value_read.size() != UINT64_C(1) << log2_size) {
            throw std::invalid_argument{"expected read " + std::string(text) + " data to contain 2^" +
                std::to_string(log2_size) + " bytes at access " + std::to_string(access_to_report())};
        }
        // check if logged read data hashes to the logged read hash
        hash_type computed_hash{};
        get_hash(m_hasher, value_read, computed_hash);
        if (access.get_read_hash() != computed_hash) {
            throw std::invalid_argument{"logged read data of " + std::string(text) +
                " data does not hash to the logged read hash at access " + std::to_string(access_to_report())};
        }
        if (m_verify_proofs) {
            auto proof = access.make_proof(m_root_hash);
            if (!proof.verify(m_hasher)) {
                throw std::invalid_argument{"Mismatch in root hash of access " + std::to_string(access_to_report())};
            }
        }
        m_next_access++;
        return access.get_read().value();
    }

    /// \brief Checks a logged word write and advances log.
    /// \param paligned Physical address in the machine state,
    /// aligned to a 64-bit word.
    /// \param word Word value to write.
    /// \param text Textual description of the access.
    /// \returns Value read.
    void check_write_word(uint64_t paligned, uint64_t word, const char *text) {
        access_data val;
        set_word_access_data(word, val);
        check_write(paligned, val, 3, text);
    }

    /// \brief Checks a logged write and advances log.
    /// \param paligned Physical address in the machine state,
    /// aligned to the access size.
    /// \param val Value to write.
    /// \param log2_size Log2 of access size.
    /// \param text Textual description of the access.
    void check_write(uint64_t paligned, const access_data &val, int log2_size, const char *text) {
        if (m_next_access >= m_accesses.size()) {
            throw std::invalid_argument{"too few accesses in log"};
        }
        const auto &access = m_accesses[m_next_access];
        if ((paligned & ((UINT64_C(1) << log2_size) - 1)) != 0) {
            throw std::invalid_argument{"access address not aligned to size"};
        }
        if (access.get_address() != paligned) {
            std::ostringstream err;
            err << "expected access " << access_to_report() << " to write " << text << " at address 0x" << std::hex
                << paligned << "(" << std::dec << paligned << ")";
            throw std::invalid_argument{err.str()};
        }
        if (log2_size < 3 || log2_size > 63) {
            throw std::invalid_argument{"invalid access size"};
        }
        if (access.get_log2_size() != log2_size) {
            throw std::invalid_argument{"expected access " + std::to_string(access_to_report()) + " to write 2^" +
                std::to_string(log2_size) + " bytes from " + text};
        }
        if (access.get_type() != access_type::write) {
            throw std::invalid_argument{"expected access " + std::to_string(access_to_report()) + " to write " + text};
        }
        if (access.get_read().has_value()) {
            const auto &value_read = access.get_read().value(); // NOLINT(bugprone-unchecked-optional-access)
            if (value_read.size() != UINT64_C(1) << log2_size) {
                throw std::invalid_argument{"expected overwritten data from " + std::string(text) + " to contain 2^" +
                    std::to_string(log2_size) + " bytes at access " + std::to_string(access_to_report())};
            }
            // check if read data hashes to the logged read hash
            hash_type computed_hash{};
            get_hash(m_hasher, value_read, computed_hash);
            if (access.get_read_hash() != computed_hash) {
                throw std::invalid_argument{"logged read data of " + std::string(text) +
                    " does not hash to the logged read hash at access " + std::to_string(access_to_report())};
            }
        }
        if (!access.get_written_hash().has_value()) {
            throw std::invalid_argument{
                "missing written " + std::string(text) + " hash at access " + std::to_string(access_to_report())};
        }
        const auto &written_hash = access.get_written_hash().value(); // NOLINT(bugprone-unchecked-optional-access)
        // check if value being written hashes to the logged written hash
        hash_type computed_hash{};
        get_hash(m_hasher, val, computed_hash);
        if (written_hash != computed_hash) {
            throw std::invalid_argument{"value being written to " + std::string(text) +
                " does not hash to the logged written hash at access " + std::to_string(access_to_report())};
        }
        if (access.get_written().has_value()) {
            const auto &value_written = access.get_written().value(); // NOLINT(bugprone-unchecked-optional-access)
            if (value_written.size() != UINT64_C(1) << log2_size) {
                throw std::invalid_argument{"expected written " + std::string(text) + " data to contain 2^" +
                    std::to_string(log2_size) + " bytes at access " + std::to_string(access_to_report())};
            }
            // check if written data hashes to the logged written hash
            get_hash(m_hasher, value_written, computed_hash);
            if (written_hash != computed_hash) {
                throw std::invalid_argument{"logged written data of " + std::string(text) +
                    " does not hash to the logged written hash at access " + std::to_string(access_to_report())};
            }
        }
        if (m_verify_proofs) {
            auto proof = access.make_proof(m_root_hash);
            if (!proof.verify(m_hasher)) {
                throw std::invalid_argument{"Mismatch in root hash of access " + std::to_string(access_to_report())};
            }
            // Update root hash to reflect the data written by this access
            m_root_hash = proof.bubble_up(m_hasher, written_hash);
        }
        m_next_access++;
    }

    friend i_uarch_step_state_access<uarch_replay_step_state_access>;

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void do_push_bracket(bracket_type type, const char *text) {
        (void) type;
        (void) text;
    }

    int do_make_scoped_note(const char *text) { // NOLINT(readability-convert-member-functions-to-static)
        (void) text;
        return 0;
    }

    uint64_t do_read_x(int reg) {
        return check_read_word(shadow_uarch_state_get_x_abs_addr(reg), "uarch.x");
    }

    void do_write_x(int reg, uint64_t val) {
        assert(reg != 0);
        check_write_word(shadow_uarch_state_get_x_abs_addr(reg), val, "uarch.x");
    }

    uint64_t do_read_pc() {
        return check_read_word(shadow_uarch_state_get_csr_abs_addr(shadow_uarch_state_csr::pc), "uarch.pc");
    }

    void do_write_pc(uint64_t val) {
        check_write_word(shadow_uarch_state_get_csr_abs_addr(shadow_uarch_state_csr::pc), val, "uarch.pc");
    }

    uint64_t do_read_cycle() {
        return check_read_word(shadow_uarch_state_get_csr_abs_addr(shadow_uarch_state_csr::cycle), "uarch.uarch_cycle");
    }

    void do_write_cycle(uint64_t val) {
        check_write_word(shadow_uarch_state_get_csr_abs_addr(shadow_uarch_state_csr::cycle), val, "uarch.cycle");
    }

    bool do_read_halt_flag() {
        return check_read_word(shadow_uarch_state_get_csr_abs_addr(shadow_uarch_state_csr::halt_flag),
            "uarch.halt_flag");
    }

    void do_set_halt_flag() {
        check_write_word(shadow_uarch_state_get_csr_abs_addr(shadow_uarch_state_csr::halt_flag), true,
            "uarch.halt_flag");
    }

    void do_reset_halt_flag() {
        check_write_word(shadow_uarch_state_get_csr_abs_addr(shadow_uarch_state_csr::halt_flag), false,
            "uarch.halt_flag");
    }

    uint64_t do_read_word(uint64_t paddr) {
        assert((paddr & (sizeof(uint64_t) - 1)) == 0);
        // Get the name of the state register identified by this address
        const auto *name = uarch_bridge::get_register_name(paddr);
        if (!name) {
            // this is a regular memory access
            name = "memory";
        }
        return check_read_word(paddr, name);
    }

    void do_write_word(uint64_t paddr, uint64_t data) {
        assert((paddr & (sizeof(uint64_t) - 1)) == 0);
        // Get the name of the state register identified by this address
        const auto *name = uarch_bridge::get_register_name(paddr);
        if (!name) {
            // this is a regular memory access
            name = "memory";
        }
        check_write_word(paddr, data, name);
    }
};

} // namespace cartesi

#endif
