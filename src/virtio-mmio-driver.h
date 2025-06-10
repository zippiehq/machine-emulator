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

#ifndef VIRTIO_MMIO_DRIVER_H
#define VIRTIO_MMIO_DRIVER_H

#pragma once

#include "pma-driver.h"
#include <functional>

namespace cartesi {

// Callback type for virtio-mmio access
using virtio_mmio_callback = std::function<void(uint32_t device_index, uint64_t offset, uint64_t value, bool is_write)>;

class virtio_mmio_driver {
public:
    virtio_mmio_driver(virtio_mmio_callback callback);
    
    // PMA driver callbacks
    static bool read(void* context, i_device_state_access* da, uint64_t offset, uint64_t* val, int log2_size);
    static execute_status write(void* context, i_device_state_access* da, uint64_t offset, uint64_t val, int log2_size);

    // Get the PMA driver instance
    static const pma_driver* get_driver() { return &m_driver; }

private:
    // Decode device index and offset from the PMA address
    static void decode_address(uint64_t pma_addr, uint32_t& device_index, uint64_t& offset);
    
    virtio_mmio_callback m_callback;
    static const pma_driver m_driver;
};

extern const pma_driver virtio_mmio_driver;

} // namespace cartesi

#endif // VIRTIO_MMIO_DRIVER_H 