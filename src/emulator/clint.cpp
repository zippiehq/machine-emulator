#include "clint.h"
#include "i-device-state-access.h"
#include "machine.h"
#include "machine-state.h"
#include "rtc.h"
#include "riscv-constants.h"

#define CLINT_MSIP0    0
#define CLINT_MTIMECMP 0x4000
#define CLINT_MTIME    0xbff8

static bool clint_read_msip(i_device_state_access *a, uint64_t *val,
    int size_log2) {
    if (size_log2 == 2) {
        *val = ((a->read_mip() & MIP_MSIP) == MIP_MSIP);
        return true;
    }
    return false;
}

static bool clint_read_mtime(i_device_state_access *a, uint64_t *val, int size_log2) {
    if (size_log2 == 3) {
        *val = rtc_cycle_to_time(a->read_mcycle());
        return true;
    }
    return false;
}

static bool clint_read_mtimecmp(i_device_state_access *a, uint64_t *val, int size_log2) {
    if (size_log2 == 3) {
        *val = a->read_mtimecmp();
        return true;
    }
    return false;
}

/// \brief CLINT device read callback. See ::pma_device_read.
static bool clint_read(i_device_state_access *a, void *context, uint64_t offset, uint64_t *val, int size_log2) {
    (void) context;

    switch (offset) {
        case CLINT_MSIP0:    // Machine software interrupt for hart 0
            return clint_read_msip(a, val, size_log2);
        case CLINT_MTIMECMP: // mtimecmp
            return clint_read_mtimecmp(a, val, size_log2);
        case CLINT_MTIME:    // mtime
            return clint_read_mtime(a, val, size_log2);
        default:
            // other reads are exceptions
            return false;
    }
}

/// \brief CLINT device read callback. See ::pma_device_write.
static bool clint_write(i_device_state_access *a, void *context, uint64_t offset, uint64_t val, int size_log2) {
    (void) context;

    switch (offset) {
        case CLINT_MSIP0: // Machine software interrupt for hart 0
            if (size_log2 == 2) {
                //??D I don't yet know why Linux tries to raise MSIP when we only have a single hart
                //    It does so repeatedly before and after every command run in the shell
                //    Will investigate.
                if (val & 1) {
                    a->set_mip(MIP_MSIP);
                } else {
                    a->reset_mip(MIP_MSIP);
                }
                return true;
            }
            return false;
        case CLINT_MTIMECMP: // mtimecmp
            if (size_log2 == 3) {
                a->write_mtimecmp(val);
                a->reset_mip(MIP_MTIP);
                return true;
            }
            // partial mtimecmp is not supported
            return false;
        default:
            // other writes are exceptions
            return false;
    }
}

#define base(v) ((v) - ((v) % (PMA_PAGE_SIZE)))
#define offset(v) ((v) % (PMA_PAGE_SIZE))
/// \brief CLINT device peek callback. See ::pma_device_peek.
static device_peek_status clint_peek(const machine_state *s, void *context, uint64_t page_index, uint8_t *page_data) {
    (void) context;
    static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
        "code assumes little-endian byte ordering");
    // There are 3 non-pristine pages: base(CLINT_MSIP0), base(CLINT_MTIMECMP), and base(CLINT_MTIME)
    switch (page_index) {
        case base(CLINT_MSIP0):
            // This page contains only msip (which is either 0 or 1)
            // Since we are little-endian, we can simply write the byte
            memset(page_data, 0, PMA_PAGE_SIZE);
            *reinterpret_cast<uint64_t *>(page_data + offset(CLINT_MSIP0)) = ((s->mip & MIP_MSIP) == MIP_MSIP);
            return device_peek_status::success;
        case base(CLINT_MTIMECMP):
            memset(page_data, 0, PMA_PAGE_SIZE);
            *reinterpret_cast<uint64_t *>(page_data + offset(CLINT_MTIMECMP)) = s->mtimecmp;
            return device_peek_status::success;
        case base(CLINT_MTIME):
            memset(page_data, 0, PMA_PAGE_SIZE);
            *reinterpret_cast<uint64_t*>(page_data + offset(CLINT_MTIME)) = rtc_cycle_to_time(s->mcycle);
            return device_peek_status::success;
        default:
            if (page_index % PMA_PAGE_SIZE == 0)
                return device_peek_status::pristine_page;
            else
                return device_peek_status::invalid_page;
    }
}
#undef base
#undef offset

const pma_device_driver clint_driver = {
    "CLINT",
    clint_read,
    clint_write,
    clint_peek
};