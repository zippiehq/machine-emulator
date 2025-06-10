#pragma once

#include <cstdint>
#include "i-device-state-access.h"

namespace cartesi {

class mmio_device {
public:
    mmio_device() = default;
    virtual ~mmio_device() = default;

    mmio_device(const mmio_device &) = delete;
    mmio_device(mmio_device &&) = delete;
    mmio_device &operator=(const mmio_device &) = delete;
    mmio_device &operator=(mmio_device &&) = delete;

    virtual bool mmio_read(i_device_state_access* da, uint64_t offset, uint32_t* pval, int log2_size) = 0;
    virtual execute_status mmio_write(i_device_state_access* da, uint64_t offset, uint64_t val, int log2_size) = 0;
    virtual uint32_t get_device_index() const = 0;
};

} // namespace cartesi 