/*
 * RISCV CPU emulator
 *
 * Copyright (c) 2016-2017 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <fcntl.h>

#define XLEN 64
#define MXL   2

#include "cutils.h"
#include "iomem.h"
#include "riscv_cpu.h"


//#define DUMP_INVALID_MEM_ACCESS
//#define DUMP_MMU_EXCEPTIONS
//#define DUMP_INTERRUPTS
//#define DUMP_INVALID_CSR
//#define DUMP_EXCEPTIONS
//#define DUMP_CSR

#define __exception __attribute__((warn_unused_result))

typedef uint64_t target_ulong;
typedef int64_t target_long;
#define PR_target_ulong "016" PRIx64

typedef uint64_t mem_uint_t;

#define TLB_SIZE 256

#define CAUSE_MISALIGNED_FETCH    0x0
#define CAUSE_FAULT_FETCH         0x1
#define CAUSE_ILLEGAL_INSTRUCTION 0x2
#define CAUSE_BREAKPOINT          0x3
#define CAUSE_MISALIGNED_LOAD     0x4
#define CAUSE_FAULT_LOAD          0x5
#define CAUSE_MISALIGNED_STORE    0x6
#define CAUSE_FAULT_STORE         0x7
#define CAUSE_USER_ECALL          0x8
#define CAUSE_SUPERVISOR_ECALL    0x9
#define CAUSE_HYPERVISOR_ECALL    0xa
#define CAUSE_MACHINE_ECALL       0xb
#define CAUSE_FETCH_PAGE_FAULT    0xc
#define CAUSE_LOAD_PAGE_FAULT     0xd
#define CAUSE_STORE_PAGE_FAULT    0xf

/* Note: converted to correct bit position at runtime */
#define CAUSE_INTERRUPT  ((uint32_t)1 << 31)

#define PRV_U 0
#define PRV_S 1
#define PRV_H 2
#define PRV_M 3

/* misa CSR */
#define MCPUID_SUPER   (1 << ('S' - 'A'))
#define MCPUID_USER    (1 << ('U' - 'A'))
#define MCPUID_I       (1 << ('I' - 'A'))
#define MCPUID_M       (1 << ('M' - 'A'))
#define MCPUID_A       (1 << ('A' - 'A'))
#define MCPUID_F       (1 << ('F' - 'A'))
#define MCPUID_D       (1 << ('D' - 'A'))
#define MCPUID_Q       (1 << ('Q' - 'A'))
#define MCPUID_C       (1 << ('C' - 'A'))

/* mstatus CSR */

#define MSTATUS_UIE_SHIFT 0
#define MSTATUS_SIE_SHIFT 1
#define MSTATUS_HIE_SHIFT 2
#define MSTATUS_MIE_SHIFT 3
#define MSTATUS_UPIE_SHIFT 4
#define MSTATUS_SPIE_SHIFT 5
#define MSTATUS_MPIE_SHIFT 7
#define MSTATUS_SPP_SHIFT 8
#define MSTATUS_MPP_SHIFT 11
#define MSTATUS_FS_SHIFT 13
#define MSTATUS_UXL_SHIFT 32
#define MSTATUS_SXL_SHIFT 34

#define MSTATUS_UIE (1 << 0)
#define MSTATUS_SIE (1 << 1)
#define MSTATUS_HIE (1 << 2)
#define MSTATUS_MIE (1 << 3)
#define MSTATUS_UPIE (1 << 4)
#define MSTATUS_SPIE (1 << MSTATUS_SPIE_SHIFT)
#define MSTATUS_HPIE (1 << 6)
#define MSTATUS_MPIE (1 << MSTATUS_MPIE_SHIFT)
#define MSTATUS_SPP (1 << MSTATUS_SPP_SHIFT)
#define MSTATUS_HPP (3 << 9)
#define MSTATUS_MPP (3 << MSTATUS_MPP_SHIFT)
#define MSTATUS_FS (3 << MSTATUS_FS_SHIFT)
#define MSTATUS_XS (3 << 15)
#define MSTATUS_MPRV (1 << 17)
#define MSTATUS_SUM (1 << 18)
#define MSTATUS_MXR (1 << 19)
#define MSTATUS_TVM (1 << 20)
#define MSTATUS_TW (1 << 21)
#define MSTATUS_TSR (1 << 22)
#define MSTATUS_UXL ((uint64_t)3 << MSTATUS_UXL_SHIFT)
#define MSTATUS_SXL ((uint64_t)3 << MSTATUS_SXL_SHIFT)

#define PG_SHIFT 12
#define PG_MASK ((1 << PG_SHIFT) - 1)

typedef struct {
    target_ulong vaddr;
    uintptr_t mem_addend;
} TLBEntry;

struct RISCVCPUState {
    target_ulong pc;
    target_ulong reg[32];

    uint8_t priv; /* see PRV_x */

    uint64_t insn_counter;
    uint64_t cycle_counter;
    BOOL power_down_flag;
    BOOL shuthost_flag;
    int pending_exception; /* used during MMU exception handling */
    target_ulong pending_tval;

    /* CSRs */
    target_ulong mstatus;
    target_ulong mtvec;
    target_ulong mscratch;
    target_ulong mepc;
    target_ulong mcause;
    target_ulong mtval;
    target_ulong mhartid; /* ro */
    target_ulong misa;

    uint32_t mie;
    uint32_t mip;
    uint32_t medeleg;
    uint32_t mideleg;
    uint32_t mcounteren;

    target_ulong stvec;
    target_ulong sscratch;
    target_ulong sepc;
    target_ulong scause;
    target_ulong stval;
    uint64_t satp; /* currently 64 bit physical addresses max */
    uint32_t scounteren;

    target_ulong load_res; /* for atomic LR/SC */

    PhysMemoryMap *mem_map;

    TLBEntry tlb_read[TLB_SIZE];
    TLBEntry tlb_write[TLB_SIZE];
    TLBEntry tlb_code[TLB_SIZE];
};

static no_inline int target_read_slow(RISCVCPUState *s, mem_uint_t *pval,
                                      target_ulong addr, int size_log2);
static no_inline int target_write_slow(RISCVCPUState *s, target_ulong addr,
                                       mem_uint_t val, int size_log2);

static void fprint_target_ulong(FILE *f, target_ulong a)
{
    fprintf(f, "%" PR_target_ulong, a);
}

static void print_target_ulong(target_ulong a)
{
    fprint_target_ulong(stderr, a);
}

static char *reg_name[32] = {
"zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
"s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
"a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
"s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

void dump_regs(RISCVCPUState *s)
{
    int i, cols;
    const char priv_str[4] = "USHM";
    cols = 256 / XLEN;
    fprintf(stderr, "pc = ");
    print_target_ulong(s->pc);
    fprintf(stderr, " ");
    for(i = 1; i < 32; i++) {
        fprintf(stderr, "%-3s= ", reg_name[i]);
        print_target_ulong(s->reg[i]);
        if ((i & (cols - 1)) == (cols - 1))
            fprintf(stderr, "\n");
        else
            fprintf(stderr, " ");
    }
    fprintf(stderr, "priv=%c", priv_str[s->priv]);
    fprintf(stderr, " mstatus=");
    print_target_ulong(s->mstatus);
    fprintf(stderr, " cycles=%" PRId64, s->cycle_counter);
    fprintf(stderr, " insns=%" PRId64, s->insn_counter);
    fprintf(stderr, "\n");
#if 1
    fprintf(stderr, "mideleg=");
    print_target_ulong(s->mideleg);
    fprintf(stderr, " mie=");
    print_target_ulong(s->mie);
    fprintf(stderr, " mip=");
    print_target_ulong(s->mip);
    fprintf(stderr, "\n");
#endif
}

/* addr must be aligned. Only RAM accesses are supported */
#define PHYS_MEM_READ_WRITE(size, uint_type) \
static __maybe_unused inline void phys_write_u ## size(RISCVCPUState *s, target_ulong addr,\
                                        uint_type val)                   \
{\
    PhysMemoryRange *pr = get_phys_mem_range(s->mem_map, addr);\
    if (!pr || !pr->is_ram)\
        return;\
    *(uint_type *)(pr->phys_mem + \
                 (uintptr_t)(addr - pr->addr)) = val;\
}\
\
static __maybe_unused inline uint_type phys_read_u ## size(RISCVCPUState *s, target_ulong addr) \
{\
    PhysMemoryRange *pr = get_phys_mem_range(s->mem_map, addr);\
    if (!pr || !pr->is_ram)\
        return 0;\
    return *(uint_type *)(pr->phys_mem + \
                          (uintptr_t)(addr - pr->addr));     \
}

PHYS_MEM_READ_WRITE(8, uint8_t)
PHYS_MEM_READ_WRITE(32, uint32_t)
PHYS_MEM_READ_WRITE(64, uint64_t)

/* return 0 if OK, != 0 if exception */
#define TARGET_READ_WRITE(size, uint_type, size_log2)                   \
static inline __exception int target_read_u ## size(RISCVCPUState *s, uint_type *pval, target_ulong addr)                              \
{\
    uint32_t tlb_idx;\
    tlb_idx = (addr >> PG_SHIFT) & (TLB_SIZE - 1);\
    if (likely(s->tlb_read[tlb_idx].vaddr == (addr & ~(PG_MASK & ~((size / 8) - 1))))) { \
        *pval = *(uint_type *)(s->tlb_read[tlb_idx].mem_addend + (uintptr_t)addr);\
    } else {\
        mem_uint_t val;\
        int ret;\
        ret = target_read_slow(s, &val, addr, size_log2);\
        if (ret)\
            return ret;\
        *pval = val;\
    }\
    return 0;\
}\
\
static inline __exception int target_write_u ## size(RISCVCPUState *s, target_ulong addr,\
                                          uint_type val)                \
{\
    uint32_t tlb_idx;\
    tlb_idx = (addr >> PG_SHIFT) & (TLB_SIZE - 1);\
    if (likely(s->tlb_write[tlb_idx].vaddr == (addr & ~(PG_MASK & ~((size / 8) - 1))))) { \
        *(uint_type *)(s->tlb_write[tlb_idx].mem_addend + (uintptr_t)addr) = val;\
        return 0;\
    } else {\
        return target_write_slow(s, addr, val, size_log2);\
    }\
}

TARGET_READ_WRITE(8, uint8_t, 0)
TARGET_READ_WRITE(16, uint16_t, 1)
TARGET_READ_WRITE(32, uint32_t, 2)
TARGET_READ_WRITE(64, uint64_t, 3)


static inline int32_t div32(int32_t a, int32_t b)
{
    if (b == 0) {
        return -1;
    } else if (a == ((int32_t)1 << (32 - 1)) && b == -1) {
        return a;
    } else {
        return a / b;
    }
}

static inline uint32_t divu32(uint32_t a, uint32_t b)
{
    if (b == 0) {
        return -1;
    } else {
        return a / b;
    }
}

static inline int32_t rem32(int32_t a, int32_t b)
{
    if (b == 0) {
        return a;
    } else if (a == ((int32_t)1 << (32 - 1)) && b == -1) {
        return 0;
    } else {
        return a % b;
    }
}

static inline uint32_t remu32(uint32_t a, uint32_t b)
{
    if (b == 0) {
        return a;
    } else {
        return a % b;
    }
}

#define PTE_V_MASK (1 << 0)
#define PTE_U_MASK (1 << 4)
#define PTE_A_MASK (1 << 6)
#define PTE_D_MASK (1 << 7)

#define ACCESS_READ  0
#define ACCESS_WRITE 1
#define ACCESS_CODE  2

/* access = 0: read, 1 = write, 2 = code. Set the exception_pending
   field if necessary. return 0 if OK, -1 if translation error */
static int get_phys_addr(RISCVCPUState *s,
                         target_ulong *ppaddr, target_ulong vaddr,
                         int access)
{
    int mode, levels, pte_bits, pte_idx, pte_mask, pte_size_log2, xwr, priv;
    int need_write, vaddr_shift, i, pte_addr_bits;
    target_ulong pte_addr, pte, vaddr_mask, paddr;

    if ((s->mstatus & MSTATUS_MPRV) && access != ACCESS_CODE) {
        /* use previous priviledge */
        priv = (s->mstatus >> MSTATUS_MPP_SHIFT) & 3;
    } else {
        priv = s->priv;
    }

    if (priv == PRV_M) {
        *ppaddr = vaddr;
        return 0;
    }
    mode = (s->satp >> 60) & 0xf;
    /* bare: no translation */
    if (mode == 0) {
        *ppaddr = vaddr;
        return 0;
    }
    /* sv39/sv48 */
    levels = mode - 8 + 3;
    pte_size_log2 = 3;
    vaddr_shift = XLEN - (PG_SHIFT + levels * 9);
    if ((((target_long)vaddr << vaddr_shift) >> vaddr_shift) != (target_long) vaddr)
        return -1;
    pte_addr_bits = 44;
    pte_addr = (s->satp & (((target_ulong)1 << pte_addr_bits) - 1)) << PG_SHIFT;
    pte_bits = 12 - pte_size_log2;
    pte_mask = (1 << pte_bits) - 1;
    for(i = 0; i < levels; i++) {
        vaddr_shift = PG_SHIFT + pte_bits * (levels - 1 - i);
        pte_idx = (vaddr >> vaddr_shift) & pte_mask;
        pte_addr += pte_idx << pte_size_log2;
        if (pte_size_log2 == 2)
            pte = phys_read_u32(s, pte_addr);
        else
            pte = phys_read_u64(s, pte_addr);
        if (!(pte & PTE_V_MASK))
            return -1; /* invalid PTE */
        paddr = (pte >> 10) << PG_SHIFT;
        xwr = (pte >> 1) & 7;
        if (xwr != 0) {
            if (xwr == 2 || xwr == 6)
                return -1;
            /* priviledge check */
            if (priv == PRV_S) {
                if ((pte & PTE_U_MASK) && !(s->mstatus & MSTATUS_SUM))
                    return -1;
            } else {
                if (!(pte & PTE_U_MASK))
                    return -1;
            }
            /* protection check */
            /* MXR allows read access to execute-only pages */
            if (s->mstatus & MSTATUS_MXR)
                xwr |= (xwr >> 2);

            if (((xwr >> access) & 1) == 0)
                return -1;
            vaddr_mask = ((target_ulong)1 << vaddr_shift) - 1;
            if (paddr  & vaddr_mask) /* alignment check */
                return -1;
            need_write = !(pte & PTE_A_MASK) ||
                (!(pte & PTE_D_MASK) && access == ACCESS_WRITE);
            pte |= PTE_A_MASK;
            if (access == ACCESS_WRITE)
                pte |= PTE_D_MASK;
            if (need_write) {
                if (pte_size_log2 == 2)
                    phys_write_u32(s, pte_addr, pte);
                else
                    phys_write_u64(s, pte_addr, pte);
            }
            *ppaddr = (vaddr & vaddr_mask) | (paddr  & ~vaddr_mask);
            return 0;
        } else {
            pte_addr = paddr;
        }
    }
    return -1;
}

/* return 0 if OK, != 0 if exception */
static no_inline int target_read_slow(RISCVCPUState *s, mem_uint_t *pval,
                                      target_ulong addr, int size_log2)
{
    int size, tlb_idx, err, al;
    target_ulong paddr, offset;
    uint8_t *ptr;
    PhysMemoryRange *pr;
    mem_uint_t ret;

    /* first handle unaligned accesses */
    size = 1 << size_log2;
    al = addr & (size - 1);
    if (al != 0) {
        switch(size_log2) {
        case 1:
            {
                uint8_t v0, v1;
                err = target_read_u8(s, &v0, addr);
                if (err)
                    return err;
                err = target_read_u8(s, &v1, addr + 1);
                if (err)
                    return err;
                ret = v0 | (v1 << 8);
            }
            break;
        case 2:
            {
                uint32_t v0, v1;
                addr -= al;
                err = target_read_u32(s, &v0, addr);
                if (err)
                    return err;
                err = target_read_u32(s, &v1, addr + 4);
                if (err)
                    return err;
                ret = (v0 >> (al * 8)) | (v1 << (32 - al * 8));
            }
            break;
        case 3:
            {
                uint64_t v0, v1;
                addr -= al;
                err = target_read_u64(s, &v0, addr);
                if (err)
                    return err;
                err = target_read_u64(s, &v1, addr + 8);
                if (err)
                    return err;
                ret = (v0 >> (al * 8)) | (v1 << (64 - al * 8));
            }
            break;
        default:
            abort();
        }
    } else {
        if (get_phys_addr(s, &paddr, addr, ACCESS_READ)) {
            s->pending_tval = addr;
            s->pending_exception = CAUSE_LOAD_PAGE_FAULT;
            return -1;
        }
        pr = get_phys_mem_range(s->mem_map, paddr);
        if (!pr) {
#ifdef DUMP_INVALID_MEM_ACCESS
            fprintf(stderr, "target_read_slow: invalid physical address 0x");
            print_target_ulong(paddr);
            fprintf(stderr, "\n");
#endif
            s->pending_tval = addr;
            s->pending_exception = CAUSE_FAULT_LOAD;
            return -1;
        } else if (pr->is_ram) {
            tlb_idx = (addr >> PG_SHIFT) & (TLB_SIZE - 1);
            ptr = pr->phys_mem + (uintptr_t)(paddr - pr->addr);
            s->tlb_read[tlb_idx].vaddr = addr & ~PG_MASK;
            s->tlb_read[tlb_idx].mem_addend = (uintptr_t)ptr - addr;
            switch(size_log2) {
            case 0:
                ret = *(uint8_t *)ptr;
                break;
            case 1:
                ret = *(uint16_t *)ptr;
                break;
            case 2:
                ret = *(uint32_t *)ptr;
                break;
            case 3:
                ret = *(uint64_t *)ptr;
                break;
            default:
                abort();
            }
        } else {
            offset = paddr - pr->addr;
            if (((pr->devio_flags >> size_log2) & 1) != 0) {
                ret = pr->read_func(pr->opaque, offset, size_log2);
            }
            else if ((pr->devio_flags & DEVIO_SIZE32) && size_log2 == 3) {
                /* emulate 64 bit access */
                ret = pr->read_func(pr->opaque, offset, 2);
                ret |= (uint64_t)pr->read_func(pr->opaque, offset + 4, 2) << 32;

            }
            else {
#ifdef DUMP_INVALID_MEM_ACCESS
                fprintf(stderr, "unsupported device read access: addr=0x");
                print_target_ulong(paddr);
                fprintf(stderr, " width=%d bits\n", 1 << (3 + size_log2));
#endif
                ret = 0;
            }
        }
    }
    *pval = ret;
    return 0;
}

/* return 0 if OK, != 0 if exception */
static no_inline int target_write_slow(RISCVCPUState *s, target_ulong addr,
                                       mem_uint_t val, int size_log2)
{
    int size, i, tlb_idx, err;
    target_ulong paddr, offset;
    uint8_t *ptr;
    PhysMemoryRange *pr;

    /* first handle unaligned accesses */
    size = 1 << size_log2;
    if ((addr & (size - 1)) != 0) {
        /* XXX: should avoid modifying the memory in case of exception */
        for(i = 0; i < size; i++) {
            err = target_write_u8(s, addr + i, (val >> (8 * i)) & 0xff);
            if (err)
                return err;
        }
    } else {
        if (get_phys_addr(s, &paddr, addr, ACCESS_WRITE)) {
            s->pending_tval = addr;
            s->pending_exception = CAUSE_STORE_PAGE_FAULT;
            return -1;
        }
        pr = get_phys_mem_range(s->mem_map, paddr);
        if (!pr) {
            /*??DD should raise exception here */
#ifdef DUMP_INVALID_MEM_ACCESS
            fprintf(stderr, "target_write_slow: invalid physical address 0x");
            print_target_ulong(paddr);
            fprintf(stderr, "\n");
#endif
        } else if (pr->is_ram) {
            phys_mem_set_dirty_bit(pr, paddr - pr->addr);
            tlb_idx = (addr >> PG_SHIFT) & (TLB_SIZE - 1);
            ptr = pr->phys_mem + (uintptr_t)(paddr - pr->addr);
            s->tlb_write[tlb_idx].vaddr = addr & ~PG_MASK;
            s->tlb_write[tlb_idx].mem_addend = (uintptr_t)ptr - addr;
            switch(size_log2) {
            case 0:
                *(uint8_t *)ptr = val;
                break;
            case 1:
                *(uint16_t *)ptr = val;
                break;
            case 2:
                *(uint32_t *)ptr = val;
                break;
            case 3:
                *(uint64_t *)ptr = val;
                break;
            default:
                abort();
            }
        } else {
            offset = paddr - pr->addr;
            if (((pr->devio_flags >> size_log2) & 1) != 0) {
                pr->write_func(pr->opaque, offset, val, size_log2);
            }
            else if ((pr->devio_flags & DEVIO_SIZE32) && size_log2 == 3) {
                /* emulate 64 bit access */
                pr->write_func(pr->opaque, offset,
                               val & 0xffffffff, 2);
                pr->write_func(pr->opaque, offset + 4,
                               (val >> 32) & 0xffffffff, 2);
            }
            else {
#ifdef DUMP_INVALID_MEM_ACCESS
                fprintf(stderr, "unsupported device write access: addr=0x");
                print_target_ulong(paddr);
                fprintf(stderr, " width=%d bits\n", 1 << (3 + size_log2));
#endif
            }
        }
    }
    return 0;
}

struct __attribute__((packed)) unaligned_u32 {
    uint32_t u32;
};

/* unaligned access at an address known to be a multiple of 2 */
static uint32_t get_insn32(uint8_t *ptr)
{
#if defined(EMSCRIPTEN)
    return ((uint16_t *)ptr)[0] | (((uint16_t *)ptr)[1] << 16);
#else
    return ((struct unaligned_u32 *)ptr)->u32;
#endif
}

/* return 0 if OK, != 0 if exception */
static no_inline __exception int target_read_insn_slow(RISCVCPUState *s,
                                                       uintptr_t *pmem_addend,
                                                       target_ulong addr)
{
    int tlb_idx;
    target_ulong paddr;
    uint8_t *ptr;
    PhysMemoryRange *pr;

    if (get_phys_addr(s, &paddr, addr, ACCESS_CODE)) {
        s->pending_tval = addr;
        s->pending_exception = CAUSE_FETCH_PAGE_FAULT;
        return -1;
    }
    pr = get_phys_mem_range(s->mem_map, paddr);
    if (!pr || !pr->is_ram) {
        /* XXX: we only access to execute code from RAM */
        s->pending_tval = addr;
        s->pending_exception = CAUSE_FAULT_FETCH;
        return -1;
    }
    tlb_idx = (addr >> PG_SHIFT) & (TLB_SIZE - 1);
    ptr = pr->phys_mem + (uintptr_t)(paddr - pr->addr);
    s->tlb_code[tlb_idx].vaddr = addr & ~PG_MASK;
    s->tlb_code[tlb_idx].mem_addend = (uintptr_t)ptr - addr;
    *pmem_addend = s->tlb_code[tlb_idx].mem_addend;
    return 0;
}

/* addr must be aligned */
static inline __exception int target_read_insn_u16(RISCVCPUState *s, uint16_t *pinsn,
                                                   target_ulong addr)
{
    uint32_t tlb_idx;
    uintptr_t mem_addend;

    tlb_idx = (addr >> PG_SHIFT) & (TLB_SIZE - 1);
    if (likely(s->tlb_code[tlb_idx].vaddr == (addr & ~PG_MASK))) {
        mem_addend = s->tlb_code[tlb_idx].mem_addend;
    } else {
        if (target_read_insn_slow(s, &mem_addend, addr))
            return -1;
    }
    *pinsn = *(uint16_t *)(mem_addend + (uintptr_t)addr);
    return 0;
}

static void tlb_init(RISCVCPUState *s)
{
    int i;

    for(i = 0; i < TLB_SIZE; i++) {
        s->tlb_read[i].vaddr = -1;
        s->tlb_write[i].vaddr = -1;
        s->tlb_code[i].vaddr = -1;
    }
}

static void tlb_flush_all(RISCVCPUState *s)
{
    tlb_init(s);
}

static void tlb_flush_vaddr(RISCVCPUState *s, target_ulong vaddr)
{
    (void) vaddr;
    tlb_flush_all(s);
}

/* XXX: inefficient but not critical as long as it is seldom used */
void riscv_cpu_flush_tlb_write_range_ram(RISCVCPUState *s,
                                         uint8_t *ram_ptr, size_t ram_size)
{
    uint8_t *ptr, *ram_end;
    int i;

    ram_end = ram_ptr + ram_size;
    for(i = 0; i < TLB_SIZE; i++) {
        if (s->tlb_write[i].vaddr != (target_ulong) -1) {
            ptr = (uint8_t *)(s->tlb_write[i].mem_addend +
                              (uintptr_t)s->tlb_write[i].vaddr);
            if (ptr >= ram_ptr && ptr < ram_end) {
                s->tlb_write[i].vaddr = -1;
            }
        }
    }
}


#define SSTATUS_MASK (MSTATUS_UIE | MSTATUS_SIE |       \
                      MSTATUS_UPIE | MSTATUS_SPIE |     \
                      MSTATUS_SPP | \
                      MSTATUS_FS | MSTATUS_XS | \
                      MSTATUS_SUM | MSTATUS_MXR | MSTATUS_UXL)

#define MSTATUS_MASK (MSTATUS_UIE | MSTATUS_SIE | MSTATUS_MIE |      \
                      MSTATUS_UPIE | MSTATUS_SPIE | MSTATUS_MPIE |    \
                      MSTATUS_SPP | MSTATUS_MPP | \
                      MSTATUS_FS | \
                      MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_MXR |\
                      MSTATUS_TVM | MSTATUS_TW | MSTATUS_TSR )
/* cycle and insn counters */
#define COUNTEREN_MASK ((1 << 0) | (1 << 2))

/* return the complete mstatus with the SD bit */
static target_ulong get_mstatus(RISCVCPUState *s, target_ulong mask)
{
    target_ulong val;
    BOOL sd;
    val = s->mstatus & mask;
    sd = ((val & MSTATUS_FS) == MSTATUS_FS) |
        ((val & MSTATUS_XS) == MSTATUS_XS);
    if (sd)
        val |= (target_ulong)1 << (XLEN - 1);
    return val;
}

static void set_mstatus(RISCVCPUState *s, target_ulong val)
{
    target_ulong mod, mask;

    /* flush the TLBs if change of MMU config */
    mod = s->mstatus ^ val;
    if ((mod & (MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_MXR)) != 0 ||
        ((s->mstatus & MSTATUS_MPRV) && (mod & MSTATUS_MPP) != 0)) {
        tlb_flush_all(s);
    }
    mask = MSTATUS_MASK & ~MSTATUS_FS;
    s->mstatus = (s->mstatus & ~mask) | (val & mask);
}

/* return -1 if invalid CSR. 0 if OK. 'will_write' indicate that the
   csr will be written after (used for CSR access check) */
static int csr_read(RISCVCPUState *s, target_ulong *pval, uint32_t csr,
                     BOOL will_write)
{
    target_ulong val;

    if (((csr & 0xc00) == 0xc00) && will_write)
        return -1; /* read-only CSR */
    if (s->priv < ((csr >> 8) & 3))
        return -1; /* not enough priviledge */

    switch(csr) {
    case 0xc00: /* ucycle */
        {
            uint32_t counteren;
            if (s->priv < PRV_M) {
                if (s->priv < PRV_S)
                    counteren = s->scounteren;
                else
                    counteren = s->mcounteren;
                if (((counteren >> (csr & 0x1f)) & 1) == 0)
                    goto invalid_csr;
            }
        }
        val = (int64_t)s->cycle_counter;
        break;
    case 0xc02: /* uinstret */
        {
            uint32_t counteren;
            if (s->priv < PRV_M) {
                if (s->priv < PRV_S)
                    counteren = s->scounteren;
                else
                    counteren = s->mcounteren;
                if (((counteren >> (csr & 0x1f)) & 1) == 0)
                    goto invalid_csr;
            }
        }
        val = (int64_t)s->insn_counter;
        break;
    case 0xc80: /* mcycleh */
        goto invalid_csr;
        break;
    case 0xc82: /* minstreth */
        goto invalid_csr;
        break;
    case 0x100: /* sstatus */
        val = get_mstatus(s, SSTATUS_MASK);
        break;
    case 0x104: /* sie */
        val = s->mie & s->mideleg;
        break;
    case 0x105:
        val = s->stvec;
        break;
    case 0x106:
        val = s->scounteren;
        break;
    case 0x140:
        val = s->sscratch;
        break;
    case 0x141:
        val = s->sepc;
        break;
    case 0x142:
        val = s->scause;
        break;
    case 0x143:
        val = s->stval;
        break;
    case 0x144: /* sip */
        val = s->mip & s->mideleg;
        break;
    case 0x180:
        if (s->priv == PRV_S && s->mstatus & MSTATUS_TVM)
            return -1;
        val = s->satp;
        break;
    case 0x300:
        val = get_mstatus(s, (target_ulong)-1);
        break;
    case 0x301:
        val = s->misa;
        break;
    case 0x302:
        val = s->medeleg;
        break;
    case 0x303:
        val = s->mideleg;
        break;
    case 0x304:
        val = s->mie;
        break;
    case 0x305:
        val = s->mtvec;
        break;
    case 0x306:
        val = s->mcounteren;
        break;
    case 0x340:
        val = s->mscratch;
        break;
    case 0x341:
        val = s->mepc;
        break;
    case 0x342:
        val = s->mcause;
        break;
    case 0x343:
        val = s->mtval;
        break;
    case 0x344:
        val = s->mip;
        break;
    case 0xb00: /* mcycle */
        val = (int64_t)s->cycle_counter;
        break;
    case 0xb02: /* minstret */
        val = (int64_t)s->insn_counter;
        break;
    case 0xb80: /* mcycleh */
        goto invalid_csr;
        break;
    case 0xb82: /* minstreth */
        goto invalid_csr;
        break;
    case 0x7a0: /* tselect */ /* ignore all */
    case 0x7a1: /* tdata1 */
    case 0x7a2: /* tdata2 */
    case 0x7a3: /* tdata3 */
        val = 0;
        break;
    case 0xf11: /* mvendorid */
        val = 0;
        break;
    case 0xf12: /* marchid */
        val = 0;
        break;
    case 0xf13: /* mimpid */
        val = 0;
        break;
    case 0xf14:
        val = s->mhartid;
        break;
    default:
    invalid_csr:
#ifdef DUMP_INVALID_CSR
        /* the 'time' counter is usually emulated */
        if (csr != 0xc01 && csr != 0xc81) {
            fprintf(stderr, "csr_read: invalid CSR=0x%x\n", csr);
        }
#endif
        *pval = 0;
        return -1;
    }
    *pval = val;
    return 0;
}

/* return -1 if invalid CSR, 0 if OK, 1 if the interpreter loop must be
   exited, 2 if TLBs have been flushed. */
static int csr_write(RISCVCPUState *s, uint32_t csr, target_ulong val)
{
    target_ulong mask;

#if defined(DUMP_CSR)
    fprintf(stderr, "csr_write: csr=0x%03x val=0x", csr);
    print_target_ulong(val);
    fprintf(stderr, "\n");
#endif
    switch(csr) {
    case 0x100: /* sstatus */
        set_mstatus(s, (s->mstatus & ~SSTATUS_MASK) | (val & SSTATUS_MASK));
        break;
    case 0x104: /* sie */
        mask = s->mideleg;
        s->mie = (s->mie & ~mask) | (val & mask);
        break;
    case 0x105:
        s->stvec = val & ~3;
        break;
    case 0x106:
        s->scounteren = val & COUNTEREN_MASK;
        break;
    case 0x140:
        s->sscratch = val;
        break;
    case 0x141:
        s->sepc = val & ~1;
        break;
    case 0x142:
        s->scause = val;
        break;
    case 0x143:
        s->stval = val;
        break;
    case 0x144: /* sip */
        mask = s->mideleg;
        s->mip = (s->mip & ~mask) | (val & mask);
        break;
    case 0x180:
        /* no ASID implemented */
        {
            int mode, new_mode;
            mode = s->satp >> 60;
            new_mode = (val >> 60) & 0xf;
            if (new_mode == 0 || (new_mode >= 8 && new_mode <= 9))
                mode = new_mode;
            s->satp = (val & (((uint64_t)1 << 44) - 1)) |
                ((uint64_t)mode << 60);
        }
        tlb_flush_all(s);
        return 2;

    case 0x300:
        set_mstatus(s, val);
        break;
    case 0x301: /* misa */
        /* ignore writes to misa */
        break;
    case 0x302:
        mask = (1 << (CAUSE_STORE_PAGE_FAULT + 1)) - 1;
        s->medeleg = (s->medeleg & ~mask) | (val & mask);
        break;
    case 0x303:
        mask = MIP_SSIP | MIP_STIP | MIP_SEIP;
        s->mideleg = (s->mideleg & ~mask) | (val & mask);
        break;
    case 0x304:
        mask = MIP_MSIP | MIP_MTIP | MIP_SSIP | MIP_STIP | MIP_SEIP;
        s->mie = (s->mie & ~mask) | (val & mask);
        break;
    case 0x305:
        /* ??DD no support for vectored iterrupts */
        s->mtvec = val & ~3;
        break;
    case 0x306:
        s->mcounteren = val & COUNTEREN_MASK;
        break;
    case 0x340:
        s->mscratch = val;
        break;
    case 0x341:
        s->mepc = val & ~1;
        break;
    case 0x342:
        s->mcause = val;
        break;
    case 0x343:
        s->mtval = val;
        break;
    case 0x344:
        mask = MIP_SSIP | MIP_STIP;
        s->mip = (s->mip & ~mask) | (val & mask);
        break;
    case 0x7a0: /* tselect */ /* ignore all */
    case 0x7a1: /* tdata1 */
    case 0x7a2: /* tdata2 */
    case 0x7a3: /* tdata3 */
        break;

    default:
#ifdef DUMP_INVALID_CSR
        fprintf(stderr, "csr_write: invalid CSR=0x%x\n", csr);
#endif
        return -1;
    }
    return 0;
}

static void set_priv(RISCVCPUState *s, int priv)
{
    if (s->priv != priv) {
        tlb_flush_all(s);
        s->priv = priv;
        /* ??D shouldn't we clear s->load_res here?
         *     so it fails because of a context switch? */
    }
}

static void raise_exception2(RISCVCPUState *s, uint32_t cause,
                             target_ulong tval)
{
    BOOL deleg;
    target_ulong causel;
#if defined(DUMP_EXCEPTIONS) || defined(DUMP_MMU_EXCEPTIONS) || defined(DUMP_INTERRUPTS)
    {
        int flag;
        flag = 0;
#ifdef DUMP_MMU_EXCEPTIONS
        if (cause == CAUSE_FAULT_FETCH ||
            cause == CAUSE_FAULT_LOAD ||
            cause == CAUSE_FAULT_STORE ||
            cause == CAUSE_FETCH_PAGE_FAULT ||
            cause == CAUSE_LOAD_PAGE_FAULT ||
            cause == CAUSE_STORE_PAGE_FAULT)
            flag = 1;
#endif
#ifdef DUMP_INTERRUPTS
        flag |= (cause & CAUSE_INTERRUPT) != 0;
#endif
#ifdef DUMP_EXCEPTIONS
        flag = (cause & CAUSE_INTERRUPT) == 0;
        if (cause == CAUSE_SUPERVISOR_ECALL)
            flag = 0;
#endif
        if (flag) {
            fprintf(stderr, "raise_exception: cause=0x%08x tval=0x", cause);
            print_target_ulong(tval);
            fprintf(stderr, "\n");
            dump_regs(s);
        }
    }
#endif

    if (s->priv <= PRV_S) {
        /* delegate the exception to the supervisor priviledge */
        if (cause & CAUSE_INTERRUPT)
            deleg = (s->mideleg >> (cause & (XLEN - 1))) & 1;
        else
            deleg = (s->medeleg >> cause) & 1;
    } else {
        deleg = 0;
    }

    causel = cause & 0x7fffffff;
    if (cause & CAUSE_INTERRUPT)
        causel |= (target_ulong)1 << (XLEN-1);

    if (deleg) {
        s->scause = causel;
        s->sepc = s->pc;
        s->stval = tval;
        s->mstatus = (s->mstatus & ~MSTATUS_SPIE) |
            (((s->mstatus >> s->priv) & 1) << MSTATUS_SPIE_SHIFT);
        s->mstatus = (s->mstatus & ~MSTATUS_SPP) |
            (s->priv << MSTATUS_SPP_SHIFT);
        s->mstatus &= ~MSTATUS_SIE;
        set_priv(s, PRV_S);
        s->pc = s->stvec;
    } else {
        s->mcause = causel;
        s->mepc = s->pc;
        s->mtval = tval;
        s->mstatus = (s->mstatus & ~MSTATUS_MPIE) |
            (((s->mstatus >> s->priv) & 1) << MSTATUS_MPIE_SHIFT);
        s->mstatus = (s->mstatus & ~MSTATUS_MPP) |
            (s->priv << MSTATUS_MPP_SHIFT);
        s->mstatus &= ~MSTATUS_MIE;
        set_priv(s, PRV_M);
        s->pc = s->mtvec;
    }
}

static void raise_exception(RISCVCPUState *s, uint32_t cause)
{
    raise_exception2(s, cause, 0);
}

static void handle_sret(RISCVCPUState *s)
{
    int spp, spie;
    spp = (s->mstatus >> MSTATUS_SPP_SHIFT) & 1;
    /* set the IE state to previous IE state */
    spie = (s->mstatus >> MSTATUS_SPIE_SHIFT) & 1;
    /* s->mstatus = (s->mstatus & ~(1 << spp)) |
        (spie << spp); */
    s->mstatus = (s->mstatus & ~(1 << MSTATUS_SIE_SHIFT)) |
        (spie << MSTATUS_SIE_SHIFT);
    /* set SPIE to 1 */
    s->mstatus |= MSTATUS_SPIE;
    /* set SPP to U */
    s->mstatus &= ~MSTATUS_SPP;
    set_priv(s, spp);
    s->pc = s->sepc;
}

static void handle_mret(RISCVCPUState *s)
{
    int mpp, mpie;
    mpp = (s->mstatus >> MSTATUS_MPP_SHIFT) & 3;
    /* set the IE state to previous IE state */
    mpie = (s->mstatus >> MSTATUS_MPIE_SHIFT) & 1;
    /* s->mstatus = (s->mstatus & ~(1 << mpp)) |
        (mpie << mpp); */
    s->mstatus = (s->mstatus & ~(1 << MSTATUS_MIE_SHIFT)) |
        (mpie << MSTATUS_MIE_SHIFT);
    /* set MPIE to 1 */
    s->mstatus |= MSTATUS_MPIE;
    /* set MPP to U */
    s->mstatus &= ~MSTATUS_MPP;
    set_priv(s, mpp);
    s->pc = s->mepc;
}

static inline uint32_t get_pending_irq_mask(RISCVCPUState *s)
{
    uint32_t pending_ints, enabled_ints;

    pending_ints = s->mip & s->mie;
    if (pending_ints == 0)
        return 0;

    enabled_ints = 0;
    switch(s->priv) {
    case PRV_M:
        if (s->mstatus & MSTATUS_MIE)
            enabled_ints = ~s->mideleg;
        break;
    case PRV_S:
        enabled_ints = ~s->mideleg;
        if (s->mstatus & MSTATUS_SIE)
            enabled_ints |= s->mideleg;
        break;
    default:
    case PRV_U:
        enabled_ints = -1;
        break;
    }
    return pending_ints & enabled_ints;
}

static __exception int raise_interrupt(RISCVCPUState *s)
{
    uint32_t mask;
    int irq_num;
    mask = get_pending_irq_mask(s);
    if (mask == 0)
        return 0;
    irq_num = ctz32(mask);
    raise_exception(s, irq_num | CAUSE_INTERRUPT);
    return -1;
}

#include "riscv_cpu_template.h"

void riscv_cpu_run(RISCVCPUState *s, uint64_t cycles_end)
{
    while (!s->power_down_flag && !s->shuthost_flag &&
        s->cycle_counter < cycles_end) {
        riscv_cpu_interp64(s, cycles_end);
    }
}

/* Note: the value is not accurate when called in riscv_cpu_interp() */
uint64_t riscv_cpu_get_cycle_counter(RISCVCPUState *s)
{
    return s->cycle_counter;
}

void riscv_cpu_set_cycle_counter(RISCVCPUState *s, uint64_t cycles)
{
    s->cycle_counter = cycles;
}

void riscv_cpu_set_mip(RISCVCPUState *s, uint32_t mask)
{
    s->mip |= mask;
    /* exit from power down if an interrupt is pending */
    if (s->power_down_flag && (s->mip & s->mie) != 0)
        s->power_down_flag = FALSE;
}

void riscv_cpu_reset_mip(RISCVCPUState *s, uint32_t mask)
{
    s->mip &= ~mask;
}

uint32_t riscv_cpu_get_mip(RISCVCPUState *s)
{
    return s->mip;
}

BOOL riscv_cpu_get_power_down(RISCVCPUState *s)
{
    return s->power_down_flag;
}

void riscv_cpu_set_power_down(RISCVCPUState *s, BOOL v)
{
    s->power_down_flag = v;
}

BOOL riscv_cpu_get_shuthost(RISCVCPUState *s)
{
    return s->shuthost_flag;
}

void riscv_cpu_set_shuthost(RISCVCPUState *s, BOOL v)
{
    s->shuthost_flag = v;
}

int riscv_cpu_get_max_xlen(void)
{
    return XLEN;
}

RISCVCPUState *riscv_cpu_init(PhysMemoryMap *mem_map)
{
    RISCVCPUState *s;
    s = mallocz(sizeof(*s));
    s->mem_map = mem_map;
    s->power_down_flag = FALSE;
    s->shuthost_flag = FALSE;
    s->pc = 0x1000;
    s->priv = PRV_M;
    s->mstatus = ((uint64_t)MXL << MSTATUS_UXL_SHIFT) |
        ((uint64_t)MXL << MSTATUS_SXL_SHIFT);
    s->misa = MXL; s->misa <<= (XLEN-2); /* set xlen to 64 */
    s->misa |= MCPUID_SUPER | MCPUID_USER | MCPUID_I | MCPUID_M | MCPUID_A;
    tlb_init(s);
    return s;
}

void riscv_cpu_end(RISCVCPUState *s)
{
    free(s);
}

uint64_t riscv_cpu_get_misa(RISCVCPUState *s)
{
    return s->misa;
}
