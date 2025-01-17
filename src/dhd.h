// Copyright 2020 Cartesi Pte. Ltd.
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

#ifndef DHD_H
#define DHD_H

#include <cstdint>
#include <vector>

#include "pma.h"

/// \file
/// \brief The dehash device.

namespace cartesi {

using dhd_data = std::vector<unsigned char>;

/// \brief Mapping between CSRs and their relative addresses in DHD memory
enum class dhd_csr {
    reserved = UINT64_C(0x0),
    tstart = UINT64_C(0x8),
    tlength = UINT64_C(0x10),
    dlength = UINT64_C(0x18),
    hlength = UINT64_C(0x20),
    h0 = UINT64_C(0x28)
};

/// \brief DHD constants
enum dhd_constants : uint64_t { DHD_NOT_FOUND = UINT64_C(-1) };

/// \brief Obtains the relative address of a CSR in DHD memory.
/// \param reg CSR name.
/// \returns The address.
uint64_t dhd_get_csr_rel_addr(dhd_csr reg);

/// \brief Obtains the relative address of an h register in DHD memory.
/// \param i Register index. Between 0 and DHD_H_REG_COUNT-1, inclusive.
/// \returns The address.
uint64_t dhd_get_h_rel_addr(int i);

/// \brief Creates a PMA entry for the DHD device
/// \param start Start address for memory range.
/// \param length Length of memory range.
/// \returns Corresponding PMA entry
pma_entry make_dhd_pma_entry(uint64_t start, uint64_t length);

} // namespace cartesi

#endif
