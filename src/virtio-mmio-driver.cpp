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

#include "virtio-mmio-driver.h"
#include "virtio-mmio-device.h"
#include "pma-driver.h"
#include <cassert>

namespace cartesi {

static bool virtio_mmio_driver_read(void *context, i_device_state_access *da, uint64_t offset, uint64_t *val, int log2_size) {
    auto *device = static_cast<virtio_mmio_device *>(context);
    assert(device != nullptr);
    uint32_t val32 = 0;
    bool success = device->mmio_read(da, offset, &val32, log2_size);
    if (success) {
        *val = val32;
    }
    return success;
}

static execute_status virtio_mmio_driver_write(void *context, i_device_state_access *da, uint64_t offset, uint64_t val, int log2_size) {
    auto *device = static_cast<virtio_mmio_device *>(context);
    assert(device != nullptr);
    return device->mmio_write(da, offset, val, log2_size);
}

const pma_driver virtio_mmio_driver{"virtio-mmio", virtio_mmio_driver_read, virtio_mmio_driver_write};

} // namespace cartesi 