#include "virtio-mmio-device.h"

namespace cartesi {

virtio_mmio_device::virtio_mmio_device(uint32_t virtio_idx, machine* m)
    : m_virtio_idx(virtio_idx)
    , m_machine(m) {
}

bool virtio_mmio_device::mmio_read(i_device_state_access* da, uint64_t offset, uint32_t* pval, int log2_size) {
    if (m_machine != nullptr) {
        uint8_t data[8] = {0}; // Buffer for read data
        int result = m_machine->call_mmio_callback(m_virtio_idx, offset, data, 1 << log2_size, false);
        if (result == 0) {
            // Copy data based on size
            switch (log2_size) {
                case 0: *pval = data[0]; break;
                case 1: *pval = *reinterpret_cast<uint16_t*>(data); break;
                case 2: *pval = *reinterpret_cast<uint32_t*>(data); break;
                case 3: *pval = *reinterpret_cast<uint64_t*>(data); break;
                default: return false;
            }
            return true;
        }
    }
    return false;
}

execute_status virtio_mmio_device::mmio_write(i_device_state_access* da, uint64_t offset, uint64_t val, int log2_size) {
    if (m_machine != nullptr) {
        uint8_t data[8] = {0}; // Buffer for write data
        // Copy data based on size
        switch (log2_size) {
            case 0: data[0] = val; break;
            case 1: *reinterpret_cast<uint16_t*>(data) = val; break;
            case 2: *reinterpret_cast<uint32_t*>(data) = val; break;
            case 3: *reinterpret_cast<uint64_t*>(data) = val; break;
            default: return execute_status::failure;
        }
        int result = m_machine->call_mmio_callback(m_virtio_idx, offset, data, 1 << log2_size, true);
        return (result == 0) ? execute_status::success : execute_status::failure;
    }
    return execute_status::failure;
}

} // namespace cartesi 