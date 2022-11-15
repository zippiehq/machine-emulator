// Copyright 2019 Cartesi Pte. Ltd.
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

#ifndef PMA_H
#define PMA_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "pma-constants.h"
#include "pma-driver.h"

namespace cartesi {

// Forward declarations
class pma_entry;
class machine;

/// \file
/// \brief Physical memory attributes interface

/// \brief Prototype for callback invoked when machine wants to peek into a range with no side-effects.
/// \param pma Reference to corresponding PMA entry.
/// \param m Reference to associated machine.
/// \param page_offset Offset of page start within range. Must be aligned to PMA_PAGE_SIZE.
/// \param page_data Receives pointer to start of page data, or nullptr if page is constant *and* pristine.
/// \param scratch Pointer to memory buffer that must be able to hold PMA_PAGE_SIZE bytes.
/// \returns True if operation succeeded, false otherwise.
using pma_peek = bool (*)(const pma_entry &, const machine &, uint64_t, const unsigned char **, unsigned char *);

/// \brief Default peek callback issues error on peeks.
bool pma_peek_error(const pma_entry &, const machine &, uint64_t, const unsigned char **, unsigned char *);

/// \brief Data for IO ranges.
class pma_device final {

    const pma_driver *m_driver; ///< Driver with callbacks.
    void *m_context;            ///< Context to pass to callbacks.

public:
    /// \brief Constructor from entries.
    /// \param description Informative description of PMA entry for use in error messages
    /// \param context Context to pass to callbacks.
    /// \param driver Pointer to driver with callbacks.
    pma_device(const std::string &description, const pma_driver *driver, void *context) :
        m_driver{driver},
        m_context{context} {
        (void) description;
    }

    /// \brief Returns context to pass to callbacks.
    void *get_context(void) {
        return m_context;
    }

    /// \brief Returns context to pass to callbacks.
    void *get_context(void) const {
        // Discard qualifier on purpose because the context
        // is none of our business.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        return const_cast<void *>(m_context);
    }

    /// \brief Returns pointer to driver with callbacks
    const pma_driver *get_driver(void) const {
        return m_driver;
    }
};

// For performance reasons, we can't possibly invoke a
// function every time we want to read from a memory
// range, so memory ranges do not use a driver like
// IO ranges do. So we use naked pointers to host memory.

/// \brief Data for memory ranges.
class pma_memory final {

    uint64_t m_length;            ///< Length of memory range (copy of PMA length field).
    unsigned char *m_host_memory; ///< Start of associated memory region in host.
    int m_backing_file;           ///< File descryptor for mmaped memory.

    /// \brief Close file and/or release memory.
    void release(void);

public:
    /// \brief Mmap'd range data (shared or not).
    struct mmapd {
        bool shared;
    };

    /// \brief Constructor for mmap'd ranges.
    /// \param description Informative description of PMA entry for use in error messages
    /// \param length Length of range.
    /// \param path Path for backing file.
    /// \param m Mmap'd range data (shared or not).
    pma_memory(const std::string &description, uint64_t length, const std::string &path, const mmapd &m);

    /// \brief Calloc'd range data (just a tag).
    struct callocd {};

    /// \brief Mock'd range data (just a tag).
    struct mockd {};

    /// \brief Constructor for calloc'd ranges.
    /// \param description Informative description of PMA entry for use in error messages
    /// \param length Length of range.
    /// \param path Path for backing file.
    /// \param c Calloc'd range data (just a tag).
    pma_memory(const std::string &description, uint64_t length, const std::string &path, const callocd &c);

    /// \brief Constructor for calloc'd ranges.
    /// \param description Informative description of PMA entry for use in error messages
    /// \param length Length of range.
    /// \param c Calloc'd range data (just a tag).
    pma_memory(const std::string &description, uint64_t length, const callocd &c);

    /// \brief Constructor for mock ranges.
    /// \param description Informative description of PMA entry for use in error messages
    /// \param length Length of range.
    /// \param m Mock'd range data (just a tag).
    pma_memory(const std::string &description, uint64_t length, const mockd &m);

    /// \brief No copy constructor
    pma_memory(const pma_memory &) = delete;

    /// \brief No copy assignment
    pma_memory &operator=(const pma_memory &) = delete;

    /// \brief Move constructor
    pma_memory(pma_memory &&) noexcept;

    /// \brief Move assignment
    pma_memory &operator=(pma_memory &&) noexcept;

    /// \brief Destructor
    ~pma_memory(void);

    /// \brief Returns start of associated memory region in host
    unsigned char *get_host_memory(void) {
        return m_host_memory;
    }

    /// \brief Returns start of associated memory region in host
    const unsigned char *get_host_memory(void) const {
        return m_host_memory;
    }

    /// \brief Returns file descryptor for mmaped memory.
    int get_backing_file(void) const {
        return m_backing_file;
    }

    /// \brief Returns copy of PMA length field (needed for munmap).
    uint64_t get_length(void) const {
        return m_length;
    }
};

/// \brief Data for empty memory ranges (nothing, really)
struct pma_empty final {};

/// \brief Physical Memory Attribute entry.
/// \details The target's physical memory layout is described by an
/// array of PMA entries.
class pma_entry final {
public:
    ///< Exploded flags for PMA entry.
    struct flags {
        // bool M = std::holds_alternative<pma_memory>(data);
        // bool IO = std::holds_alternative<pma_device>(data);
        // bool E = std::holds_alternative<pma_empty>(data);
        bool R;
        bool W;
        bool X;
        bool IR;
        bool IW;
        PMA_ISTART_DID DID;
    };

private:
    std::string m_description; ///< Informative description of PMA entry for use in error messages.
    uint64_t m_start;          ///< Start of physical memory range in target.
    uint64_t m_length;         ///< Length of physical memory range in target.
    int m_index;               ///< PMA entry index in target.
    flags m_flags;             ///< PMA Flags

    pma_peek m_peek; ///< Callback for peek operations.

    std::vector<uint8_t> m_dirty_page_map; ///< Map of dirty pages.

    std::variant<pma_empty, ///< Data specific to E ranges
        pma_device,         ///< Data specific to IO ranges
        pma_memory          ///< Data specific to M ranges
        >
        m_data;

public:
    /// \brief No copy constructor
    pma_entry(const pma_entry &) = delete;
    /// \brief No copy assignment
    pma_entry &operator=(const pma_entry &) = delete;
    /// \brief Default move constructor
    pma_entry(pma_entry &&) = default;
    /// \brief Default move assignment
    pma_entry &operator=(pma_entry &&) = default;
    /// \bried Default destructor
    ~pma_entry() = default;

    /// \brief Constructor for empty entry
    /// \param description Informative description of PMA entry for use in error messages
    /// \param start Start of range.
    /// \param length Length of range.
    pma_entry(std::string description, uint64_t start, uint64_t length) :
        m_description{std::move(description)},
        m_start{start},
        m_length{length},
        m_index{PMA_MAX},
        m_flags{},
        m_peek{pma_peek_error},
        m_data{pma_empty{}} {
        ;
    }

    /// \brief Default constructor creates an empty entry spanning an empty range
    /// \param description Informative description of PMA entry for use in error messages
    pma_entry(std::string description = {}) :
        m_description{std::move(description)},
        m_start{0},
        m_length{0},
        m_index{PMA_MAX},
        m_flags{},
        m_peek{pma_peek_error},
        m_data{pma_empty{}} {
        ;
    }

    /// \brief Constructor for memory entry
    /// \param description Informative description of PMA entry for use in error messages
    /// \param start Start of range.
    /// \param length Length of range.
    /// \param memory Memory PMA holding range data
    /// \param peek Function used to extract a page of data from the range
    pma_entry(std::string description, uint64_t start, uint64_t length, pma_memory &&memory,
        pma_peek peek = pma_peek_error) :
        m_description{std::move(description)},
        m_start{start},
        m_length{length},
        m_index{PMA_MAX},
        m_flags{},
        m_peek{peek},
        m_data{std::move(memory)} {
        // allocate dirty page map and mark all pages as dirty
        m_dirty_page_map.resize(length / (8 * PMA_PAGE_SIZE) + 1, 0xff);
    }

    /// \brief Constructor for device entry
    /// \param description Informative description of PMA entry for use in error messages
    /// \param start Start of range.
    /// \param length Length of range.
    /// \param device Device PMA controlling range data
    /// \param peek Function used to extract a page of data from the range
    pma_entry(std::string description, uint64_t start, uint64_t length, pma_device &&device,
        pma_peek peek = pma_peek_error) :
        m_description{std::move(description)},
        m_start{start},
        m_length{length},
        m_index{PMA_MAX},
        m_flags{},
        m_peek{peek},
        m_data{std::move(device)} {
        ;
    }

    /// \brief Set PMA entry index in target.
    void set_index(int index) {
        m_index = index;
    }

    /// \brief Returns PMA entry index in target.
    int get_index(void) const {
        return m_index;
    }

    /// \brief Set flags for lvalue references
    /// \param f New flags.
    pma_entry &set_flags(flags f) & {
        m_flags = f;
        return *this;
    }

    /// \brief Set flags for rvalue references
    /// \param f New flags.
    pma_entry &&set_flags(flags f) && {
        m_flags = f;
        return std::move(*this);
    }

    /// \brief Get flags
    /// \return Flags
    flags get_flags(void) const {
        return m_flags;
    }

    /// \brief Returns the peek callback for the range.
    pma_peek get_peek(void) const {
        return m_peek;
    }

    /// \returns data specific to E ranges
    const pma_empty &get_empty(void) const {
        return std::get<pma_empty>(m_data);
    }

    /// \returns data specific to E ranges
    pma_empty &get_empty(void) {
        return std::get<pma_empty>(m_data);
    }

    /// \returns data specific to M ranges
    const pma_memory &get_memory(void) const {
        return std::get<pma_memory>(m_data);
    }

    /// \returns data specific to M ranges
    pma_memory &get_memory(void) {
        return std::get<pma_memory>(m_data);
    }

    /// \returns data specific to IO ranges
    const pma_device &get_device(void) const {
        return std::get<pma_device>(m_data);
    }

    /// \returns data specific to IO ranges
    pma_device &get_device(void) {
        return std::get<pma_device>(m_data);
    }

    /// \brief Returns packed PMA istart field as per whitepaper
    uint64_t get_istart(void) const;

    /// \brief Returns start of physical memory range in target.
    uint64_t get_start(void) const {
        return m_start;
    }
    /// \brief Returns length of physical memory range in target.
    uint64_t get_length(void) const {
        return m_length;
    }

    /// \brief Returns encoded PMA ilength field as per whitepaper
    uint64_t get_ilength(void) const;

    /// \brief Tells if PMA is a memory range
    bool get_istart_M(void) const {
        return std::holds_alternative<pma_memory>(m_data);
    }

    /// \brief Tells if PMA is a device range
    bool get_istart_IO(void) const {
        return std::holds_alternative<pma_device>(m_data);
    }

    /// \brief Tells if PMA is an empty range
    bool get_istart_E(void) const {
        return std::holds_alternative<pma_empty>(m_data);
    }

    /// \brief Tells if PMA range is readable
    bool get_istart_R(void) const {
        return m_flags.R;
    }

    /// \brief Tells if PMA range is writable
    bool get_istart_W(void) const {
        return m_flags.W;
    }

    /// \brief Tells if PMA range is executable
    bool get_istart_X(void) const {
        return m_flags.X;
    }

    /// \brief Tells if reads to PMA range are idempotent
    bool get_istart_IR(void) const {
        return m_flags.IR;
    }

    /// \brief Tells if writes to PMA range are idempotent
    bool get_istart_IW(void) const {
        return m_flags.IW;
    }

    /// \brief Returns the id of the device that owns the range
    PMA_ISTART_DID get_istart_DID(void) const {
        return m_flags.DID;
    }

    /// \brief Mark a given page as dirty
    /// \param address_in_range Any address within page in range
    void mark_dirty_page(uint64_t address_in_range) {
        if (!m_dirty_page_map.empty()) {
            auto page_number = address_in_range >> PMA_constants::PMA_PAGE_SIZE_LOG2;
            m_dirty_page_map.at(page_number >> 3) |= (1 << (page_number & 7));
        }
    }

    /// \brief Mark a given page as clean
    /// \param address_in_range Any address within page in range
    void mark_clean_page(uint64_t address_in_range) {
        if (!m_dirty_page_map.empty()) {
            auto page_number = address_in_range >> PMA_constants::PMA_PAGE_SIZE_LOG2;
            m_dirty_page_map.at(page_number >> 3) &= ~(1 << (page_number & 7));
        }
    }

    /// \brief Checks if a given page is marked dirty
    /// \param address_in_range Any address within page in range
    /// \returns true if dirty, false if clean
    bool is_page_marked_dirty(uint64_t address_in_range) const {
        if (!m_dirty_page_map.empty()) {
            auto page_number = address_in_range >> PMA_constants::PMA_PAGE_SIZE_LOG2;
            return m_dirty_page_map.at(page_number >> 3) & (1 << (page_number & 7));
        } else {
            return true;
        }
    }

    /// \brief Marks all pages in range as clean
    void mark_pages_clean(void) {
        return std::fill(m_dirty_page_map.begin(), m_dirty_page_map.end(), 0);
    }

    /// \brief Returns PMA description as a string
    /// \returns Description
    const std::string &get_description(void) const {
        return m_description;
    }
};

/// \brief Creates a PMA entry for a new memory range initially filled with zeros.
/// \param description Informative description of PMA entry for use in error messages
/// \param start Start of PMA range.
/// \param length Length of PMA range.
/// \returns Corresponding PMA entry
pma_entry make_callocd_memory_pma_entry(const std::string &description, uint64_t start, uint64_t length);

/// \brief Creates a PMA entry for a new memory range initially filled with the contents of a backing file.
/// \param description Informative description of PMA entry for use in error messages
/// \param start Start of PMA range.
/// \param length Length of PMA range.
/// \param path Path to backing file.
/// \returns Corresponding PMA entry
pma_entry make_callocd_memory_pma_entry(const std::string &description, uint64_t start, uint64_t length,
    const std::string &path);

/// \brief Creates a PMA entry for a new memory region using the host's
/// mmap functionality.
/// \param description Informative description of PMA entry for use in error messages
/// \param start Start of physical memory range in the target address
/// space on which to map the memory region.
/// \param length Length of physical memory range in the
/// target address space on which to map the memory region.
/// \param path Reference to a string containing the filename
/// for the backing file in the host with the contents of the memory region.
/// \param shared Whether target modifications to the memory region are
/// reflected in the host's backing file.
/// \returns Corresponding PMA entry
/// \details \p length must match the size of the backing file.
/// This function is typically used to map flash drives.
pma_entry make_mmapd_memory_pma_entry(const std::string &description, uint64_t start, uint64_t length,
    const std::string &path, bool shared);

/// \brief Creates a PMA entry for a new mock memory region (no allocation).
/// \param description Informative description of PMA entry for use in error messages
/// \param start Start of physical memory range in the target address
/// space on which to map the memory region.
/// \param length Length of physical memory range in the
/// target address space on which to map the memory region.
/// \returns Corresponding PMA entry
pma_entry make_mockd_memory_pma_entry(const std::string &description, uint64_t start, uint64_t length);

/// \brief Creates a PMA entry for a new memory-mapped IO device.
/// \param description Informative description of PMA entry for use in error messages
/// \param start Start of physical memory range in the target address
/// space on which to map the device.
/// \param length Length of physical memory range in the
/// target address space on which to map the device.
/// \param peek Peek callback for the range.
/// \param driver Pointer to driver with callbacks.
/// \param context Pointer to context to be passed to callbacks.
/// \returns Corresponding PMA entry
pma_entry make_device_pma_entry(const std::string &description, uint64_t start, uint64_t length, pma_peek peek,
    const pma_driver *driver, void *context = nullptr);

/// \brief Creates an empty PMA entry.
/// \param description Informative description of PMA entry for use in error messages
/// \param start Start of physical memory range in the target address
/// space on which to map the device.
/// \param length Length of physical memory range in the
/// target address space on which to map the device.
/// \returns Corresponding PMA entry
pma_entry make_empty_pma_entry(const std::string &description, uint64_t start, uint64_t length);

} // namespace cartesi

#endif
