#pragma once

#include <cstdint>
#include <memory>
#include "machine.h"
#include "i-device-state-access.h"
#include "mmio-device.h"

namespace cartesi {

class virtio_mmio_device : public mmio_device {
public:
    virtio_mmio_device(uint32_t virtio_idx, machine* m);
    ~virtio_mmio_device() override = default;

    virtio_mmio_device(const virtio_mmio_device &) = delete;
    virtio_mmio_device(virtio_mmio_device &&) = delete;
    virtio_mmio_device &operator=(const virtio_mmio_device &) = delete;
    virtio_mmio_device &operator=(virtio_mmio_device &&) = delete;

    bool mmio_read(i_device_state_access* da, uint64_t offset, uint32_t* pval, int log2_size) override;
    execute_status mmio_write(i_device_state_access* da, uint64_t offset, uint64_t val, int log2_size) override;
    uint32_t get_device_index() const override { return m_virtio_idx; }

private:
    uint32_t m_virtio_idx;
    machine* m_machine = nullptr;
};

} // namespace cartesi 