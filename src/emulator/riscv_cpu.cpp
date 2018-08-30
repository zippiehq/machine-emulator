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
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cinttypes>
#include <cstdint>
#include <cassert>
#include <bitset>
#include <iostream>
#include <functional>
#include <limits>
#include <type_traits>

//??D
//
// This code assumes the host's byte-ordering is the same as RISC-V's.
// RISC-V is little endian, and so is x86.
// There is a static_assert to prevent the code from compiling otherwise.
//
// This code assumes the modulo operator is such that
//
//      (a/b)*b + a%b = a
//
// i.e., the sign of the result is the sign of a.
// This is guaranteed from C++11 forward.
//
//   https://en.cppreference.com/w/cpp/language/operator_arithmetic
//
// RISC-V does not define this (at least I have not found it
// in the documentation), but the tests assume this behavior.
//
//   https://github.com/riscv/riscv-tests/blob/master/isa/rv64um/rem.S
//
// EVM defines the same behavior. See the yellowpaper.
//
// This code assumes right-shifts of negative values are arithmetic shifts.
// This is implementation-defined in C and C++.
// Most compilers indeed do arithmetic shifts:
//
//   https://docs.microsoft.com/en-us/cpp/c-language/right-shifts
//   https://gcc.gnu.org/onlinedocs/gcc-7.3.0/gcc/Integers-implementation.html#Integers-implementation
//   (clang should behave the same as gcc, but does not document it)
//   (I have not found documentation for icc)
//
// EVM does not have a shift operator.
// Solidity defines shift as division, which means it rounds negative numbers towards zero.
// WARNING: An arithmetic shift right would "round" a negative number away from zero!
//
// The code assumes narrowing conversions of signed types are modulo operations.
// This is implementation-defined in C and C++.
// Most compilers indeed do modulo narrowing:
//
//   https://docs.microsoft.com/en-us/cpp/c-language/demotion-of-integers
//   https://gcc.gnu.org/onlinedocs/gcc-7.3.0/gcc/Integers-implementation.html#Integers-implementation
//   (clang should behave the same as gcc, but does not document it)
//   (I have not found documentation for icc)
//
// Signed integer overflows are UNDEFINED according to C and C++.
// We do not assume signed integers handled overflowed with modulo arithmetic.
// Detecting and preventing overflows is awkward and costly.
// Fortunately, GCC offers intrinsics that have well-defined overflow behavior.
//
//   https://gcc.gnu.org/onlinedocs/gcc-7.3.0/gcc/Integer-Overflow-Builtins.html#Integer-Overflow-Builtins
//

// GCC complains about __int128 with -pedantic or -pedantic-errors
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
typedef __int128 int128_t;
typedef unsigned __int128 uint128_t;
#pragma GCC diagnostic pop

#define XLEN 64
#define MXL   2

#include "iomem.h"
#include "riscv_cpu.h"

#ifdef DUMP_INSN
extern "C" {
#include "dis/riscv-opc.h"
}
#endif

#define __exception __attribute__((warn_unused_result))

#define PR_target_ulong "016" PRIx64

typedef uint64_t mem_uint_t;

#define TLB_SIZE 256

#define CAUSE_MISALIGNED_FETCH        0x0
#define CAUSE_FETCH_FAULT             0x1
#define CAUSE_ILLEGAL_INSTRUCTION     0x2
#define CAUSE_BREAKPOINT              0x3
#define CAUSE_LOAD_FAULT              0x5
#define CAUSE_MISALIGNED_AMO          0x6
#define CAUSE_STORE_AMO_FAULT         0x7
#define CAUSE_ECALL                   0x8
#define CAUSE_FETCH_PAGE_FAULT        0xc
#define CAUSE_LOAD_PAGE_FAULT         0xd
#define CAUSE_STORE_AMO_PAGE_FAULT    0xf

#define CAUSE_INTERRUPT  ((uint64_t)1 << 63)

/* privilege levels */
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
#define MSTATUS_SD_SHIFT 31
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
#define MSTATUS_SD ((uint64_t)1 << MSTATUS_SD_SHIFT)
#define MSTATUS_UXL ((uint64_t)3 << MSTATUS_UXL_SHIFT)
#define MSTATUS_SXL ((uint64_t)3 << MSTATUS_SXL_SHIFT)


#define PG_SHIFT 12
#define PG_MASK ((1 << PG_SHIFT) - 1)

typedef struct {
    uint64_t vaddr;
    uintptr_t mem_addend;
} TLBEntry;

struct RISCVCPUState {
    uint64_t pc;
    uint64_t reg[32];

    uint8_t iflags_PRV; // current privilege level
    bool iflags_I; // CPU is idle (waiting for interrupts)
    bool iflags_H; // CPU is permanently halted

    bool iflags_B; // Set when we need to break from the the inner loop

    /* CSRs */
    uint64_t minstret;
    uint64_t mcycle;

    uint64_t mstatus;
    uint64_t mtvec;
    uint64_t mscratch;
    uint64_t mepc;
    uint64_t mcause;
    uint64_t mtval;
    uint64_t misa;

    uint32_t mie;
    uint32_t mip;
    uint32_t medeleg;
    uint32_t mideleg;
    uint32_t mcounteren;

    uint64_t stvec;
    uint64_t sscratch;
    uint64_t sepc;
    uint64_t scause;
    uint64_t stval;
    uint64_t satp;
    uint32_t scounteren;

    uint64_t ilrsc; /* for atomic LR/SC */

    PhysMemoryMap *mem_map;

    TLBEntry tlb_read[TLB_SIZE];
    TLBEntry tlb_write[TLB_SIZE];
    TLBEntry tlb_code[TLB_SIZE];
};

static void fprint_uint64_t(FILE *f, uint64_t a)
{
    fprintf(f, "%" PR_target_ulong, a);
}

static void print_uint64_t(uint64_t a)
{
    fprint_uint64_t(stderr, a);
}

static const char *reg_name[32] = {
"zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
"s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
"a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
"s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

void dump_regs(RISCVCPUState *s)
{
    int i, cols;
    const char priv_str[] = "USHM";
    cols = 256 / XLEN;
    fprintf(stderr, "pc = ");
    print_uint64_t(s->pc);
    fprintf(stderr, " ");
    for(i = 1; i < 32; i++) {
        fprintf(stderr, "%-3s= ", reg_name[i]);
        print_uint64_t(s->reg[i]);
        if ((i & (cols - 1)) == (cols - 1))
            fprintf(stderr, "\n");
        else
            fprintf(stderr, " ");
    }
    fprintf(stderr, "priv=%c", priv_str[s->iflags_PRV]);
    fprintf(stderr, " mstatus=");
    print_uint64_t(s->mstatus);
    fprintf(stderr, " cycles=%" PRId64, s->mcycle);
    fprintf(stderr, " insns=%" PRId64, s->minstret);
    fprintf(stderr, "\n");
#if 1
    fprintf(stderr, "mideleg=");
    print_uint64_t(s->mideleg);
    fprintf(stderr, " mie=");
    print_uint64_t(s->mie);
    fprintf(stderr, " mip=");
    print_uint64_t(s->mip);
    fprintf(stderr, "\n");
#endif
}

/* addr must be aligned. Only RAM accesses are supported */
template <typename T>
static inline void phys_write(RISCVCPUState *s, uint64_t addr, T val) {
    PhysMemoryRange *pr = get_phys_mem_range(s->mem_map, addr);
    if (!pr || !pr->is_ram)
        return;
    *(T *)(pr->phys_mem + (uintptr_t)(addr - pr->addr)) = val;
}

template <typename T>
static inline T phys_read(RISCVCPUState *s, uint64_t addr) {
    PhysMemoryRange *pr = get_phys_mem_range(s->mem_map, addr);
    if (!pr || !pr->is_ram)
        return 0;
    return *(T *)(pr->phys_mem + (uintptr_t)(addr - pr->addr));
}

template <typename T> int size_log2(void);

template <> int size_log2<uint8_t>(void) { return 0; }
template <> int size_log2<uint16_t>(void) { return 1; }
template <> int size_log2<uint32_t>(void) { return 2; }
template <> int size_log2<uint64_t>(void) { return 3; }

/* return 0 if OK, != 0 if exception */
template <typename T>
static inline int target_read(RISCVCPUState *s, T *pval, uint64_t addr)  {
    uint32_t tlb_idx;
    tlb_idx = (addr >> PG_SHIFT) & (TLB_SIZE - 1);
    if (s->tlb_read[tlb_idx].vaddr == (addr & ~(PG_MASK & ~(sizeof(T) - 1)))) {
        *pval = *(T *)(s->tlb_read[tlb_idx].mem_addend + (uintptr_t)addr);
    } else {
        mem_uint_t val;
        int ret;
        ret = target_read_slow(s, &val, addr, size_log2<T>());
        if (ret) return ret;
        *pval = val;
    }
    return 0;
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
                         uint64_t *ppaddr, uint64_t vaddr,
                         int access)
{
    int mode, levels, pte_bits, pte_idx, pte_mask, pte_size_log2, xwr, priv;
    int need_write, vaddr_shift, i, pte_addr_bits;
    uint64_t pte_addr, pte, vaddr_mask, paddr;

    if ((s->mstatus & MSTATUS_MPRV) && access != ACCESS_CODE) {
        /* use previous priviledge */
        priv = (s->mstatus >> MSTATUS_MPP_SHIFT) & 3;
    } else {
        priv = s->iflags_PRV;
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
    if ((((int64_t)vaddr << vaddr_shift) >> vaddr_shift) != (int64_t) vaddr)
        return -1;
    pte_addr_bits = 44;
    pte_addr = (s->satp & (((uint64_t)1 << pte_addr_bits) - 1)) << PG_SHIFT;
    pte_bits = 12 - pte_size_log2;
    pte_mask = (1 << pte_bits) - 1;
    for(i = 0; i < levels; i++) {
        vaddr_shift = PG_SHIFT + pte_bits * (levels - 1 - i);
        pte_idx = (vaddr >> vaddr_shift) & pte_mask;
        pte_addr += pte_idx << pte_size_log2;
        pte = phys_read<uint64_t>(s, pte_addr);
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
            vaddr_mask = ((uint64_t)1 << vaddr_shift) - 1;
            if (paddr  & vaddr_mask) /* alignment check */
                return -1;
            need_write = !(pte & PTE_A_MASK) ||
                (!(pte & PTE_D_MASK) && access == ACCESS_WRITE);
            pte |= PTE_A_MASK;
            if (access == ACCESS_WRITE)
                pte |= PTE_D_MASK;
            if (need_write) {
                phys_write<uint64_t>(s, pte_addr, pte);
            }
            *ppaddr = (vaddr & vaddr_mask) | (paddr  & ~vaddr_mask);
            return 0;
        } else {
            pte_addr = paddr;
        }
    }
    return -1;
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

static void tlb_flush_vaddr(RISCVCPUState *s, uint64_t vaddr)
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
        if (s->tlb_write[i].vaddr != (uint64_t) -1) {
            ptr = (uint8_t *)(s->tlb_write[i].mem_addend +
                              (uintptr_t)s->tlb_write[i].vaddr);
            if (ptr >= ram_ptr && ptr < ram_end) {
                s->tlb_write[i].vaddr = -1;
            }
        }
    }
}

#define SSTATUS_WRITE_MASK ( \
    MSTATUS_UIE  | \
    MSTATUS_SIE  | \
    MSTATUS_UPIE | \
    MSTATUS_SPIE | \
    MSTATUS_SPP  | \
    MSTATUS_FS   | \
    MSTATUS_SUM  | \
    MSTATUS_MXR  \
)

#define SSTATUS_READ_MASK ( \
    MSTATUS_UIE  | \
    MSTATUS_SIE  | \
    MSTATUS_UPIE | \
    MSTATUS_SPIE | \
    MSTATUS_SPP  | \
    MSTATUS_FS   | \
    MSTATUS_SUM  | \
    MSTATUS_MXR  | \
    MSTATUS_UXL  | \
    MSTATUS_SD  \
)

#define MSTATUS_WRITE_MASK ( \
    MSTATUS_UIE  | \
    MSTATUS_SIE  | \
    MSTATUS_MIE  | \
    MSTATUS_UPIE | \
    MSTATUS_SPIE | \
    MSTATUS_MPIE | \
    MSTATUS_SPP  | \
    MSTATUS_MPP  | \
    MSTATUS_FS   | \
    MSTATUS_MPRV | \
    MSTATUS_SUM  | \
    MSTATUS_MXR  | \
    MSTATUS_TVM  | \
    MSTATUS_TW   | \
    MSTATUS_TSR  \
)

#define MSTATUS_READ_MASK ( \
    MSTATUS_UIE  | \
    MSTATUS_SIE  | \
    MSTATUS_MIE  | \
    MSTATUS_UPIE | \
    MSTATUS_SPIE | \
    MSTATUS_MPIE | \
    MSTATUS_SPP  | \
    MSTATUS_MPP  | \
    MSTATUS_FS   | \
    MSTATUS_MPRV | \
    MSTATUS_SUM  | \
    MSTATUS_MXR  | \
    MSTATUS_TVM  | \
    MSTATUS_TW   | \
    MSTATUS_TSR  | \
    MSTATUS_UXL  | \
    MSTATUS_SXL  | \
    MSTATUS_SD  \
)

/* cycle and insn counters */
#define COUNTEREN_MASK ((1 << 0) | (1 << 2))

enum class CSR_address: uint32_t {
    ustatus = 0x000,
    uie = 0x004,
    utvec = 0x005,

    uscratch = 0x040,
    uepc = 0x041,
    ucause = 0x042,
    utval = 0x043,
    uip = 0x044,

    ucycle = 0xc00,
    utime = 0xc01,
    uinstret =  0xc02,
    ucycleh = 0xc80,
    utimeh = 0xc81,
    uinstreth = 0xc82,

    sstatus = 0x100,
    sedeleg = 0x102,
    sideleg = 0x103,
    sie = 0x104,
    stvec = 0x105,
    scounteren = 0x106,

    sscratch = 0x140,
    sepc = 0x141,
    scause = 0x142,
    stval = 0x143,
    sip = 0x144,

    satp = 0x180,

    mvendorid = 0xf11,
    marchid = 0xf12,
    mimplid = 0xf13,
    mhartid = 0xf14,

    mstatus = 0x300,
    misa = 0x301,
    medeleg = 0x302,
    mideleg = 0x303,
    mie = 0x304,
    mtvec = 0x305,
    mcounteren = 0x306,

    mscratch = 0x340,
    mepc = 0x341,
    mcause = 0x342,
    mtval = 0x343,
    mip = 0x344,

    mcycle = 0xb00,
    minstret = 0xb02,
    mcycleh = 0xb80,
    minstreth = 0xb82,

    tselect = 0x7a0,
    tdata1 = 0x7a1,
    tdata2 = 0x7a2,
    tdata3 = 0x7a3,
};

static inline bool csr_is_read_only(CSR_address csraddr) {
    // 0xc00--0xcff, 0xd00--0xdff, and 0xf00--0xfff are all read-only.
    // so as long as bits 0xc00 are set, the register is read-only
    return ((static_cast<uint32_t>(csraddr) & 0xc00) == 0xc00);
}

static inline uint32_t csr_priv(CSR_address csr) {
    return (static_cast<uint32_t>(csr) >> 8) & 3;
}

static void set_priv(RISCVCPUState *s, int priv)
{
    if (s->iflags_PRV != priv) {
        tlb_flush_all(s);
        s->iflags_PRV = priv;
        s->ilrsc = 0;
    }
}

static void raise_exception(RISCVCPUState *s, uint64_t cause,
    uint64_t tval)
{
#if defined(DUMP_EXCEPTIONS) || defined(DUMP_MMU_EXCEPTIONS) || defined(DUMP_INTERRUPTS)
    {
        int flag;
        flag = 0;
#ifdef DUMP_MMU_EXCEPTIONS
        if (cause == CAUSE_FETCH_FAULT ||
            cause == CAUSE_LOAD_FAULT ||
            cause == CAUSE_STORE_AMO_FAULT ||
            cause == CAUSE_FETCH_PAGE_FAULT ||
            cause == CAUSE_LOAD_PAGE_FAULT ||
            cause == CAUSE_STORE_AMO_PAGE_FAULT)
            flag = 1;
#endif
#ifdef DUMP_INTERRUPTS
        flag |= (cause & CAUSE_INTERRUPT) != 0;
#endif
#ifdef DUMP_EXCEPTIONS
        flag |= (cause & CAUSE_INTERRUPT) == 0;
#endif
        if (flag) {
            fprintf(stderr, "raise_exception: cause=0x");
            print_uint64_t(cause);
            fprintf(stderr, " tval=0x");
            print_uint64_t(tval);
            fprintf(stderr, "\n");
            dump_regs(s);
        }
    }
#endif

    // Check if exception should be delegated to supervisor privilege
    // For each interrupt or exception number, there is a bit at mideleg
    // or medeleg saying if it should be delegated
    bool deleg;
    if (s->iflags_PRV <= PRV_S) {
        if (cause & CAUSE_INTERRUPT)
            // Clear the CAUSE_INTERRUPT bit before shifting
            deleg = (s->mideleg >> (cause & (XLEN - 1))) & 1;
        else
            deleg = (s->medeleg >> cause) & 1;
    } else {
        deleg = 0;
    }

    if (deleg) {
        s->scause = cause;
        s->sepc = s->pc;
        s->stval = tval;
        s->mstatus = (s->mstatus & ~MSTATUS_SPIE) |
            (((s->mstatus >> s->iflags_PRV) & 1) << MSTATUS_SPIE_SHIFT);
        s->mstatus = (s->mstatus & ~MSTATUS_SPP) |
            (s->iflags_PRV << MSTATUS_SPP_SHIFT);
        s->mstatus &= ~MSTATUS_SIE;
        set_priv(s, PRV_S);
        s->pc = s->stvec;
    } else {
        s->mcause = cause;
        s->mepc = s->pc;
        s->mtval = tval;
        s->mstatus = (s->mstatus & ~MSTATUS_MPIE) |
            (((s->mstatus >> s->iflags_PRV) & 1) << MSTATUS_MPIE_SHIFT);
        s->mstatus = (s->mstatus & ~MSTATUS_MPP) |
            (s->iflags_PRV << MSTATUS_MPP_SHIFT);
        s->mstatus &= ~MSTATUS_MIE;
        set_priv(s, PRV_M);
        s->pc = s->mtvec;
    }
}

static inline uint32_t get_pending_irq_mask(RISCVCPUState *s)
{
    uint32_t pending_ints, enabled_ints;

    pending_ints = s->mip & s->mie;
    if (pending_ints == 0)
        return 0;

    enabled_ints = 0;
    switch(s->iflags_PRV) {
    case PRV_M:
        if (s->mstatus & MSTATUS_MIE)
            enabled_ints = ~s->mideleg;
        break;
    case PRV_S:
        // Interrupts not set in mideleg are machine-mode
        // and cannot be masked by supervisor mode
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

// The return value is undefined if v == 0
// This works on gcc and clang and uses the lzcnt instruction
static inline uint32_t ilog2(uint32_t v) {
    return 31 - __builtin_clz(v);
}

static int raise_interrupt(RISCVCPUState *s)
{
    uint32_t mask = get_pending_irq_mask(s);
    if (mask == 0) return 0;
    uint64_t irq_num = ilog2(mask);
    raise_exception(s, irq_num | CAUSE_INTERRUPT, 0);
    return -1;
}

uint64_t riscv_cpu_get_mcycle(const RISCVCPUState *s) {
    return s->mcycle;
}

void riscv_cpu_set_mcycle(RISCVCPUState *s, uint64_t cycles) {
    s->mcycle = cycles;
}

void riscv_cpu_set_mip(RISCVCPUState *s, uint32_t mask) {
    s->mip |= mask;
    /* exit from power down if an interrupt is pending */
    bool pending = (s->mip & s->mie);
    s->iflags_I &= !pending;
    s->iflags_B |= pending;
}

void riscv_cpu_reset_mip(RISCVCPUState *s, uint32_t mask) {
    s->mip &= ~mask;
}

uint32_t riscv_cpu_get_mip(const RISCVCPUState *s) {
    return s->mip;
}

bool riscv_cpu_get_iflags_I(const RISCVCPUState *s) {
    return s->iflags_I;
}

void riscv_cpu_set_iflags_I(RISCVCPUState *s) {
    s->iflags_I = true;
    s->iflags_B = true;
}

void riscv_cpu_reset_iflags_I(RISCVCPUState *s) {
    s->iflags_I = false;
}

bool riscv_cpu_get_iflags_H(const RISCVCPUState *s) {
    return s->iflags_H;
}

void riscv_cpu_set_iflags_H(RISCVCPUState *s) {
    s->iflags_H = true;
    s->iflags_B = true;
}

int riscv_cpu_get_max_xlen(const RISCVCPUState *)
{
    return XLEN;
}

RISCVCPUState *riscv_cpu_init(PhysMemoryMap *mem_map)
{
    RISCVCPUState *s = reinterpret_cast<RISCVCPUState *>(calloc(1, sizeof(*s)));
    s->mem_map = mem_map;
    s->iflags_I = false;
    s->iflags_H = false;
    s->pc = 0x1000;
    s->iflags_PRV = PRV_M;
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

uint64_t riscv_cpu_get_misa(const RISCVCPUState *s)
{
    return s->misa;
}

enum class opcode {
    LUI   = 0b0110111,
    AUIPC = 0b0010111,
    JAL   = 0b1101111,
    JALR  = 0b1100111,

    branch_group = 0b1100011,
    load_group = 0b0000011,
    store_group = 0b0100011,
    arithmetic_immediate_group = 0b0010011,
    arithmetic_group = 0b0110011,
    fence_group = 0b0001111,
    csr_env_trap_int_mm_group = 0b1110011,
    arithmetic_immediate_32_group = 0b0011011,
    arithmetic_32_group = 0b0111011,
    atomic_group = 0b0101111,
};

enum class branch_funct3 {
    BEQ  = 0b000,
    BNE  = 0b001,
    BLT  = 0b100,
    BGE  = 0b101,
    BLTU = 0b110,
    BGEU = 0b111
};

enum class load_funct3 {
    LB  = 0b000,
    LH  = 0b001,
    LW  = 0b010,
    LD  = 0b011,
    LBU = 0b100,
    LHU = 0b101,
    LWU = 0b110
};

enum class store_funct3 {
    SB = 0b000,
    SH = 0b001,
    SW = 0b010,
    SD = 0b011
};

enum class arithmetic_immediate_funct3 {
    ADDI  = 0b000,
    SLTI  = 0b010,
    SLTIU = 0b011,
    XORI  = 0b100,
    ORI   = 0b110,
    ANDI  = 0b111,
    SLLI  = 0b001,

    shift_right_immediate_group = 0b101,
};

enum class shift_right_immediate_funct6 {
    SRLI = 0b000000,
    SRAI = 0b010000
};

enum class arithmetic_funct3_funct7 {
    ADD    = 0b0000000000,
    SUB    = 0b0000100000,
    SLL    = 0b0010000000,
    SLT    = 0b0100000000,
    SLTU   = 0b0110000000,
    XOR    = 0b1000000000,
    SRL    = 0b1010000000,
    SRA    = 0b1010100000,
    OR     = 0b1100000000,
    AND    = 0b1110000000,
    MUL    = 0b0000000001,
    MULH   = 0b0010000001,
    MULHSU = 0b0100000001,
    MULHU  = 0b0110000001,
    DIV    = 0b1000000001,
    DIVU   = 0b1010000001,
    REM    = 0b1100000001,
    REMU   = 0b1110000001,
};

enum class fence_group_funct3 {
    FENCE   = 0b000,
    FENCE_I = 0b001
};

enum class env_trap_int_group_insn {
    ECALL  = 0b00000000000000000000000001110011,
    EBREAK = 0b00000000000100000000000001110011,
    URET   = 0b00000000001000000000000001110011,
    SRET   = 0b00010000001000000000000001110011,
    MRET   = 0b00110000001000000000000001110011,
    WFI    = 0b00010000010100000000000001110011
};

enum class csr_env_trap_int_mm_funct3 {
    CSRRW  = 0b001,
    CSRRS  = 0b010,
    CSRRC  = 0b011,
    CSRRWI = 0b101,
    CSRRSI = 0b110,
    CSRRCI = 0b111,

    env_trap_int_mm_group  = 0b000,
};

enum class arithmetic_immediate_32_funct3 {
    ADDIW = 0b000,
    SLLIW = 0b001,

    shift_right_immediate_32_group = 0b101,
};

enum class shift_right_immediate_32_funct7 {
    SRLIW = 0b0000000,
    SRAIW = 0b0100000
};

enum class arithmetic_32_funct3_funct7 {
    ADDW  = 0b0000000000,
    SUBW  = 0b0000100000,
    SLLW  = 0b0010000000,
    SRLW  = 0b1010000000,
    SRAW  = 0b1010100000,
    MULW  = 0b0000000001,
    DIVW  = 0b1000000001,
    DIVUW = 0b1010000001,
    REMW  = 0b1100000001,
    REMUW = 0b1110000001
};

enum class atomic_funct3_funct5 {
    LR_W      = 0b01000010,
    SC_W      = 0b01000011,
    AMOSWAP_W = 0b01000001,
    AMOADD_W  = 0b01000000,
    AMOXOR_W  = 0b01000100,
    AMOAND_W  = 0b01001100,
    AMOOR_W   = 0b01001000,
    AMOMIN_W  = 0b01010000,
    AMOMAX_W  = 0b01010100,
    AMOMINU_W = 0b01011000,
    AMOMAXU_W = 0b01011100,
    LR_D      = 0b01100010,
    SC_D      = 0b01100011,
    AMOSWAP_D = 0b01100001,
    AMOADD_D  = 0b01100000,
    AMOXOR_D  = 0b01100100,
    AMOAND_D  = 0b01101100,
    AMOOR_D   = 0b01101000,
    AMOMIN_D  = 0b01110000,
    AMOMAX_D  = 0b01110100,
    AMOMINU_D = 0b01111000,
    AMOMAXU_D = 0b01111100
};

template <typename DERIVED> class i_state_access {

    DERIVED &derived(void) {
        return *static_cast<DERIVED *>(this);
    }

    const DERIVED &derived(void) const {
        return *static_cast<const DERIVED *>(this);
    }

public:

    uint64_t read_register(RISCVCPUState *s, uint32_t reg) {
        return derived().do_read_register(s, reg);
    }

    void write_register(RISCVCPUState *s, uint32_t reg, uint64_t val) {
        return derived().do_write_register(s, reg, val);
    }

    uint64_t read_pc(RISCVCPUState *s) {
        return derived().do_read_pc(s);
    }

    void write_pc(RISCVCPUState *s, uint64_t val) {
        return derived().do_write_pc(s, val);
    }

	uint64_t read_minstret(RISCVCPUState *s) {
		return derived().do_read_minstret(s);
	}

	void write_minstret(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_minstret(s,val);
	}

	uint64_t read_mcycle(RISCVCPUState *s) {
		return derived().do_read_mcycle(s);
	}

	void write_mcycle(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_mcycle(s,val);
	}

	uint64_t read_mstatus(RISCVCPUState *s) {
		return derived().do_read_mstatus(s);
	}

	void write_mstatus(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_mstatus(s,val);
	}

	uint64_t read_mtvec(RISCVCPUState *s) {
		return derived().do_read_mtvec(s);
	}

	void write_mtvec(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_mtvec(s,val);
	}

	uint64_t read_mscratch(RISCVCPUState *s) {
		return derived().do_read_mscratch(s);
	}

	void write_mscratch(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_mscratch(s,val);
	}

	uint64_t read_mepc(RISCVCPUState *s) {
		return derived().do_read_mepc(s);
	}

	void write_mepc(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_mepc(s,val);
	}

	uint64_t read_mcause(RISCVCPUState *s) {
		return derived().do_read_mcause(s);
	}

	void write_mcause(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_mcause(s,val);
	}

	uint64_t read_mtval(RISCVCPUState *s) {
		return derived().do_read_mtval(s);
	}

	void write_mtval(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_mtval(s,val);
	}

	uint64_t read_misa(RISCVCPUState *s) {
		return derived().do_read_misa(s);
	}

	void write_misa(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_misa(s,val);
	}

	uint64_t read_mie(RISCVCPUState *s) {
		return derived().do_read_mie(s);
	}

	void write_mie(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_mie(s,val);
	}

	uint64_t read_mip(RISCVCPUState *s) {
		return derived().do_read_mip(s);
	}

	void write_mip(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_mip(s,val);
	}

	uint64_t read_medeleg(RISCVCPUState *s) {
		return derived().do_read_medeleg(s);
	}

	void write_medeleg(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_medeleg(s,val);
	}

	uint64_t read_mideleg(RISCVCPUState *s) {
		return derived().do_read_mideleg(s);
	}

	void write_mideleg(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_mideleg(s,val);
	}

	uint64_t read_mcounteren(RISCVCPUState *s) {
		return derived().do_read_mcounteren(s);
	}

	void write_mcounteren(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_mcounteren(s,val);
	}

	uint64_t read_stvec(RISCVCPUState *s) {
		return derived().do_read_stvec(s);
	}

	void write_stvec(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_stvec(s,val);
	}

	uint64_t read_sscratch(RISCVCPUState *s) {
		return derived().do_read_sscratch(s);
	}

	void write_sscratch(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_sscratch(s,val);
	}

	uint64_t read_sepc(RISCVCPUState *s) {
		return derived().do_read_sepc(s);
	}

	void write_sepc(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_sepc(s,val);
	}

	uint64_t read_scause(RISCVCPUState *s) {
		return derived().do_read_scause(s);
	}

	void write_scause(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_scause(s,val);
	}

	uint64_t read_stval(RISCVCPUState *s) {
		return derived().do_read_stval(s);
	}

	void write_stval(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_stval(s,val);
	}

	uint64_t read_satp(RISCVCPUState *s) {
		return derived().do_read_satp(s);
	}

	void write_satp(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_satp(s,val);
	}

	uint64_t read_scounteren(RISCVCPUState *s) {
		return derived().do_read_scounteren(s);
	}

	void write_scounteren(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_scounteren(s,val);
	}

	uint64_t read_ilrsc(RISCVCPUState *s) {
		return derived().do_read_ilrsc(s);
	}

	void write_ilrsc(RISCVCPUState *s, uint64_t val) {
		return derived().do_write_ilrsc(s,val);
	}
};

class state_access: public i_state_access<state_access> {
private:
    friend i_state_access<state_access>;

    void do_write_register(RISCVCPUState *s, uint32_t reg, uint64_t val) {
        assert(reg != 0);
        s->reg[reg] = val;
//fprintf(stderr, "write reg %d = %lx\n", reg, s->reg[reg]);
    }

    uint64_t do_read_register(RISCVCPUState *s, uint32_t reg) {
//fprintf(stderr, "read reg %d = %lx\n", reg, s->reg[reg]);
        return s->reg[reg];
    }

    uint64_t do_read_pc(RISCVCPUState *s) {
        return s->pc;
    }

    void do_write_pc(RISCVCPUState *s, uint64_t val) {
        s->pc = val;
    }

	uint64_t do_read_minstret(RISCVCPUState *s) {
		return s->minstret;
	}

	void do_write_minstret(RISCVCPUState *s, uint64_t val) {
		s->minstret = val;
	}

	uint64_t do_read_mcycle(RISCVCPUState *s) {
		return s->mcycle;
	}

	void do_write_mcycle(RISCVCPUState *s, uint64_t val) {
		s->mcycle = val;
	}

	uint64_t do_read_mstatus(RISCVCPUState *s) {
        return s->mstatus & MSTATUS_READ_MASK;
	}

	void do_write_mstatus(RISCVCPUState *s, uint64_t val) {
        uint64_t mstatus = do_read_mstatus(s);
        // If MMU configuration was changted, flush the TLBs
        // This does not need to be done within the blockchain
        uint64_t mod = mstatus ^ val;
        if ((mod & (MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_MXR)) != 0 ||
            ((mstatus & MSTATUS_MPRV) && (mod & MSTATUS_MPP) != 0)) {
            tlb_flush_all(s);
        }
        // Modify only bits that can be written to
        mstatus = (mstatus & ~MSTATUS_WRITE_MASK) | (val & MSTATUS_WRITE_MASK);
        // Update the SD bit
        if ((mstatus & MSTATUS_FS) == MSTATUS_FS) mstatus |= MSTATUS_SD;
        // Store results
        s->mstatus = mstatus;
	}

	uint64_t do_read_mtvec(RISCVCPUState *s) {
		return s->mtvec;
	}

	void do_write_mtvec(RISCVCPUState *s, uint64_t val) {
		s->mtvec = val;
	}

	uint64_t do_read_mscratch(RISCVCPUState *s) {
//fprintf(stderr, "read mscratch: %lx\n", s->mscratch);
		return s->mscratch;
	}

	void do_write_mscratch(RISCVCPUState *s, uint64_t val) {
		s->mscratch = val;
//fprintf(stderr, "write mscratch: %lx\n", val);
	}

	uint64_t do_read_mepc(RISCVCPUState *s) {
		return s->mepc;
	}

	void do_write_mepc(RISCVCPUState *s, uint64_t val) {
		s->mepc = val;
	}

	uint64_t do_read_mcause(RISCVCPUState *s) {
		return s->mcause;
	}

	void do_write_mcause(RISCVCPUState *s, uint64_t val) {
		s->mcause = val;
	}

	uint64_t do_read_mtval(RISCVCPUState *s) {
		return s->mtval;
	}

	void do_write_mtval(RISCVCPUState *s, uint64_t val) {
		s->mtval = val;
	}

	uint64_t do_read_misa(RISCVCPUState *s) {
		return s->misa;
	}

	void do_write_misa(RISCVCPUState *s, uint64_t val) {
		s->misa = val;
	}

	uint64_t do_read_mie(RISCVCPUState *s) {
		return s->mie;
	}

	void do_write_mie(RISCVCPUState *s, uint64_t val) {
		s->mie = val;
	}

	uint64_t do_read_mip(RISCVCPUState *s) {
		return s->mip;
	}

	void do_write_mip(RISCVCPUState *s, uint64_t val) {
		s->mip = val;
	}

	uint64_t do_read_medeleg(RISCVCPUState *s) {
		return s->medeleg;
	}

	void do_write_medeleg(RISCVCPUState *s, uint64_t val) {
		s->medeleg = val;
	}

	uint64_t do_read_mideleg(RISCVCPUState *s) {
		return s->mideleg;
	}

	void do_write_mideleg(RISCVCPUState *s, uint64_t val) {
		s->mideleg = val;
	}

	uint64_t do_read_mcounteren(RISCVCPUState *s) {
		return s->mcounteren;
	}

	void do_write_mcounteren(RISCVCPUState *s, uint64_t val) {
		s->mcounteren = val;
	}

	uint64_t do_read_stvec(RISCVCPUState *s) {
		return s->stvec;
	}

	void do_write_stvec(RISCVCPUState *s, uint64_t val) {
		s->stvec = val;
	}

	uint64_t do_read_sscratch(RISCVCPUState *s) {
		return s->sscratch;
	}

	void do_write_sscratch(RISCVCPUState *s, uint64_t val) {
		s->sscratch = val;
	}

	uint64_t do_read_sepc(RISCVCPUState *s) {
		return s->sepc;
	}

	void do_write_sepc(RISCVCPUState *s, uint64_t val) {
		s->sepc = val;
	}

	uint64_t do_read_scause(RISCVCPUState *s) {
		return s->scause;
	}

	void do_write_scause(RISCVCPUState *s, uint64_t val) {
		s->scause = val;
	}

	uint64_t do_read_stval(RISCVCPUState *s) {
		return s->stval;
	}

	void do_write_stval(RISCVCPUState *s, uint64_t val) {
		s->stval = val;
	}

	uint64_t do_read_satp(RISCVCPUState *s) {
		return s->satp;
	}

	void do_write_satp(RISCVCPUState *s, uint64_t val) {
		s->satp = val;
	}

	uint64_t do_read_scounteren(RISCVCPUState *s) {
		return s->scounteren;
	}

	void do_write_scounteren(RISCVCPUState *s, uint64_t val) {
		s->scounteren = val;
	}

	uint64_t do_read_ilrsc(RISCVCPUState *s) {
		return s->ilrsc;
	}

	void do_write_ilrsc(RISCVCPUState *s, uint64_t val) {
		s->ilrsc = val;
	}

};

static inline uint32_t insn_rd(uint32_t insn) {
    return (insn >> 7) & 0b11111;
}

static inline uint32_t insn_rs1(uint32_t insn) {
    return (insn >> 15) & 0b11111;
}

static inline uint32_t insn_rs2(uint32_t insn) {
    return (insn >> 20) & 0b11111;
}

static inline int32_t insn_I_imm(uint32_t insn) {
    return (int32_t)insn >> 20;
}

static inline uint32_t insn_I_uimm(uint32_t insn) {
    return insn >> 20;
}

static inline int32_t insn_U_imm(uint32_t insn) {
    return static_cast<int32_t>(insn & 0xfffff000);
}

static inline int32_t insn_B_imm(uint32_t insn) {
    int32_t imm = ((insn >> (31 - 12)) & (1 << 12)) |
        ((insn >> (25 - 5)) & 0x7e0) |
        ((insn >> (8 - 1)) & 0x1e) |
        ((insn << (11 - 7)) & (1 << 11));
    imm = (imm << 19) >> 19;
    return imm;
}

static inline int32_t insn_J_imm(uint32_t insn) {
    int32_t imm = ((insn >> (31 - 20)) & (1 << 20)) |
        ((insn >> (21 - 1)) & 0x7fe) |
        ((insn >> (20 - 11)) & (1 << 11)) |
        (insn & 0xff000);
    imm = (imm << 11) >> 11;
    return imm;
}

static inline int32_t insn_S_imm(uint32_t insn) {
    return (static_cast<int32_t>(insn & 0xfe000000) >> (25 - 5)) | ((insn >> 7) & 0b11111);
}

static inline uint32_t insn_opcode(uint32_t insn) {
    //std::cerr << "opcode: " << std::bitset<7>(insn & 0b1111111) << '\n';
    return insn & 0b1111111;
}

static inline uint32_t insn_funct3(uint32_t insn) {
    //std::cerr << "funct3: " << std::bitset<3>((insn >> 12) & 0b111) << '\n';
    return (insn >> 12) & 0b111;
}

static inline uint32_t insn_funct3_funct7(uint32_t insn) {
    //std::cerr << "funct3_funct7: " << std::bitset<10>(((insn >> 5) & 0b1110000000) | (insn >> 24)) << '\n';
    return ((insn >> 5) & 0b1110000000) | (insn >> 25);
}

static inline uint32_t insn_funct3_funct5(uint32_t insn) {
    //std::cerr << "funct3_funct5: " << std::bitset<8>(((insn >> 7) & 0b11100000) | (insn >> 27)) << '\n';
    return ((insn >> 7) & 0b11100000) | (insn >> 27);
}

static inline uint32_t insn_funct7(uint32_t insn) {
    //std::cerr << "funct7: " << std::bitset<7>((insn >> 25) & 0b1111111) << '\n';
    return (insn >> 25) & 0b1111111;
}

static inline uint32_t insn_funct6(uint32_t insn) {
    //std::cerr << "funct6: " << std::bitset<6>((insn >> 26) & 0b111111) << '\n';
    return (insn >> 26) & 0b111111;
}

template <typename T>
static bool read_memory_slow(RISCVCPUState *s, uint64_t addr, T *pval);

template <typename T>
static inline bool read_memory(RISCVCPUState *s, uint64_t addr, T *pval)  {
    int tlb_idx = (addr >> PG_SHIFT) & (TLB_SIZE - 1);
    if (s->tlb_read[tlb_idx].vaddr == (addr & ~(PG_MASK & ~(sizeof(T) - 1)))) {
        *pval = *reinterpret_cast<T *>(s->tlb_read[tlb_idx].mem_addend + (uintptr_t)addr);
        return true;
    } else {
        return read_memory_slow<T>(s, addr, pval);
    }
}

/* return 0 if OK, != 0 if exception */
template <typename T>
static bool read_memory_slow(RISCVCPUState *s, uint64_t addr, T *pval) {
    // To make sure shifts are logical
    using U = std::make_unsigned_t<T>;
    // Deal with unaligned accesses
    uint64_t al = addr & (sizeof(T)-1);
    if (al != 0) {
        U v0, v1;
        addr -= al;
        //??D We will have to change this code to make only 64-bit accesses to memory
        // Read aligned word right before
        if (!read_memory<U>(s, addr, &v0)) return false;
        // Read aligned word right after
        if (!read_memory<U>(s, addr + sizeof(U), &v1)) return false;
        // Extract desired word from the middle
        *pval = static_cast<T>((v0 >> (al * 8)) | (v1 << ((sizeof(U)-al) * 8)));
        return true;
    // Deal with aligned accesses
    } else {
        uint64_t paddr;
        if (get_phys_addr(s, &paddr, addr, ACCESS_READ)) {
            raise_exception(s, CAUSE_LOAD_PAGE_FAULT, addr);
            return false;
        }
        PhysMemoryRange *pr = get_phys_mem_range(s->mem_map, paddr);
        if (!pr) {
            raise_exception(s, CAUSE_LOAD_FAULT, addr);
            return false;
        } else if (pr->is_ram) {
            int tlb_idx = (addr >> PG_SHIFT) & (TLB_SIZE - 1);
            uint8_t *ptr = pr->phys_mem + (uintptr_t)(paddr - pr->addr);
            s->tlb_read[tlb_idx].vaddr = addr & ~PG_MASK;
            s->tlb_read[tlb_idx].mem_addend = (uintptr_t)ptr - addr;
            *pval = *reinterpret_cast<T *>(ptr);
            return true;
        } else {
            uint64_t offset = paddr - pr->addr;
            uint64_t val;
            // If we do not know how to read, we treat this as a PMA viloation
            if (!pr->read_func(pr->opaque, offset, &val, size_log2<U>())) {
                raise_exception(s, CAUSE_LOAD_FAULT, addr);
                return false;
            }
            *pval = static_cast<T>(val);
            return true;
        }
    }
}

template <typename T>
static inline bool write_memory(RISCVCPUState *s, uint64_t addr, uint64_t val);

template <typename T>
static bool write_memory_slow(RISCVCPUState *s, uint64_t addr, uint64_t val) {
    using U = std::make_unsigned_t<T>;
    /* first handle unaligned accesses */
    const int size = 1 << size_log2<U>();
    if ((addr & (size - 1)) != 0) {
        /* WARNING: must avoid modifying memory in case of exception */
        for (int i = 0; i < size; i++) {
            if (!write_memory<uint8_t>(s, addr + i, (val >> (8 * i)) & 0xff)) {
                return false;
            }
        }
        return true;
    } else {
        uint64_t paddr, offset;
        if (get_phys_addr(s, &paddr, addr, ACCESS_WRITE)) {
            raise_exception(s, CAUSE_STORE_AMO_PAGE_FAULT, addr);
            return false;
        }
        PhysMemoryRange *pr = get_phys_mem_range(s->mem_map, paddr);
        if (!pr) {
            // If we do not have the range in our map, we treat this as a PMA viloation
            raise_exception(s, CAUSE_STORE_AMO_FAULT, addr);
            return false;
        } else if (pr->is_ram) {
            phys_mem_set_dirty_bit(pr, paddr - pr->addr);
            int tlb_idx = (addr >> PG_SHIFT) & (TLB_SIZE - 1);
            uint8_t *ptr = pr->phys_mem + (uintptr_t)(paddr - pr->addr);
            s->tlb_write[tlb_idx].vaddr = addr & ~PG_MASK;
            s->tlb_write[tlb_idx].mem_addend = (uintptr_t)ptr - addr;
            *reinterpret_cast<T *>(ptr) = static_cast<T>(val);
            return true;
        } else {
            offset = paddr - pr->addr;
            // If we do not know how to write, we treat this as a PMA viloation
            if (!pr->write_func(pr->opaque, offset, val, size_log2<U>())) {
                raise_exception(s, CAUSE_STORE_AMO_FAULT, addr);
                return false;
            }
            return true;
        }
    }
}

template <typename T>
static inline bool write_memory(RISCVCPUState *s, uint64_t addr, uint64_t val) {
    uint32_t tlb_idx;
    tlb_idx = (addr >> PG_SHIFT) & (TLB_SIZE - 1);
    if (s->tlb_write[tlb_idx].vaddr == (addr & ~(PG_MASK & ~(sizeof(T) - 1)))) {
        *reinterpret_cast<T *>(s->tlb_write[tlb_idx].mem_addend + (uintptr_t)addr) = static_cast<T>(val);
        return true;
    } else {
        return write_memory_slow<T>(s, addr, val);
    }
}

static void dump_insn(RISCVCPUState *s, uint64_t pc, uint32_t insn, const char *name) {
#ifdef DUMP_INSN
    fprintf(stderr, "%s\n", name);
    uint64_t ppc;
    if (!get_phys_addr(s, &ppc, pc, ACCESS_CODE)) {
        fprintf(stderr, "p    %08" PRIx64, ppc);
    } else {
        ppc = pc;
        fprintf(stderr, "v    %08" PRIx64, ppc);
    }
    fprintf(stderr, ":   %08" PRIx32 "   ", insn);
    fprintf(stderr, "\n");
//    dump_regs(s);
#else
    (void) s;
    (void) pc;
    (void) insn;
    (void) name;
#endif
}

// An execute_OP function is only invoked when the opcode
// has been decoded enough to preclude any other instruction.
// In some cases, further checks are needed to ensure the
// instruction is valid.

template <typename STATE_ACCESS>
static inline bool execute_illegal_insn_exception(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    (void) a; (void) pc;
    raise_exception(s, CAUSE_ILLEGAL_INSTRUCTION, insn);
    return false;
}

template <typename STATE_ACCESS>
static inline bool execute_misaligned_fetch_exception(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc) {
    (void) a;
    raise_exception(s, CAUSE_MISALIGNED_FETCH, pc);
    return true;
}

template <typename STATE_ACCESS>
static inline bool execute_raised_exception(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc) {
    (void) a; (void) s; (void) pc;
    return true;
}

template <typename STATE_ACCESS>
static inline bool execute_jump(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc) {
    a.write_pc(s, pc);
    s->iflags_B = true; // overkill
    return true;
}

template <typename STATE_ACCESS>
static inline bool execute_next_insn(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc) {
    a.write_pc(s, pc + 4);
    return true;
}

template <typename T, typename STATE_ACCESS>
static inline bool execute_LR(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    uint64_t addr = a.read_register(s, insn_rs1(insn));
    T val = 0;
    if (!read_memory<T>(s, addr, &val))
        return true;
    a.write_ilrsc(s, addr);
    uint32_t rd = insn_rd(insn);
    if (rd != 0)
        a.write_register(s, rd, static_cast<uint64_t>(val));
    return execute_next_insn(a, s, pc);
}

template <typename T, typename STATE_ACCESS>
static inline bool execute_SC(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    uint64_t val = 0;
    uint64_t addr = a.read_register(s, insn_rs1(insn));
    if (a.read_ilrsc(s) == addr) {
        if (!write_memory<T>(s, addr, static_cast<T>(a.read_register(s, insn_rs2(insn)))))
            return true;
    } else {
        val = 1;
    }
    uint32_t rd = insn_rd(insn);
    if (rd != 0)
        a.write_register(s, rd, val);
    return execute_next_insn(a, s, pc);
}

template <typename STATE_ACCESS>
static inline bool execute_LR_W(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    (void) a; (void) s; (void) pc; (void) insn;
    if ((insn & 0b00000001111100000000000000000000) == 0 ) {
        dump_insn(s, pc, insn, "LR_W");
        return execute_LR<int32_t>(a, s, pc, insn);
    } else {
        return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

template <typename STATE_ACCESS>
static inline bool execute_SC_W(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SC_W");
    return execute_SC<int32_t>(a, s, pc, insn);
}

template <typename T, typename STATE_ACCESS, typename F>
static inline bool execute_AMO(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn, const F &f) {
    uint64_t addr = a.read_register(s, insn_rs1(insn));
    T valm = 0;
    if (!read_memory<T>(s, addr, &valm))
        return true;
    T valr = static_cast<T>(a.read_register(s, insn_rs2(insn)));
    valr = f(valm, valr);
    if (!write_memory<T>(s, addr, valr))
        return true;
    uint32_t rd = insn_rd(insn);
    if (rd != 0)
        a.write_register(s, rd, static_cast<uint64_t>(valm));
    return execute_next_insn(a, s, pc);
}

template <typename STATE_ACCESS>
static inline bool execute_AMOSWAP_W(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOSWAP_W");
    return execute_AMO<int32_t>(a, s, pc, insn, [](int32_t valm, int32_t valr) -> int32_t { (void) valm; return valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOADD_W(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOADD_W");
    return execute_AMO<int32_t>(a, s, pc, insn, [](int32_t valm, int32_t valr) -> int32_t { return valm + valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOXOR_W(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    return execute_AMO<int32_t>(a, s, pc, insn, [](int32_t valm, int32_t valr) -> int32_t { return valm ^ valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOAND_W(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOAND_W");
    return execute_AMO<int32_t>(a, s, pc, insn, [](int32_t valm, int32_t valr) -> int32_t { return valm & valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOOR_W(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOOR_W");
    return execute_AMO<int32_t>(a, s, pc, insn, [](int32_t valm, int32_t valr) -> int32_t { return valm | valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOMIN_W(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOMIN_W");
    return execute_AMO<int32_t>(a, s, pc, insn, [](int32_t valm, int32_t valr) -> int32_t { return valm < valr? valm: valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOMAX_W(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOMAX_W");
    return execute_AMO<int32_t>(a, s, pc, insn, [](int32_t valm, int32_t valr) -> int32_t { return valm > valr? valm: valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOMINU_W(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOMINU_W");
    return execute_AMO<int32_t>(a, s, pc, insn, [](int32_t valm, int32_t valr) -> int32_t {
        return static_cast<uint32_t>(valm) < static_cast<uint32_t>(valr)? valm: valr;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOMAXU_W(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOMAXU_W");
    return execute_AMO<int32_t>(a, s, pc, insn, [](int32_t valm, int32_t valr) -> int32_t {
        return static_cast<uint32_t>(valm) > static_cast<uint32_t>(valr)? valm: valr;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_LR_D(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    if ((insn & 0b00000001111100000000000000000000) == 0 ) {
        dump_insn(s, pc, insn, "LR_D");
        return execute_LR<uint64_t>(a, s, pc, insn);
    } else {
        return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

template <typename STATE_ACCESS>
static inline bool execute_SC_D(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SC_D");
    return execute_SC<uint64_t>(a, s, pc, insn);
}

template <typename STATE_ACCESS>
static inline bool execute_AMOSWAP_D(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOSWAP_D");
    return execute_AMO<int64_t>(a, s, pc, insn, [](int64_t valm, int64_t valr) -> int64_t { (void) valm; return valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOADD_D(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOADD_D");
    return execute_AMO<int64_t>(a, s, pc, insn, [](int64_t valm, int64_t valr) -> int64_t { return valm + valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOXOR_D(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    return execute_AMO<int64_t>(a, s, pc, insn, [](int64_t valm, int64_t valr) -> int64_t { return valm ^ valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOAND_D(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOAND_D");
    return execute_AMO<int64_t>(a, s, pc, insn, [](int64_t valm, int64_t valr) -> int64_t { return valm & valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOOR_D(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOOR_D");
    return execute_AMO<int64_t>(a, s, pc, insn, [](int64_t valm, int64_t valr) -> int64_t { return valm | valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOMIN_D(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOMIN_D");
    return execute_AMO<int64_t>(a, s, pc, insn, [](int64_t valm, int64_t valr) -> int64_t { return valm < valr? valm: valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOMAX_D(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOMAX_D");
    return execute_AMO<int64_t>(a, s, pc, insn, [](int64_t valm, int64_t valr) -> int64_t { return valm > valr? valm: valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOMINU_D(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOMINU_D");
    return execute_AMO<uint64_t>(a, s, pc, insn,
        [](uint64_t valm, uint64_t valr) -> uint64_t { return valm < valr? valm: valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_AMOMAXU_D(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AMOMAXU_D");
    return execute_AMO<uint64_t>(a, s, pc, insn,
        [](uint64_t valm, uint64_t valr) -> uint64_t { return valm > valr? valm: valr; });
}

template <typename STATE_ACCESS>
static inline bool execute_ADDW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "ADDW");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        // Discard upper 32 bits
        int32_t rs1w = static_cast<int32_t>(rs1);
        int32_t rs2w = static_cast<int32_t>(rs2);
        int32_t val = 0;
        __builtin_add_overflow(rs1w, rs2w, &val);
        return static_cast<uint64_t>(val);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SUBW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SUBW");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        // Convert 64-bit to 32-bit
        int32_t rs1w = static_cast<int32_t>(rs1);
        int32_t rs2w = static_cast<int32_t>(rs2);
        int32_t val = 0;
        __builtin_sub_overflow(rs1w, rs2w, &val);
        return static_cast<uint64_t>(val);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SLLW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SLLW");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        int32_t rs1w = static_cast<int32_t>(rs1) << (rs2 & 31);
        return static_cast<uint64_t>(rs1w);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SRLW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SRLW");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        int32_t rs1w = static_cast<int32_t>(static_cast<uint32_t>(rs1) >> (rs2 & 31));
        return static_cast<uint64_t>(rs1w);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SRAW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SRAW");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        int32_t rs1w = static_cast<int32_t>(rs1) >> (rs2 & 31);
        return static_cast<uint64_t>(rs1w);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_MULW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "MULW");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        int32_t rs1w = static_cast<int32_t>(rs1);
        int32_t rs2w = static_cast<int32_t>(rs2);
        int32_t val = 0;
        __builtin_mul_overflow(rs1w, rs2w, &val);
        return static_cast<uint64_t>(val);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_DIVW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "DIVW");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        int32_t rs1w = static_cast<int32_t>(rs1);
        int32_t rs2w = static_cast<int32_t>(rs2);
        if (rs2w == 0) {
            return static_cast<uint64_t>(-1);
        } else if (rs1w == ((int32_t)1 << (32 - 1)) && rs2w == -1) {
            return static_cast<uint64_t>(rs1w);
        } else {
            return static_cast<uint64_t>(rs1w / rs2w);
        }
    });
}

template <typename STATE_ACCESS>
static inline bool execute_DIVUW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "DIVUW");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        uint32_t rs1w = static_cast<uint32_t>(rs1);
        uint32_t rs2w = static_cast<uint32_t>(rs2);
        if (rs2w == 0) {
            return static_cast<uint64_t>(-1);
        } else {
            return static_cast<uint64_t>(static_cast<int32_t>(rs1w / rs2w));
        }
    });
}

template <typename STATE_ACCESS>
static inline bool execute_REMW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "REMW");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        int32_t rs1w = static_cast<int32_t>(rs1);
        int32_t rs2w = static_cast<int32_t>(rs2);
        if (rs2w == 0) {
            return static_cast<uint64_t>(rs1w);
        } else if (rs1w == ((int32_t)1 << (32 - 1)) && rs2w == -1) {
            return static_cast<uint64_t>(0);
        } else {
            return static_cast<uint64_t>(rs1w % rs2w);
        }
    });
}

template <typename STATE_ACCESS>
static inline bool execute_REMUW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    (void) a; (void) s; (void) pc; (void) insn;
    dump_insn(s, pc, insn, "REMUW");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        uint32_t rs1w = static_cast<uint32_t>(rs1);
        uint32_t rs2w = static_cast<uint32_t>(rs2);
        if (rs2w == 0) {
            return static_cast<uint64_t>(static_cast<int32_t>(rs1w));
        } else {
            return static_cast<uint64_t>(static_cast<int32_t>(rs1w % rs2w));
        }
    });
}

static inline uint64_t read_csr_fail(bool *status) {
    *status = false;
    return 0;
}

static inline uint64_t read_csr_success(uint64_t val, bool *status) {
    *status = true;
    return val;
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_ucycle(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    uint32_t counteren;
    if (s->iflags_PRV < PRV_M) {
        if (s->iflags_PRV < PRV_S) {
            counteren = a.read_scounteren(s);
        } else {
            counteren = a.read_mcounteren(s);
        }
        if (((counteren >> (static_cast<int>(csraddr) & 0x1f)) & 1) == 0) {
            return read_csr_fail(status);
        }
    }
    return read_csr_success(a.read_mcycle(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_uinstret(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    uint32_t counteren;
    if (s->iflags_PRV < PRV_M) {
        if (s->iflags_PRV < PRV_S) {
            counteren = a.read_scounteren(s);
        } else {
            counteren = a.read_mcounteren(s);
        }
        if (((counteren >> (static_cast<int>(csraddr) & 0x1f)) & 1) == 0) {
            return read_csr_fail(status);
        }
    }
    return read_csr_success(a.read_minstret(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_sstatus(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_mstatus(s) & SSTATUS_READ_MASK, status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_sie(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    uint64_t mie = a.read_mie(s);
    uint64_t mideleg = a.read_mideleg(s);
    return read_csr_success(mie & mideleg, status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_stvec(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_stvec(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_scounteren(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_scounteren(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_sscratch(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_sscratch(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_sepc(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_sepc(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_scause(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_scause(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_stval(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_stval(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_sip(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    // Ensure values are are loaded in order: do not nest with operator
    uint64_t mip = a.read_mip(s);
    uint64_t mideleg = a.read_mideleg(s);
    return read_csr_success(mip & mideleg, status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_satp(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    uint64_t mstatus = a.read_mstatus(s);
    if (s->iflags_PRV == PRV_S && mstatus & MSTATUS_TVM) {
        return read_csr_fail(status);
    } else {
        return read_csr_success(a.read_satp(s), status);
    }
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_mstatus(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_mstatus(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_misa(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_misa(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_medeleg(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_medeleg(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_mideleg(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_mideleg(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_mie(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_mie(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_mtvec(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_mtvec(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_mcounteren(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_mcounteren(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_mscratch(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_mscratch(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_mepc(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_mepc(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_mcause(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_mcause(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_mtval(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_mtval(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_mip(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_mip(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_mcycle(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_mcycle(s), status);
}

template <typename STATE_ACCESS>
static inline uint64_t read_csr_minstret(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {
    (void) csraddr;
    return read_csr_success(a.read_minstret(s), status);
}

template <typename STATE_ACCESS>
static uint64_t read_csr(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, bool *status) {

    if (csr_priv(csraddr) > s->iflags_PRV)
        return read_csr_fail(status);

    switch (csraddr) {
        case CSR_address::ucycle: return read_csr_ucycle(a, s, csraddr, status);
        case CSR_address::uinstret: return read_csr_uinstret(a, s, csraddr, status);
        //??D case CSR_address::utime: ?

        case CSR_address::sstatus: return read_csr_sstatus(a, s, csraddr, status);
        case CSR_address::sie: return read_csr_sie(a, s, csraddr, status);
        case CSR_address::stvec: return read_csr_stvec(a, s, csraddr, status);
        case CSR_address::scounteren: return read_csr_scounteren(a, s, csraddr, status);
        case CSR_address::sscratch: return read_csr_sscratch(a, s, csraddr, status);
        case CSR_address::sepc: return read_csr_sepc(a, s, csraddr, status);
        case CSR_address::scause: return read_csr_scause(a, s, csraddr, status);
        case CSR_address::stval: return read_csr_stval(a, s, csraddr, status);
        case CSR_address::sip: return read_csr_sip(a, s, csraddr, status);
        case CSR_address::satp: return read_csr_satp(a, s, csraddr, status);


        case CSR_address::mstatus: return read_csr_mstatus(a, s, csraddr, status);
        case CSR_address::misa: return read_csr_misa(a, s, csraddr, status);
        case CSR_address::medeleg: return read_csr_medeleg(a, s, csraddr, status);
        case CSR_address::mideleg: return read_csr_mideleg(a, s, csraddr, status);
        case CSR_address::mie: return read_csr_mie(a, s, csraddr, status);
        case CSR_address::mtvec: return read_csr_mtvec(a, s, csraddr, status);
        case CSR_address::mcounteren: return read_csr_mcounteren(a, s, csraddr, status);


        case CSR_address::mscratch: return read_csr_mscratch(a, s, csraddr, status);
        case CSR_address::mepc: return read_csr_mepc(a, s, csraddr, status);
        case CSR_address::mcause: return read_csr_mcause(a, s, csraddr, status);
        case CSR_address::mtval: return read_csr_mtval(a, s, csraddr, status);
        case CSR_address::mip: return read_csr_mip(a, s, csraddr, status);

        case CSR_address::mcycle: return read_csr_mcycle(a, s, csraddr, status);
        case CSR_address::minstret: return read_csr_minstret(a, s, csraddr, status);

        // All hardwired to zero
        case CSR_address::tselect:
        case CSR_address::tdata1:
        case CSR_address::tdata2:
        case CSR_address::tdata3:
        case CSR_address::mvendorid:
        case CSR_address::marchid:
        case CSR_address::mimplid:
        case CSR_address::mhartid:
           return read_csr_success(0, status);

        // Invalid CSRs
        default:
        //case CSR_address::ustatus: // no U-mode traps
        //case CSR_address::uie: // no U-mode traps
        //case CSR_address::utvec: // no U-mode traps
        //case CSR_address::uscratch: // no U-mode traps
        //case CSR_address::uepc: // no U-mode traps
        //case CSR_address::ucause: // no U-mode traps
        //case CSR_address::utval: // no U-mode traps
        //case CSR_address::uip: // no U-mode traps
        //case CSR_address::sedeleg: // no U-mode traps
        //case CSR_address::sideleg: // no U-mode traps
        //case CSR_address::ucycleh: // 32-bit only
        //case CSR_address::utimeh: // 32-bit only
        //case CSR_address::uinstreth: // 32-bit only
        //case CSR_address::mcycleh: // 32-bit only
        //case CSR_address::minstreth: // 32-bit only
#ifdef DUMP_INVALID_CSR
            /* the 'time' counter is usually emulated */
            //??D but we don't emulate it, so maybe we should handle it right here
            if (csraddr != CSR_address::utime) {
                fprintf(stderr, "csr_read: invalid CSR=0x%x\n", static_cast<int>(csraddr));
            }
#endif
            return read_csr_fail(status);
    }
}

template <typename STATE_ACCESS>
static bool write_csr_sstatus(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    uint64_t mstatus = a.read_mstatus(s);
    a.write_mstatus(s, (mstatus & ~SSTATUS_WRITE_MASK) | (val & SSTATUS_WRITE_MASK));
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_sie(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    uint64_t mask = a.read_mideleg(s);
    uint64_t mie = a.read_mie(s);
    a.write_mie(s, (mie & ~mask) | (val & mask));
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_stvec(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_stvec(s, val & ~3);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_scounteren(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_scounteren(s, val & COUNTEREN_MASK);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_sscratch(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_sscratch(s, val);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_sepc(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_sepc(s, val & ~3);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_scause(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_scause(s, val);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_stval(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_stval(s, val);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_sip(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    uint64_t mask = a.read_mideleg(s);
    uint64_t mip = a.read_mip(s);
    a.write_mip(s, (mip & ~mask) | (val & mask));
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_satp(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    uint64_t satp = a.read_satp(s);
    int mode = satp >> 60;
    int new_mode = (val >> 60) & 0xf;
    if (new_mode == 0 || (new_mode >= 8 && new_mode <= 9))
        mode = new_mode;
    // no ASID implemented
    a.write_satp(s, (val & (((uint64_t)1 << 44) - 1)) | ((uint64_t)mode << 60));
    // Since MMU configuration was changted, flush the TLBs
    // This does not need to be done within the blockchain
    tlb_flush_all(s);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_mstatus(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_mstatus(s, val);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_medeleg(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    const uint64_t mask = (1 << (CAUSE_STORE_AMO_PAGE_FAULT + 1)) - 1;
    a.write_medeleg(s, (a.read_medeleg(s) & ~mask) | (val & mask));
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_mideleg(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    const uint64_t mask = MIP_SSIP | MIP_STIP | MIP_SEIP;
    a.write_mideleg(s, (a.read_mideleg(s) & ~mask) | (val & mask));
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_mie(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    const uint64_t mask = MIP_MSIP | MIP_MTIP | MIP_SSIP | MIP_STIP | MIP_SEIP;
    a.write_mie(s, (a.read_mie(s) & ~mask) | (val & mask));
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_mtvec(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_mtvec(s, val & ~3);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_mcounteren(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_mcounteren(s, val & COUNTEREN_MASK);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_minstret(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_minstret(s, val-1); // The value will be incremented after the instruction is executed
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_mcycle(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_mcycle(s, val-1); // The value will be incremented after the instruction is executed
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_mscratch(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_mscratch(s, val);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_mepc(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_mepc(s, val & ~3);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_mcause(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_mcause(s, val);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_mtval(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    a.write_mtval(s, val);
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr_mip(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
    (void) csraddr;
    const uint64_t mask = MIP_SSIP | MIP_STIP;
    a.write_mip(s, (a.read_mip(s) & ~mask) | (val & mask));
    return true;
}

template <typename STATE_ACCESS>
static bool write_csr(STATE_ACCESS a, RISCVCPUState *s, CSR_address csraddr, uint64_t val) {
#if defined(DUMP_CSR)
    fprintf(stderr, "csr_write: csr=0x%03x val=0x", static_cast<int>(csraddr));
    print_uint64_t(val);
    fprintf(stderr, "\n");
#endif
    if (csr_is_read_only(csraddr)) return false;
    if (csr_priv(csraddr) > s->iflags_PRV) return false;

    switch(csraddr) {
        case CSR_address::sstatus: return write_csr_sstatus(a, s, csraddr, val);
        case CSR_address::sie: return write_csr_sie(a, s, csraddr, val);
        case CSR_address::stvec: return write_csr_stvec(a, s, csraddr, val);
        case CSR_address::scounteren: return write_csr_scounteren(a, s, csraddr, val);

        case CSR_address::sscratch: return write_csr_sscratch(a, s, csraddr, val);
        case CSR_address::sepc: return write_csr_sepc(a, s, csraddr, val);
        case CSR_address::scause: return write_csr_scause(a, s, csraddr, val);
        case CSR_address::stval: return write_csr_stval(a, s, csraddr, val);
        case CSR_address::sip: return write_csr_sip(a, s, csraddr, val);

        case CSR_address::satp: return write_csr_satp(a, s, csraddr, val);

        case CSR_address::mstatus: return write_csr_mstatus(a, s, csraddr, val);
        case CSR_address::medeleg: return write_csr_medeleg(a, s, csraddr, val);
        case CSR_address::mideleg: return write_csr_mideleg(a, s, csraddr, val);
        case CSR_address::mie: return write_csr_mie(a, s, csraddr, val);
        case CSR_address::mtvec: return write_csr_mtvec(a, s, csraddr, val);
        case CSR_address::mcounteren: return write_csr_mcounteren(a, s, csraddr, val);

        case CSR_address::mscratch: return write_csr_mscratch(a, s, csraddr, val);
        case CSR_address::mepc: return write_csr_mepc(a, s, csraddr, val);
        case CSR_address::mcause: return write_csr_mcause(a, s, csraddr, val);
        case CSR_address::mtval: return write_csr_mtval(a, s, csraddr, val);
        case CSR_address::mip: return write_csr_mip(a, s, csraddr, val);

        case CSR_address::mcycle: return write_csr_mcycle(a, s, csraddr, val);
        case CSR_address::minstret: return write_csr_minstret(a, s, csraddr, val);

        // Ignore writes
        case CSR_address::misa:
        case CSR_address::tselect:
        case CSR_address::tdata1:
        case CSR_address::tdata2:
        case CSR_address::tdata3:
            return true;

        // Invalid CSRs
        default:
        //case CSR_address::ucycle: // read-only
        //case CSR_address::utime: // read-only
        //case CSR_address::uinstret: // read-only
        //case CSR_address::ustatus: // no U-mode traps
        //case CSR_address::uie: // no U-mode traps
        //case CSR_address::utvec: // no U-mode traps
        //case CSR_address::uscratch: // no U-mode traps
        //case CSR_address::uepc: // no U-mode traps
        //case CSR_address::ucause: // no U-mode traps
        //case CSR_address::utval: // no U-mode traps
        //case CSR_address::uip: // no U-mode traps
        //case CSR_address::ucycleh: // 32-bit only
        //case CSR_address::utimeh: // 32-bit only
        //case CSR_address::uinstreth: // 32-bit only
        //case CSR_address::sedeleg: // no U-mode traps
        //case CSR_address::sideleg: // no U-mode traps
        //case CSR_address::mvendorid: // read-only
        //case CSR_address::marchid: // read-only
        //case CSR_address::mimplid: // read-only
        //case CSR_address::mhartid: // read-only
        //case CSR_address::mcycleh: // 32-bit only
        //case CSR_address::minstreth: // 32-bit only
#ifdef DUMP_INVALID_CSR
            fprintf(stderr, "csr_write: invalid CSR=0x%x\n", static_cast<int>(csraddr));
#endif
            return false;
    }
}

template <typename STATE_ACCESS, typename RS1VAL>
static inline bool execute_csr_RW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn, const RS1VAL &rs1val) {
    CSR_address csraddr = static_cast<CSR_address>(insn_I_uimm(insn));
    // Try to read old CSR value
    bool status = true;
    uint64_t csrval = 0;
    // If rd=r0, we do not read from the CSR to avoid side-effects
    uint32_t rd = insn_rd(insn);
    if (rd != 0)
        csrval = read_csr(a, s, csraddr, &status);
    if (!status)
        return execute_illegal_insn_exception(a, s, pc, insn);
    // Try to write new CSR value
    //??D When we optimize the inner interpreter loop, we
    //    will have to check if there was a change to the
    //    memory manager and report back from here so we
    //    break out of the inner loop
    if (!write_csr(a, s, csraddr, rs1val(a, s, insn)))
        return execute_illegal_insn_exception(a, s, pc, insn);
    if (rd != 0)
        a.write_register(s, rd, csrval);
    return execute_next_insn(a, s, pc);

}

template <typename STATE_ACCESS>
static inline bool execute_CSRRW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "CSRRW");
    return execute_csr_RW(a, s, pc, insn,
        [](STATE_ACCESS a, RISCVCPUState *s, uint32_t insn) -> uint64_t { return a.read_register(s, insn_rs1(insn)); }
    );
}

template <typename STATE_ACCESS>
static inline bool execute_CSRRWI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "CSRRWI");
    return execute_csr_RW(a, s, pc, insn,
        [](STATE_ACCESS, RISCVCPUState *, uint32_t insn) -> uint64_t { return static_cast<uint64_t>(insn_rs1(insn)); }
    );
}

template <typename STATE_ACCESS, typename F>
static inline bool execute_csr_SC(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn, const F &f) {
    CSR_address csraddr = static_cast<CSR_address>(insn_I_uimm(insn));
    // Try to read old CSR value
    bool status = false;
    uint64_t csrval = read_csr(a, s, csraddr, &status);
    if (!status)
        return execute_illegal_insn_exception(a, s, pc, insn);
    // Load value of rs1 before potentially overwriting it
    // with the value of the csr when rd=rs1
    uint32_t rs1 = insn_rs1(insn);
    uint64_t rs1val = a.read_register(s, rs1);
    uint32_t rd = insn_rd(insn);
    if (rd != 0)
        a.write_register(s, rd, csrval);
    if (rs1 != 0) {
        //??D When we optimize the inner interpreter loop, we
        //    will have to check if there was a change to the
        //    memory manager and report back from here so we
        //    break out of the inner loop
        if (!write_csr(a, s, csraddr, f(csrval, rs1val)))
            return execute_illegal_insn_exception(a, s, pc, insn);
    }
    return execute_next_insn(a, s, pc);
}

template <typename STATE_ACCESS>
static inline bool execute_CSRRS(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "CSRRS");
    return execute_csr_SC(a, s, pc, insn, [](uint64_t csr, uint64_t rs1) -> uint64_t { return csr | rs1; });
}

template <typename STATE_ACCESS>
static inline bool execute_CSRRC(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "CSRRC");
    return execute_csr_SC(a, s, pc, insn, [](uint64_t csr, uint64_t rs1) -> uint64_t {
        return csr & ~rs1;
    });
}

template <typename STATE_ACCESS, typename F>
static inline bool execute_csr_SCI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn, const F &f) {
    CSR_address csraddr = static_cast<CSR_address>(insn_I_uimm(insn));
    // Try to read old CSR value
    bool status = false;
    uint64_t csrval = read_csr(a, s, csraddr, &status);
    if (!status)
        return execute_illegal_insn_exception(a, s, pc, insn);
    uint32_t rd = insn_rd(insn);
    if (rd != 0)
        a.write_register(s, rd, csrval);
    uint32_t rs1 = insn_rs1(insn);
    if (rs1 != 0) {
        //??D When we optimize the inner interpreter loop, we
        //    will have to check if there was a change to the
        //    memory manager and report back from here so we
        //    break out of the inner loop
        if (!write_csr(a, s, csraddr, f(csrval, rs1)))
            return execute_illegal_insn_exception(a, s, pc, insn);
    }
    return execute_next_insn(a, s, pc);
}

template <typename STATE_ACCESS>
static inline bool execute_CSRRSI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "CSRRSI");
    return execute_csr_SCI(a, s, pc, insn, [](uint64_t csr, uint32_t rs1) -> uint64_t { return csr | rs1; });
}

template <typename STATE_ACCESS>
static inline bool execute_CSRRCI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "CSRRCI");
    return execute_csr_SCI(a, s, pc, insn, [](uint64_t csr, uint32_t rs1) -> uint64_t { return csr & ~rs1; });
}

template <typename STATE_ACCESS>
static inline bool execute_ECALL(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    (void) a;
    dump_insn(s, pc, insn, "ECALL");
    //??D Need another version of raise_exception that does not modify mtval
    raise_exception(s, CAUSE_ECALL + s->iflags_PRV, s->mtval);
    return true;
}

template <typename STATE_ACCESS>
static inline bool execute_EBREAK(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    (void) a;
    dump_insn(s, pc, insn, "EBREAK");
    //??D Need another version of raise_exception that does not modify mtval
    raise_exception(s, CAUSE_BREAKPOINT, s->mtval);
    return true;
}

template <typename STATE_ACCESS>
static inline bool execute_URET(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "URET"); // no U-mode traps
    return execute_illegal_insn_exception(a, s, pc, insn);
}

template <typename STATE_ACCESS>
static inline bool execute_SRET(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SRET");
    if (s->iflags_PRV < PRV_S || (s->iflags_PRV == PRV_S && (s->mstatus & MSTATUS_TSR))) {
        return execute_illegal_insn_exception(a, s, pc, insn);
    } else {
        int spp = (s->mstatus >> MSTATUS_SPP_SHIFT) & 1;
        /* set the IE state to previous IE state */
        int spie = (s->mstatus >> MSTATUS_SPIE_SHIFT) & 1;
        s->mstatus = (s->mstatus & ~(1 << MSTATUS_SIE_SHIFT)) |
            (spie << MSTATUS_SIE_SHIFT);
        /* set SPIE to 1 */
        s->mstatus |= MSTATUS_SPIE;
        /* set SPP to U */
        s->mstatus &= ~MSTATUS_SPP;
        set_priv(s, spp);
        s->pc = s->sepc;
        s->iflags_B = true;
        return true;
    }
}

template <typename STATE_ACCESS>
static inline bool execute_MRET(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "MRET");
    if (s->iflags_PRV < PRV_M) {
        return execute_illegal_insn_exception(a, s, pc, insn);
    } else {
        int mpp = (s->mstatus >> MSTATUS_MPP_SHIFT) & 3;
        /* set the IE state to previous IE state */
        int mpie = (s->mstatus >> MSTATUS_MPIE_SHIFT) & 1;
        s->mstatus = (s->mstatus & ~(1 << MSTATUS_MIE_SHIFT)) |
            (mpie << MSTATUS_MIE_SHIFT);
        /* set MPIE to 1 */
        s->mstatus |= MSTATUS_MPIE;
        /* set MPP to U */
        s->mstatus &= ~MSTATUS_MPP;
        set_priv(s, mpp);
        s->pc = s->mepc;
        s->iflags_B = true;
        return true;
    }
}

template <typename STATE_ACCESS>
static inline bool execute_WFI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "WFI");
    if (s->iflags_PRV == PRV_U || (s->iflags_PRV == PRV_S && (s->mstatus & MSTATUS_TW)))
        return execute_illegal_insn_exception(a, s, pc, insn);
    // Go to power down if no enabled interrupts are pending
    if ((s->mip & s->mie) == 0) {
        s->iflags_I = true;
        s->iflags_B = true;
    }
    return execute_next_insn(a, s, pc);
}

template <typename STATE_ACCESS>
static inline bool execute_FENCE(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    (void) insn;
    dump_insn(s, pc, insn, "FENCE");
    // Really do nothing
    return execute_next_insn(a, s, pc);
}

template <typename STATE_ACCESS>
static inline bool execute_FENCE_I(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    (void) insn;
    dump_insn(s, pc, insn, "FENCE_I");
    // Really do nothing
    return execute_next_insn(a, s, pc);
}

template <typename STATE_ACCESS, typename F>
static inline bool execute_arithmetic(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn, const F &f) {
    uint32_t rd = insn_rd(insn);
    if (rd != 0) {
        // Ensure rs1 and rs2 are loaded in order: do not nest with call to f() as
        // the order of evaluation of arguments in a function call is undefined.
        uint64_t rs1 = a.read_register(s, insn_rs1(insn));
        uint64_t rs2 = a.read_register(s, insn_rs2(insn));
        // Now we can safely invoke f()
        a.write_register(s, rd, f(rs1, rs2));
    }
    return execute_next_insn(a, s, pc);
}

template <typename STATE_ACCESS>
static inline bool execute_ADD(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "ADD");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        uint64_t val = 0;
        __builtin_add_overflow(rs1, rs2, &val);
        return val;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SUB(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SUB");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        uint64_t val = 0;
        __builtin_sub_overflow(rs1, rs2, &val);
        return val;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SLL(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SLL");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        return rs1 << (rs2 & (XLEN-1));
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SLT(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SLT");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        return static_cast<int64_t>(rs1) < static_cast<int64_t>(rs2);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SLTU(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SLTU");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        return rs1 < rs2;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_XOR(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "XOR");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        return rs1 ^ rs2;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SRL(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SRL");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        return rs1 >> (rs2 & (XLEN-1));
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SRA(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SRA");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        return static_cast<uint64_t>(static_cast<int64_t>(rs1) >> (rs2 & (XLEN-1)));
    });
}

template <typename STATE_ACCESS>
static inline bool execute_OR(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "OR");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        return rs1 | rs2;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_AND(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AND");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        return rs1 & rs2;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_MUL(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "MUL");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        int64_t srs1 = static_cast<int64_t>(rs1);
        int64_t srs2 = static_cast<int64_t>(rs2);
        int64_t val = 0;
        __builtin_mul_overflow(srs1, srs2, &val);
        return static_cast<uint64_t>(val);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_MULH(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "MULH");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        int64_t srs1 = static_cast<int64_t>(rs1);
        int64_t srs2 = static_cast<int64_t>(rs2);
        return static_cast<uint64_t>((static_cast<int128_t>(srs1) * static_cast<int128_t>(srs2)) >> 64);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_MULHSU(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "MULHSU");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        int64_t srs1 = static_cast<int64_t>(rs1);
        return static_cast<uint64_t>((static_cast<int128_t>(srs1) * static_cast<int128_t>(rs2)) >> 64);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_MULHU(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "MULHU");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        return static_cast<uint64_t>((static_cast<int128_t>(rs1) * static_cast<int128_t>(rs2)) >> 64);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_DIV(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "DIV");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        int64_t srs1 = static_cast<int64_t>(rs1);
        int64_t srs2 = static_cast<int64_t>(rs2);
        if (srs2 == 0) {
            return static_cast<uint64_t>(-1);
        } else if (srs1 == ((int64_t)1 << (XLEN - 1)) && srs2 == -1) {
            return static_cast<uint64_t>(srs1);
        } else {
            return static_cast<uint64_t>(srs1 / srs2);
        }
    });
}

template <typename STATE_ACCESS>
static inline bool execute_DIVU(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "DIVU");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        if (rs2 == 0) {
            return static_cast<uint64_t>(-1);
        } else {
            return rs1 / rs2;
        }
    });
}

template <typename STATE_ACCESS>
static inline bool execute_REM(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "REM");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        int64_t srs1 = static_cast<int64_t>(rs1);
        int64_t srs2 = static_cast<int64_t>(rs2);
        if (srs2 == 0) {
            return srs1;
        } else if (srs1 == ((int64_t)1 << (XLEN - 1)) && srs2 == -1) {
            return 0;
        } else {
            return static_cast<uint64_t>(srs1 % srs2);
        }
    });
}

template <typename STATE_ACCESS>
static inline bool execute_REMU(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "REMU");
    return execute_arithmetic(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> uint64_t {
        if (rs2 == 0) {
            return rs1;
        } else {
            return rs1 % rs2;
        }
    });
}

template <typename STATE_ACCESS, typename F>
static inline bool execute_arithmetic_immediate(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn, const F &f) {
    uint32_t rd = insn_rd(insn);
    if (rd != 0) {
        uint64_t rs1 = a.read_register(s, insn_rs1(insn));
        int32_t imm = insn_I_imm(insn);
        a.write_register(s, rd, f(rs1, imm));
    }
    return execute_next_insn(a, s, pc);
}

template <typename STATE_ACCESS>
static inline bool execute_SRLI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SRLI");
    return execute_arithmetic_immediate(a, s, pc, insn, [](uint64_t rs1, int32_t imm) -> uint64_t {
        return rs1 >> (imm & (XLEN - 1));
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SRAI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SRAI");
    return execute_arithmetic_immediate(a, s, pc, insn, [](uint64_t rs1, int32_t imm) -> uint64_t {
        return static_cast<uint64_t>(static_cast<int64_t>(rs1) >> (imm & (XLEN - 1)));
    });
}

template <typename STATE_ACCESS>
static inline bool execute_ADDI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "ADDI");
    return execute_arithmetic_immediate(a, s, pc, insn, [](uint64_t rs1, int32_t imm) -> uint64_t {
        return rs1+imm;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SLTI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SLTI");
    return execute_arithmetic_immediate(a, s, pc, insn, [](uint64_t rs1, int32_t imm) -> uint64_t {
        return static_cast<int64_t>(rs1) < static_cast<int64_t>(imm);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SLTIU(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SLTIU");
    return execute_arithmetic_immediate(a, s, pc, insn, [](uint64_t rs1, int32_t imm) -> uint64_t {
        return rs1 < static_cast<uint64_t>(imm);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_XORI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "XORI");
    return execute_arithmetic_immediate(a, s, pc, insn, [](uint64_t rs1, int32_t imm) -> uint64_t {
        return rs1 ^ imm;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_ORI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "ORI");
    return execute_arithmetic_immediate(a, s, pc, insn, [](uint64_t rs1, int32_t imm) -> uint64_t {
        return rs1 | imm;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_ANDI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "ANDI");
    return execute_arithmetic_immediate(a, s, pc, insn, [](uint64_t rs1, int32_t imm) -> uint64_t {
        return rs1 & imm;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SLLI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    if ((insn & (0b111111 << 26)) == 0) {
        dump_insn(s, pc, insn, "SLLI");
        return execute_arithmetic_immediate(a, s, pc, insn, [](uint64_t rs1, int32_t imm) -> uint64_t {
            // No need to mask lower 6 bits in imm because of the if condition a above
            // We do it anyway here to prevent problems if this code is moved
            return rs1 << (imm & 0b111111);
        });
    } else {
        return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

template <typename STATE_ACCESS>
static inline bool execute_ADDIW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "ADDIW");
    return execute_arithmetic_immediate(a, s, pc, insn, [](uint64_t rs1, int32_t imm) -> uint64_t {
        return static_cast<uint64_t>(static_cast<int32_t>(rs1) + imm);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SLLIW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    if (insn_funct7(insn) == 0) {
        dump_insn(s, pc, insn, "SLLIW");
        return execute_arithmetic_immediate(a, s, pc, insn, [](uint64_t rs1, int32_t imm) -> uint64_t {
            // No need to mask lower 5 bits in imm because of the if condition a above
            // We do it anyway here to prevent problems if this code is moved
            int32_t rs1w = static_cast<int32_t>(rs1) << (imm & 0b11111);
            return static_cast<uint64_t>(rs1w);
        });
    } else {
        return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

template <typename STATE_ACCESS>
static inline bool execute_SRLIW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SRLIW");
    return execute_arithmetic_immediate(a, s, pc, insn, [](uint64_t rs1, int32_t imm) -> uint64_t {
        // No need to mask lower 5 bits in imm because of funct7 test in caller
        // We do it anyway here to prevent problems if this code is moved
        int32_t rs1w = static_cast<int32_t>(static_cast<uint32_t>(rs1) >> (imm & 0b11111));
        return static_cast<uint64_t>(rs1w);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_SRAIW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SRAIW");
    return execute_arithmetic_immediate(a, s, pc, insn, [](uint64_t rs1, int32_t imm) -> uint64_t {
        int32_t rs1w = static_cast<int32_t>(rs1) >> (imm & 0b11111);
        return static_cast<uint64_t>(rs1w);
    });
}

template <typename T, typename STATE_ACCESS>
static inline bool execute_S(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    uint64_t addr = a.read_register(s, insn_rs1(insn));
    int32_t imm = insn_S_imm(insn);
    uint64_t val = a.read_register(s, insn_rs2(insn));
    if (write_memory<T>(s, addr+imm, val)) {
        return execute_next_insn(a, s, pc);
    } else {
        return execute_raised_exception(a, s, pc);
    }
}

template <typename STATE_ACCESS>
static inline bool execute_SB(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SB");
    return execute_S<uint8_t>(a, s, pc, insn);
}

template <typename STATE_ACCESS>
static inline bool execute_SH(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SH");
    return execute_S<uint16_t>(a, s, pc, insn);
}

template <typename STATE_ACCESS>
static inline bool execute_SW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SW");
    return execute_S<uint32_t>(a, s, pc, insn);
}

template <typename STATE_ACCESS>
static inline bool execute_SD(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "SD");
    return execute_S<uint64_t>(a, s, pc, insn);
}

template <typename T, typename STATE_ACCESS>
static inline bool execute_L(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    uint64_t addr = a.read_register(s, insn_rs1(insn));
    int32_t imm = insn_I_imm(insn);
    T val;
    if (read_memory<T>(s, addr+imm, &val)) {
        // This static branch is eliminated by the compiler
        if (std::is_signed<T>::value) {
            a.write_register(s, insn_rd(insn), static_cast<int64_t>(val));
        } else {
            a.write_register(s, insn_rd(insn), static_cast<uint64_t>(val));
        }
        return execute_next_insn(a, s, pc);
    } else {
        return execute_raised_exception(a, s, pc);
    }
}

template <typename STATE_ACCESS>
static inline bool execute_LB(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "LB");
    return execute_L<int8_t>(a, s, pc, insn);
}

template <typename STATE_ACCESS>
static inline bool execute_LH(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "LH");
    return execute_L<int16_t>(a, s, pc, insn);
}

template <typename STATE_ACCESS>
static inline bool execute_LW(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "LW");
    return execute_L<int32_t>(a, s, pc, insn);
}

template <typename STATE_ACCESS>
static inline bool execute_LD(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "LD");
    return execute_L<int64_t>(a, s, pc, insn);
}

template <typename STATE_ACCESS>
static inline bool execute_LBU(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "LBU");
    return execute_L<uint8_t>(a, s, pc, insn);
}

template <typename STATE_ACCESS>
static inline bool execute_LHU(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "LHU");
    return execute_L<uint16_t>(a, s, pc, insn);
}

template <typename STATE_ACCESS>
static inline bool execute_LWU(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "LWU");
    return execute_L<uint32_t>(a, s, pc, insn);
}

template <typename STATE_ACCESS, typename F>
static inline bool execute_branch(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn, const F &f) {
    uint64_t rs1 = a.read_register(s, insn_rs1(insn));
    uint64_t rs2 = a.read_register(s, insn_rs2(insn));
    if (f(rs1, rs2)) {
        uint64_t new_pc = (int64_t)(pc + insn_B_imm(insn));
        if (new_pc & 3) {
            return execute_misaligned_fetch_exception(a, s, new_pc);
        } else {
            return execute_jump(a, s, new_pc);
        }
    }
    return execute_next_insn(a, s, pc);
}

template <typename STATE_ACCESS>
static inline bool execute_BEQ(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "BEQ");
    return execute_branch(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> bool { return rs1 == rs2; });
}


template <typename STATE_ACCESS>
static inline bool execute_BNE(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "BNE");
    return execute_branch(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> bool { return rs1 != rs2; });
}

template <typename STATE_ACCESS>
static inline bool execute_BLT(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "BLT");
    return execute_branch(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> bool {
        return static_cast<int64_t>(rs1) < static_cast<int64_t>(rs2);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_BGE(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "BGE");
    return execute_branch(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> bool {
        return static_cast<int64_t>(rs1) >= static_cast<int64_t>(rs2);
    });
}

template <typename STATE_ACCESS>
static inline bool execute_BLTU(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "BLTU");
    return execute_branch(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> bool {
        return rs1 < rs2;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_BGEU(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "BGEU");
    return execute_branch(a, s, pc, insn, [](uint64_t rs1, uint64_t rs2) -> bool {
        return rs1 >= rs2;
    });
}

template <typename STATE_ACCESS>
static inline bool execute_LUI(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "LUI");
    uint32_t rd = insn_rd(insn);
    if (rd != 0)
        a.write_register(s, rd, insn_U_imm(insn));
    return execute_next_insn(a, s, pc);
}

template <typename STATE_ACCESS>
static inline bool execute_AUIPC(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "AUIPC");
    uint32_t rd = insn_rd(insn);
    if (rd != 0)
        a.write_register(s, rd, pc + insn_U_imm(insn));
    return execute_next_insn(a, s, pc);
}

template <typename STATE_ACCESS>
static inline bool execute_JAL(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "JAL");
    uint64_t new_pc = pc + insn_J_imm(insn);
    if (new_pc & 3) {
        return execute_misaligned_fetch_exception(a, s, new_pc);
    }
    uint32_t rd = insn_rd(insn);
    if (rd != 0)
        a.write_register(s, rd, pc + 4);
    return execute_jump(a, s, new_pc);
}

template <typename STATE_ACCESS>
static inline bool execute_JALR(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    dump_insn(s, pc, insn, "JALR");
    uint64_t val = pc + 4;
    uint64_t new_pc = (int64_t)(a.read_register(s, insn_rs1(insn)) + insn_I_imm(insn)) & ~1;
    if (new_pc & 3)
        return execute_misaligned_fetch_exception(a, s, new_pc);
    uint32_t rd = insn_rd(insn);
    if (rd != 0)
        a.write_register(s, rd, val);
    return execute_jump(a, s, new_pc);
}

template <typename STATE_ACCESS>
static bool execute_SFENCE_VMA(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    // rs1 and rs2 are arbitrary, rest is set
    if ((insn & 0b11111110000000000111111111111111) == 0b00010010000000000000000001110011) {
        dump_insn(s, pc, insn, "SFENCE_VMA");
        if (s->iflags_PRV == PRV_U ||
            (s->iflags_PRV == PRV_S && (s->mstatus & MSTATUS_TVM)))
            return execute_illegal_insn_exception(a, s, pc, insn);
        uint32_t rs1 = insn_rs1(insn);
        if (rs1 == 0) {
            tlb_flush_all(s);
        } else {
            tlb_flush_vaddr(s, s->reg[rs1]);
        }
        // The current code TLB may have been flushed
        s->iflags_B = true;
        return execute_next_insn(a, s, pc);
    } else {
        return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

/// \brief Executes an instruction of the atomic group.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_atomic_group(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    switch (static_cast<atomic_funct3_funct5>(insn_funct3_funct5(insn))) {
        case atomic_funct3_funct5::LR_W: return execute_LR_W(a, s, pc, insn);
        case atomic_funct3_funct5::SC_W: return execute_SC_W(a, s, pc, insn);
        case atomic_funct3_funct5::AMOSWAP_W: return execute_AMOSWAP_W(a, s, pc, insn);
        case atomic_funct3_funct5::AMOADD_W: return execute_AMOADD_W(a, s, pc, insn);
        case atomic_funct3_funct5::AMOXOR_W: return execute_AMOXOR_W(a, s, pc, insn);
        case atomic_funct3_funct5::AMOAND_W: return execute_AMOAND_W(a, s, pc, insn);
        case atomic_funct3_funct5::AMOOR_W: return execute_AMOOR_W(a, s, pc, insn);
        case atomic_funct3_funct5::AMOMIN_W: return execute_AMOMIN_W(a, s, pc, insn);
        case atomic_funct3_funct5::AMOMAX_W: return execute_AMOMAX_W(a, s, pc, insn);
        case atomic_funct3_funct5::AMOMINU_W: return execute_AMOMINU_W(a, s, pc, insn);
        case atomic_funct3_funct5::AMOMAXU_W: return execute_AMOMAXU_W(a, s, pc, insn);
        case atomic_funct3_funct5::LR_D: return execute_LR_D(a, s, pc, insn);
        case atomic_funct3_funct5::SC_D: return execute_SC_D(a, s, pc, insn);
        case atomic_funct3_funct5::AMOSWAP_D: return execute_AMOSWAP_D(a, s, pc, insn);
        case atomic_funct3_funct5::AMOADD_D: return execute_AMOADD_D(a, s, pc, insn);
        case atomic_funct3_funct5::AMOXOR_D: return execute_AMOXOR_D(a, s, pc, insn);
        case atomic_funct3_funct5::AMOAND_D: return execute_AMOAND_D(a, s, pc, insn);
        case atomic_funct3_funct5::AMOOR_D: return execute_AMOOR_D(a, s, pc, insn);
        case atomic_funct3_funct5::AMOMIN_D: return execute_AMOMIN_D(a, s, pc, insn);
        case atomic_funct3_funct5::AMOMAX_D: return execute_AMOMAX_D(a, s, pc, insn);
        case atomic_funct3_funct5::AMOMINU_D: return execute_AMOMINU_D(a, s, pc, insn);
        case atomic_funct3_funct5::AMOMAXU_D: return execute_AMOMAXU_D(a, s, pc, insn);
        default: return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

/// \brief Executes an instruction of the arithmetic-32 group.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_arithmetic_32_group(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    switch (static_cast<arithmetic_32_funct3_funct7>(insn_funct3_funct7(insn))) {
        case arithmetic_32_funct3_funct7::ADDW: return execute_ADDW(a, s, pc, insn);
        case arithmetic_32_funct3_funct7::SUBW: return execute_SUBW(a, s, pc, insn);
        case arithmetic_32_funct3_funct7::SLLW: return execute_SLLW(a, s, pc, insn);
        case arithmetic_32_funct3_funct7::SRLW: return execute_SRLW(a, s, pc, insn);
        case arithmetic_32_funct3_funct7::SRAW: return execute_SRAW(a, s, pc, insn);
        case arithmetic_32_funct3_funct7::MULW: return execute_MULW(a, s, pc, insn);
        case arithmetic_32_funct3_funct7::DIVW: return execute_DIVW(a, s, pc, insn);
        case arithmetic_32_funct3_funct7::DIVUW: return execute_DIVUW(a, s, pc, insn);
        case arithmetic_32_funct3_funct7::REMW: return execute_REMW(a, s, pc, insn);
        case arithmetic_32_funct3_funct7::REMUW: return execute_REMUW(a, s, pc, insn);
        default: return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

/// \brief Executes an instruction of the shift-rightimmediate-32 group.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_shift_right_immediate_32_group(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    switch (static_cast<shift_right_immediate_32_funct7>(insn_funct7(insn))) {
        case shift_right_immediate_32_funct7::SRLIW: return execute_SRLIW(a, s, pc, insn);
        case shift_right_immediate_32_funct7::SRAIW: return execute_SRAIW(a, s, pc, insn);
        default: return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

/// \brief Executes an instruction of the arithmetic-immediate-32 group.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_arithmetic_immediate_32_group(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    switch (static_cast<arithmetic_immediate_32_funct3>(insn_funct3(insn))) {
        case arithmetic_immediate_32_funct3::ADDIW: return execute_ADDIW(a, s, pc, insn);
        case arithmetic_immediate_32_funct3::SLLIW: return execute_SLLIW(a, s, pc, insn);
        case arithmetic_immediate_32_funct3::shift_right_immediate_32_group:
            return execute_shift_right_immediate_32_group(a, s, pc, insn);
        default: return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

/// \brief Executes an instruction of the environment, trap, interrupt, or memory management groups.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_env_trap_int_mm_group(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    switch (static_cast<env_trap_int_group_insn>(insn)) {
        case env_trap_int_group_insn::ECALL: return execute_ECALL(a, s, pc, insn);
        case env_trap_int_group_insn::EBREAK: return execute_EBREAK(a, s, pc, insn);
        case env_trap_int_group_insn::URET: return execute_URET(a, s, pc, insn);
        case env_trap_int_group_insn::SRET: return execute_SRET(a, s, pc, insn);
        case env_trap_int_group_insn::MRET: return execute_MRET(a, s, pc, insn);
        case env_trap_int_group_insn::WFI: return execute_WFI(a, s, pc, insn);
        default: return execute_SFENCE_VMA(a, s, pc, insn);
    }
}

/// \brief Executes an instruction of the CSR, environment, trap, interrupt, or memory management groups.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_csr_env_trap_int_mm_group(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    switch (static_cast<csr_env_trap_int_mm_funct3>(insn_funct3(insn))) {
        case csr_env_trap_int_mm_funct3::CSRRW: return execute_CSRRW(a, s, pc, insn);
        case csr_env_trap_int_mm_funct3::CSRRS: return execute_CSRRS(a, s, pc, insn);
        case csr_env_trap_int_mm_funct3::CSRRC: return execute_CSRRC(a, s, pc, insn);
        case csr_env_trap_int_mm_funct3::CSRRWI: return execute_CSRRWI(a, s, pc, insn);
        case csr_env_trap_int_mm_funct3::CSRRSI: return execute_CSRRSI(a, s, pc, insn);
        case csr_env_trap_int_mm_funct3::CSRRCI: return execute_CSRRCI(a, s, pc, insn);
        case csr_env_trap_int_mm_funct3::env_trap_int_mm_group:
             return execute_env_trap_int_mm_group(a, s, pc, insn);
        default: return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

/// \brief Executes an instruction of the fence group.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_fence_group(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    if (insn == 0x0000100f) {
        return execute_FENCE_I(a, s, pc, insn);
    } else if (insn & 0xf00fff80) {
        return execute_illegal_insn_exception(a, s, pc, insn);
    } else {
        return execute_FENCE(a, s, pc, insn);
    }
}

/// \brief Executes an instruction of the shift-right-immediate group.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_shift_right_immediate_group(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    switch (static_cast<shift_right_immediate_funct6>(insn_funct6(insn))) {
        case shift_right_immediate_funct6::SRLI: return execute_SRLI(a, s, pc, insn);
        case shift_right_immediate_funct6::SRAI: return execute_SRAI(a, s, pc, insn);
        default: return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

/// \brief Executes an instruction of the arithmetic group.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_arithmetic_group(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    //std::cerr << "funct3_funct7: " << std::bitset<10>(insn_funct3_funct7(insn)) << '\n';
    switch (static_cast<arithmetic_funct3_funct7>(insn_funct3_funct7(insn))) {
        case arithmetic_funct3_funct7::ADD: return execute_ADD(a, s, pc, insn);
        case arithmetic_funct3_funct7::SUB: return execute_SUB(a, s, pc, insn);
        case arithmetic_funct3_funct7::SLL: return execute_SLL(a, s, pc, insn);
        case arithmetic_funct3_funct7::SLT: return execute_SLT(a, s, pc, insn);
        case arithmetic_funct3_funct7::SLTU: return execute_SLTU(a, s, pc, insn);
        case arithmetic_funct3_funct7::XOR: return execute_XOR(a, s, pc, insn);
        case arithmetic_funct3_funct7::SRL: return execute_SRL(a, s, pc, insn);
        case arithmetic_funct3_funct7::SRA: return execute_SRA(a, s, pc, insn);
        case arithmetic_funct3_funct7::OR: return execute_OR(a, s, pc, insn);
        case arithmetic_funct3_funct7::AND: return execute_AND(a, s, pc, insn);
        case arithmetic_funct3_funct7::MUL: return execute_MUL(a, s, pc, insn);
        case arithmetic_funct3_funct7::MULH: return execute_MULH(a, s, pc, insn);
        case arithmetic_funct3_funct7::MULHSU: return execute_MULHSU(a, s, pc, insn);
        case arithmetic_funct3_funct7::MULHU: return execute_MULHU(a, s, pc, insn);
        case arithmetic_funct3_funct7::DIV: return execute_DIV(a, s, pc, insn);
        case arithmetic_funct3_funct7::DIVU: return execute_DIVU(a, s, pc, insn);
        case arithmetic_funct3_funct7::REM: return execute_REM(a, s, pc, insn);
        case arithmetic_funct3_funct7::REMU: return execute_REMU(a, s, pc, insn);
        default: return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

/// \brief Executes an instruction of the arithmetic-immediate group.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_arithmetic_immediate_group(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    switch (static_cast<arithmetic_immediate_funct3>(insn_funct3(insn))) {
        case arithmetic_immediate_funct3::ADDI: return execute_ADDI(a, s, pc, insn);
        case arithmetic_immediate_funct3::SLTI: return execute_SLTI(a, s, pc, insn);
        case arithmetic_immediate_funct3::SLTIU: return execute_SLTIU(a, s, pc, insn);
        case arithmetic_immediate_funct3::XORI: return execute_XORI(a, s, pc, insn);
        case arithmetic_immediate_funct3::ORI: return execute_ORI(a, s, pc, insn);
        case arithmetic_immediate_funct3::ANDI: return execute_ANDI(a, s, pc, insn);
        case arithmetic_immediate_funct3::SLLI: return execute_SLLI(a, s, pc, insn);
        case arithmetic_immediate_funct3::shift_right_immediate_group:
            return execute_shift_right_immediate_group(a, s, pc, insn);
        default: return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

/// \brief Executes an instruction of the store group.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_store_group(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    switch (static_cast<store_funct3>(insn_funct3(insn))) {
        case store_funct3::SB: return execute_SB(a, s, pc, insn);
        case store_funct3::SH: return execute_SH(a, s, pc, insn);
        case store_funct3::SW: return execute_SW(a, s, pc, insn);
        case store_funct3::SD: return execute_SD(a, s, pc, insn);
        default: return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

/// \brief Executes an instruction of the load group.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_load_group(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    switch (static_cast<load_funct3>(insn_funct3(insn))) {
        case load_funct3::LB: return execute_LB(a, s, pc, insn);
        case load_funct3::LH: return execute_LH(a, s, pc, insn);
        case load_funct3::LW: return execute_LW(a, s, pc, insn);
        case load_funct3::LD: return execute_LD(a, s, pc, insn);
        case load_funct3::LBU: return execute_LBU(a, s, pc, insn);
        case load_funct3::LHU: return execute_LHU(a, s, pc, insn);
        case load_funct3::LWU: return execute_LWU(a, s, pc, insn);
        default: return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

/// \brief Executes an instruction of the branch group.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_branch_group(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
    switch (static_cast<branch_funct3>(insn_funct3(insn))) {
        case branch_funct3::BEQ: return execute_BEQ(a, s, pc, insn);
        case branch_funct3::BNE: return execute_BNE(a, s, pc, insn);
        case branch_funct3::BLT: return execute_BLT(a, s, pc, insn);
        case branch_funct3::BGE: return execute_BGE(a, s, pc, insn);
        case branch_funct3::BLTU: return execute_BLTU(a, s, pc, insn);
        case branch_funct3::BGEU: return execute_BGEU(a, s, pc, insn);
        default: return execute_illegal_insn_exception(a, s, pc, insn);
    }
}

/// \brief Executes an instruction.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Current pc.
/// \param insn Instruction.
/// \return Returns true if the execution completed, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static inline bool execute_insn(STATE_ACCESS a, RISCVCPUState *s, uint64_t pc, uint32_t insn) {
//std::cerr << "insn: " << std::bitset<32>(insn) << '\n';
//??D We should probably try doing the first branch on the combined opcode, funct3, and funct7.
//    Maybe it reduces the number of levels needed to decode most instructions.
    switch (static_cast<opcode>(insn_opcode(insn))) {
        case opcode::LUI: return execute_LUI(a, s, pc, insn);
        case opcode::AUIPC: return execute_AUIPC(a, s, pc, insn);
        case opcode::JAL: return execute_JAL(a, s, pc, insn);
        case opcode::JALR: return execute_JALR(a, s, pc, insn);
        case opcode::branch_group: return execute_branch_group(a, s, pc, insn);
        case opcode::load_group: return execute_load_group(a, s, pc, insn);
        case opcode::store_group: return execute_store_group(a, s, pc, insn);
        case opcode::arithmetic_immediate_group: return execute_arithmetic_immediate_group(a, s, pc, insn);
        case opcode::arithmetic_group: return execute_arithmetic_group(a, s, pc, insn);
        case opcode::fence_group: return execute_fence_group(a, s, pc, insn);
        case opcode::csr_env_trap_int_mm_group: return execute_csr_env_trap_int_mm_group(a, s, pc, insn);
        case opcode::arithmetic_immediate_32_group: return execute_arithmetic_immediate_32_group(a, s, pc, insn);
        case opcode::arithmetic_32_group: return execute_arithmetic_32_group(a, s, pc, insn);
        case opcode::atomic_group: return execute_atomic_group(a, s, pc, insn);
        default: return execute_illegal_insn_exception(a, s, pc, insn);
    }
}


/// \brief Loads the next instruction.
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param pc Receives current pc.
/// \param insn Receives fetched instruction.
/// \return Returns true if load succeeded, false if it caused an exception. In that case, raise the exception.
template <typename STATE_ACCESS>
static bool fetch_insn(STATE_ACCESS a, RISCVCPUState *s, uint64_t *pc, uint32_t *insn) {
    // Get current pc from state
    uint64_t vaddr = a.read_pc(s);
    // Translate pc address from virtual to physical
    // First, check TLB
    int tlb_idx = (vaddr >> PG_SHIFT) & (TLB_SIZE - 1);
    uintptr_t mem_addend;
    // TLB match
    if (s->tlb_code[tlb_idx].vaddr == (vaddr & ~PG_MASK)) {
        mem_addend = s->tlb_code[tlb_idx].mem_addend;
    // TLB miss
    } else {
        uint64_t paddr;
        // Walk page table and obtain the physical address
        if (get_phys_addr(s, &paddr, vaddr, ACCESS_CODE)) {
            raise_exception(s, CAUSE_FETCH_PAGE_FAULT, vaddr);
            return false;
        }
        // Walk memory map to find the range that contains the physical address
        PhysMemoryRange *pr = get_phys_mem_range(s->mem_map, paddr);
        // We only execute directly from RAM (as in "random access memory", which includes ROM)
        // If we are not in RAM or if we are not in any range, we treat this as a PMA violation
        if (!pr || !pr->is_ram) {
            raise_exception(s, CAUSE_FETCH_FAULT, vaddr);
            return false;
        }
        // Update TLB with the new mapping between virtual and physical
        tlb_idx = (vaddr >> PG_SHIFT) & (TLB_SIZE - 1);
        uint8_t *ptr = pr->phys_mem + (uintptr_t)(paddr - pr->addr);
        s->tlb_code[tlb_idx].vaddr = vaddr & ~PG_MASK;
        s->tlb_code[tlb_idx].mem_addend = (uintptr_t)ptr - vaddr;
        mem_addend = s->tlb_code[tlb_idx].mem_addend;
    }

    // Finally load the instruction
    *pc = vaddr;
    *insn = *reinterpret_cast<uint32_t *>(mem_addend + (uintptr_t)vaddr);
    return true;
}

/// \brief Interpreter status code
enum class interpreter_status {
    done, ///< mcycle reached target value
    halted, ///< iflags_H is set, indicating the machine is permanently halted
    idle ///< iflags_I is set, indicating the machine is waiting for an interrupt
};

/// \brief Tries to run the interpreter until mcycle hits a target
/// \tparam STATE_ACCESS Class of CPU state accessor object.
/// \param a CPU state accessor object.
/// \param s CPU state.
/// \param mcycle_end Target value for mcycle.
/// \returns Returns a status code that tells if the loop hit the target mcycle or stopped early.
/// \details The interpret may stop early if the machine halts permanently or becomes temporarily idle (waiting for interrupts).
template <typename STATE_ACCESS>
interpreter_status interpret(STATE_ACCESS a, RISCVCPUState *s, uint64_t mcycle_end) {

    static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
        "code assumes little-endian byte ordering");

    // The external loop continues until the CPU halts,
    // becomes idle, or mcycle reaches mcycle_end
    for ( ;; ) {

        // If the cpu is halted, report it
        if (s->iflags_H) {
            return interpreter_status::halted;
        }

        // If we reached the target mcycle, report it
        if (a.read_mcycle(s) >= mcycle_end) {
            return interpreter_status::done;
        }

        // The idle flag is set if there were no pending interrupts when the machine executed a WFI instruction.
        // Any attempt to externally set a pending interrupt clears the idle flag.
        // Finding it set, there is nothing else to do and we simply report it back to the callee.
        if (s->iflags_I) {
            return interpreter_status::idle;
        }

        // If the break flag is set, try to raise the interrupts and reset the flag.
        if (s->iflags_B) {
            s->iflags_B = false;
            raise_interrupt(s);
        }

        uint64_t pc = 0;
        uint32_t insn = 0;

        // The inner loops continues until there is an interrupt condition
        // or mcycle reaches mcycle_end
        for ( ;; )  {
            // Try to fetch the next instruction
            if (fetch_insn(a, s, &pc, &insn)) {
                // Try to execute it
                if (execute_insn(a, s, pc, insn)) {
                    // If successful, increment the number of retired instructions minstret
                    // WARNING: if an instruction modifies minstret, it needs to take into
                    // account it this unconditional increment and set the value accordingly
                    a.write_minstret(s, a.read_minstret(s)+1);
                }
            }
            // Increment the cycle counter mcycle
            // WARNING: if an instruction modifies mcycle, it needs to take into
            // account it this unconditional increment and set the value accordingly
            uint64_t mcycle = a.read_mcycle(s) + 1;
            a.write_mcycle(s, mcycle);
            // If the break flag is active, break from the inner loop.
            // This will give the outer loop an opportunity to handle it.
            if (s->iflags_B) {
                break;
            }
            // If we reached the target mcycle, we are done
            if (mcycle >= mcycle_end) {
                return interpreter_status::done;
            }
        }
    }
}

void riscv_cpu_run(RISCVCPUState *s, uint64_t mcycle_end) {
    state_access a;
    interpret(a, s, mcycle_end);
}
