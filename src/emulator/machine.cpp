/*
 * VM utilities
 *
 * Copyright (c) 2017 Fabrice Bellard
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
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>

#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

extern "C" {
#include <libfdt.h>
}

#include <lua.hpp>

#include "iomem.h"
#include "riscv_cpu.h"

#include "machine.h"

#define Ki(n) (((uint64_t)n) << 10)
#define Mi(n) (((uint64_t)n) << 20)
#define Gi(n) (((uint64_t)n) << 30)

#define ROM_BASE_ADDR  Ki(4)
#define ROM_SIZE       Ki(64)
#define RAM_BASE_ADDR      Gi(2)
#define CLINT_BASE_ADDR    Mi(32)
#define CLINT_SIZE         Ki(768)
#define HTIF_BASE_ADDR     (Gi(1)+Ki(32))
#define HTIF_SIZE             16
#define HTIF_CONSOLE_BUF_SIZE (1024)

#define CLOCK_FREQ 1000000000 /* 1 GHz (arbitrary) */
#define RTC_FREQ_DIV 100      /* This cannot change */

typedef struct {
    int stdin_fd;
    struct termios oldtty;
    int old_fd0_flags;
    uint8_t buf[HTIF_CONSOLE_BUF_SIZE];
    ssize_t buf_len, buf_pos;
    bool char_pending;
} HTIFConsole;

typedef struct RISCVMachine {
    PhysMemoryMap *mem_map;
    RISCVCPUState *cpu_state;
    uint64_t ram_size;
    /* CLINT */
    uint64_t timecmp;
    /* HTIF */
    uint64_t htif_tohost, htif_fromhost;
    HTIFConsole *htif_console;
} RISCVMachine;

/* return -1 if error. */
static uint64_t load_file(uint8_t **pbuf, const char *filename)
{
    FILE *f;
    int size;
    uint8_t *buf;

    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Unable to open %s\n", filename);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = reinterpret_cast<uint8_t *>(calloc(1, size));
    if ((int) fread(buf, 1, size, f) != size) {
        fprintf(stderr, "Unable to read from %s\n", filename);
        return -1;
    }
    fclose(f);
    *pbuf = buf;
    return size;
}

static int optboolean(lua_State *L, int tabidx, const char *field, int def) {
    int val = def;
    lua_getfield(L, tabidx, field);
    if (lua_isboolean(L, -1)) {
        val = lua_toboolean(L, -1);
    } else if (!lua_isnil(L, -1)) {
        luaL_error(L, "Invalid %s (expected Boolean).", field);
    }
    lua_pop(L, 1);
    return val;
}

static uint64_t checkuint(lua_State *L, int tabidx, const char *field) {
    lua_Integer ival;
    lua_getfield(L, tabidx, field);
    if (!lua_isinteger(L, -1))
        luaL_error(L, "Invalid %s (expected unsigned integer).", field);
    ival = lua_tointeger(L, -1);
    lua_pop(L, 1);
    return (uint64_t) ival;
}

static char *dupoptstring(lua_State *L, int tabidx, const char *field) {
    char *val = NULL;
    lua_getfield(L, tabidx, field);
    if (lua_isnil(L, -1)) {
        val = NULL;
    } else if (lua_isstring(L, -1)) {
        val = strdup(lua_tostring(L, -1));
    } else {
        luaL_error(L, "Invalid %s (expected string).", field);
    }
    lua_pop(L, 1);
    return val;
}

static char *dupcheckstring(lua_State *L, int tabidx, const char *field) {
    char *val = NULL;
    lua_getfield(L, tabidx, field);
    if (lua_isstring(L, -1)) {
        val = strdup(lua_tostring(L, -1));
    } else {
        luaL_error(L, "Invalid %s (expected string).", field);
    }
    lua_pop(L, 1);
    return val;
}

void virt_lua_load_config(lua_State *L, VirtMachineParams *p, int tabidx) {

    virt_machine_set_defaults(p);

    if (checkuint(L, tabidx, "version") != VM_CONFIG_VERSION) {
        luaL_error(L, "Emulator does not match version number.");
    }

    lua_getfield(L, tabidx, "machine");
    if (!lua_isstring(L, -1)) {
        luaL_error(L, "No machine string.");
    }
    if (strcmp(virt_machine_get_name(), lua_tostring(L, -1)) != 0) {
        luaL_error(L, "Unsupported machine %s (running machine is %s).",
            lua_tostring(L, -1), virt_machine_get_name());
    }
    lua_pop(L, 1);

    p->ram_size = checkuint(L, tabidx, "memory_size");
    p->ram_size <<= 20;

    p->ram_image.filename = dupcheckstring(L, tabidx, "ram_image");
    p->ram_image.len = load_file(&p->ram_image.buf, p->ram_image.filename);
    if (p->ram_image.len == 0) {
        luaL_error(L, "Unable to load RAM image %s.", p->ram_image.filename);
    }

    p->rom_image.filename = dupoptstring(L, tabidx, "rom_image");
    if (p->rom_image.filename) {
        p->rom_image.len = load_file(&p->rom_image.buf, p->rom_image.filename);
        if (p->rom_image.len == 0) {
            luaL_error(L, "Unable to load ROM image %s.", p->rom_image.filename);
        }
    }

    p->interactive = optboolean(L, -1, "interactive", 0);

    p->cmdline = dupoptstring(L, tabidx, "cmdline");

    for (p->flash_count = 0;
         p->flash_count < VM_MAX_FLASH_DEVICE;
         p->flash_count++) {
        char flash[16];
        snprintf(flash, sizeof(flash), "flash%d", p->flash_count);
        lua_getfield(L, tabidx, flash);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        if (!lua_istable(L, -1)) {
            luaL_error(L, "Invalid flash%d.", p->flash_count);
        }
        p->tab_flash[p->flash_count].shared = optboolean(L, -1, "shared", 0);
        p->tab_flash[p->flash_count].backing = dupcheckstring(L, -1, "backing");
        p->tab_flash[p->flash_count].label = dupcheckstring(L, -1, "label");
        p->tab_flash[p->flash_count].address = checkuint(L, -1, "address");
        p->tab_flash[p->flash_count].size = checkuint(L, -1, "size");
        lua_pop(L, 1);
    }

    if (p->flash_count >= VM_MAX_FLASH_DEVICE) {
        luaL_error(L, "too many flash drives (max is %d)", VM_MAX_FLASH_DEVICE);
    }
}

void virt_machine_free_config(VirtMachineParams *p)
{
    int i;
    free(p->cmdline);
    free(p->ram_image.filename);
    free(p->ram_image.buf);
    free(p->rom_image.filename);
    free(p->rom_image.buf);
    for(i = 0; i < p->flash_count; i++) {
        free(p->tab_flash[i].backing);
        free(p->tab_flash[i].label);
    }
}

static uint64_t rtc_cycles_to_time(uint64_t cycle_counter)
{
    return cycle_counter / RTC_FREQ_DIV;
}

static uint64_t rtc_time_to_cycles(uint64_t time) {
    return time * RTC_FREQ_DIV;
}

static uint64_t rtc_get_time(RISCVMachine *m) {
    return rtc_cycles_to_time(riscv_cpu_get_mcycle(m->cpu_state));
}

/* Host/Target Interface */
static uint32_t htif_read(void *opaque, uint32_t offset,
                          int size_log2)
{
    RISCVMachine *m = reinterpret_cast<RISCVMachine *>(opaque);
    uint32_t val;

    assert(size_log2 == 2);
    switch(offset) {
    case 0:
        val = m->htif_tohost;
        break;
    case 4:
        val = m->htif_tohost >> 32;
        break;
    case 8:
        val = m->htif_fromhost;
        break;
    case 12:
        val = m->htif_fromhost >> 32;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void htif_handle_cmd(RISCVMachine *m)
{
    uint32_t device, cmd;
    uint64_t payload;

    device = m->htif_tohost >> 56;
    cmd = (m->htif_tohost >> 48) & 0xff;
    payload = (m->htif_tohost & (~1ULL >> 16));

#if 0
    printf("HTIF: tohost=0x%016"
        PRIx64 "(%" PRIu32 "):(%" PRIu32 "):(%" PRIu64 ")\n",
        m->htif_tohost, device, cmd, payload);
#endif

    if (device == 0x0 && cmd == 0x0 && payload & 0x1) {
        riscv_cpu_set_shuthost(m->cpu_state, true);
    } else if (device == 0x1 && cmd == 0x1) {
        uint8_t ch = m->htif_tohost & 0xff;
        if (write(1, &ch, 1) < 1) { }
        m->htif_tohost = 0; // notify that we are done with putchar
        m->htif_fromhost = ((uint64_t)device << 56) | ((uint64_t)cmd << 48);
    } else if (device == 0x1 && cmd == 0x0) {
        // request keyboard interrupt
        m->htif_tohost = 0;
    } else {
        printf("HTIF: unsupported tohost=0x%016"
            PRIx64 "(%" PRIu32 "):(%" PRIu32 "):(%" PRIu64 ")\n",
            m->htif_tohost, device, cmd, payload);
    }
}

static void htif_write(void *opaque, uint32_t offset, uint32_t val,
                       int size_log2)
{
    RISCVMachine *m = reinterpret_cast<RISCVMachine *>(opaque);
    assert(size_log2 == 2);
    switch(offset) {
    case 0:
        /* fprintf(stderr, "wrote %u to 0\n", val); */
        m->htif_tohost = (m->htif_tohost & ~0xffffffff) | val;
        break;
    case 4:
        /* fprintf(stderr, "wrote %u to 4\n", val); */
        m->htif_tohost = (m->htif_tohost & 0xffffffff) | ((uint64_t)val << 32);
        htif_handle_cmd(m);
        break;
    case 8:
        m->htif_fromhost = (m->htif_fromhost & ~0xffffffff) | val;
        break;
    case 12:
        m->htif_fromhost = (m->htif_fromhost & 0xffffffff) |
            (uint64_t)val << 32;
        if (m->htif_console) {
            m->htif_console->char_pending = false;
        }
        break;
    default:
        break;
    }
}

/* Clock Interrupt */
static uint32_t clint_read(void *opaque, uint32_t offset, int size_log2)
{
    RISCVMachine *m = reinterpret_cast<RISCVMachine *>(opaque);
    uint32_t val;

    /*??D we should probably enable reads from offset 0,
     * which should return MSIP of HART 0*/
    assert(size_log2 == 2);
    switch(offset) {
    case 0xbff8:
        val = rtc_get_time(m);
        break;
    case 0xbffc:
        val = rtc_get_time(m) >> 32;
        break;
    case 0x4000:
        val = m->timecmp;
        break;
    case 0x4004:
        val = m->timecmp >> 32;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void clint_write(void *opaque, uint32_t offset, uint32_t val,
                      int size_log2)
{
    RISCVMachine *m = reinterpret_cast<RISCVMachine *>(opaque);

    /*??D we should probably enable writes to offset 0,
     * which should modify MSIP of HART 0*/
    assert(size_log2 == 2);
    switch(offset) {
    case 0x4000:
        m->timecmp = (m->timecmp & ~0xffffffff) | val;
        riscv_cpu_reset_mip(m->cpu_state, MIP_MTIP);
        break;
    case 0x4004:
        m->timecmp = (m->timecmp & 0xffffffff) | ((uint64_t)val << 32);
        riscv_cpu_reset_mip(m->cpu_state, MIP_MTIP);
        break;
    default:
        break;
    }
}

static uint8_t *get_ram_ptr(RISCVMachine *m, uint64_t paddr)
{
    PhysMemoryRange *pr = get_phys_mem_range(m->mem_map, paddr);
    if (!pr || !pr->is_ram)
        return NULL;
    return pr->phys_mem + (uintptr_t)(paddr - pr->addr);
}

#define FDT_CHECK(s) do { \
    int err = s; \
    if (err != 0) return err; \
} while (0);

static int fdt_begin_node_num(void *fdt, const char *name, uint64_t num) {
    char name_num[256];
    snprintf(name_num, sizeof(name_num), "%s@%" PRIx64, name, num);
    return fdt_begin_node(fdt, name_num);
}

static int fdt_property_u64_u64(void *fdt, const char *name, uint64_t v0, uint64_t v1) {
    uint32_t tab[4];
    tab[0] = cpu_to_fdt32(v0 >> 32);
    tab[1] = cpu_to_fdt32(v0);
    tab[2] = cpu_to_fdt32(v1 >> 32);
    tab[3] = cpu_to_fdt32(v1);
	return fdt_property(fdt, name, tab, sizeof(tab));
}

static int fdt_build_riscv(const VirtMachineParams *p, const RISCVMachine *m,
    void *buf, int bufsize)
{
    int cur_phandle = 1;
    FDT_CHECK(fdt_create(buf, bufsize));
    FDT_CHECK(fdt_add_reservemap_entry(buf, 0, 0));
    FDT_CHECK(fdt_finish_reservemap(buf));
    FDT_CHECK(fdt_begin_node(buf, ""));
     FDT_CHECK(fdt_property_u32(buf, "#address-cells", 2));
     FDT_CHECK(fdt_property_u32(buf, "#size-cells", 2));
     FDT_CHECK(fdt_property_string(buf, "compatible", "ucbbar,riscvemu-bar_dev"));
     FDT_CHECK(fdt_property_string(buf, "model", "ucbbar,riscvemu-bare"));
     FDT_CHECK(fdt_begin_node(buf, "cpus"));
      FDT_CHECK(fdt_property_u32(buf, "#address-cells", 1));
      FDT_CHECK(fdt_property_u32(buf, "#size-cells", 0));
      FDT_CHECK(fdt_property_u32(buf, "timebase-frequency", CLOCK_FREQ/RTC_FREQ_DIV));
      FDT_CHECK(fdt_begin_node_num(buf, "cpu", 0));
       FDT_CHECK(fdt_property_string(buf, "device_type", "cpu"));
       FDT_CHECK(fdt_property_u32(buf, "reg", 0));
       FDT_CHECK(fdt_property_string(buf, "status", "okay"));
       FDT_CHECK(fdt_property_string(buf, "compatible", "riscv"));
       int max_xlen = riscv_cpu_get_max_xlen(m->cpu_state);
       uint32_t misa = riscv_cpu_get_misa(m->cpu_state);
       char isa_string[128], *q = isa_string;
       q += snprintf(isa_string, sizeof(isa_string), "rv%d", max_xlen);
       for(int i = 0; i < 26; i++) {
           if (misa & (1 << i))
               *q++ = 'a' + i;
       }
       *q = '\0';
       FDT_CHECK(fdt_property_string(buf, "riscv,isa", isa_string));
       FDT_CHECK(fdt_property_string(buf, "mmu-type", "riscv,sv48"));
       FDT_CHECK(fdt_property_u32(buf, "clock-frequency", CLOCK_FREQ));
       FDT_CHECK(fdt_begin_node(buf, "interrupt-controller"));
        FDT_CHECK(fdt_property_u32(buf, "#interrupt-cells", 1));
        FDT_CHECK(fdt_property(buf, "interrupt-controller", NULL, 0));
        FDT_CHECK(fdt_property_string(buf, "compatible", "riscv,cpu-intc"));
        int intc_phandle = cur_phandle++;
        FDT_CHECK(fdt_property_u32(buf, "phandle", intc_phandle));
       FDT_CHECK(fdt_end_node(buf)); /* interrupt-controller */
      FDT_CHECK(fdt_end_node(buf)); /* cpu */
     FDT_CHECK(fdt_end_node(buf)); /* cpus */

     FDT_CHECK(fdt_begin_node_num(buf, "memory", RAM_BASE_ADDR));
      FDT_CHECK(fdt_property_string(buf, "device_type", "memory"));
      FDT_CHECK(fdt_property_u64_u64(buf, "reg", RAM_BASE_ADDR, m->ram_size));
     FDT_CHECK(fdt_end_node(buf)); /* memory */

     /* flash */
     for (int i = 0; i < p->flash_count; i++) {
         FDT_CHECK(fdt_begin_node_num(buf, "flash", p->tab_flash[i].address));
          FDT_CHECK(fdt_property_u32(buf, "#address-cells", 2));
          FDT_CHECK(fdt_property_u32(buf, "#size-cells", 2));
          FDT_CHECK(fdt_property_string(buf, "compatible", "mtd-ram"));
          FDT_CHECK(fdt_property_u32(buf, "bank-width", 4));
          FDT_CHECK(fdt_property_u64_u64(buf, "reg", p->tab_flash[i].address, p->tab_flash[i].size));
          FDT_CHECK(fdt_begin_node_num(buf, "fs0", 0));
           FDT_CHECK(fdt_property_string(buf, "label", p->tab_flash[i].label));
           FDT_CHECK(fdt_property_u64_u64(buf, "reg", 0, p->tab_flash[i].size));
          FDT_CHECK(fdt_end_node(buf)); /* fs */
         FDT_CHECK(fdt_end_node(buf)); /* flash */
     }

     FDT_CHECK(fdt_begin_node(buf, "soc"));
      FDT_CHECK(fdt_property_u32(buf, "#address-cells", 2));
      FDT_CHECK(fdt_property_u32(buf, "#size-cells", 2));
      const char comp[] = "ucbbar,riscvemu-bar-soc\0simple-bus";
      FDT_CHECK(fdt_property(buf, "compatible", comp, sizeof(comp)));
      FDT_CHECK(fdt_property(buf, "ranges", NULL, 0));

      FDT_CHECK(fdt_begin_node_num(buf, "clint", CLINT_BASE_ADDR));
       FDT_CHECK(fdt_property_string(buf, "compatible", "riscv,clint0"));
       uint32_t clint[] = {
	       cpu_to_fdt32(intc_phandle),
	       cpu_to_fdt32(3), /* M IPI irq */
	       cpu_to_fdt32(intc_phandle),
	       cpu_to_fdt32(7) /* M timer irq */
       };
       FDT_CHECK(fdt_property(buf, "interrupts-extended", clint, sizeof(clint)));
       FDT_CHECK(fdt_property_u64_u64(buf, "reg", CLINT_BASE_ADDR, CLINT_SIZE));
      FDT_CHECK(fdt_end_node(buf)); /* clint */

      FDT_CHECK(fdt_begin_node_num(buf, "htif", HTIF_BASE_ADDR));
       FDT_CHECK(fdt_property_string(buf, "compatible", "ucb,htif0"));
       FDT_CHECK(fdt_property_u64_u64(buf, "reg", HTIF_BASE_ADDR, HTIF_SIZE));
       uint32_t htif[] = {
           cpu_to_fdt32(intc_phandle),
           cpu_to_fdt32(13) // X HOST
       };
       FDT_CHECK(fdt_property(buf, "interrupts-extended", htif, sizeof(htif)));
      FDT_CHECK(fdt_end_node(buf));

     FDT_CHECK(fdt_end_node(buf)); /* soc */

     FDT_CHECK(fdt_begin_node(buf, "chosen"));
      FDT_CHECK(fdt_property_string(buf, "bootargs", p->cmdline ? p->cmdline : ""));
     FDT_CHECK(fdt_end_node(buf));

    FDT_CHECK(fdt_end_node(buf)); /* root */
    FDT_CHECK(fdt_finish(buf));

    auto size = fdt_totalsize(buf);

#if 0
    {
        FILE *f;
        f = fopen("emu.dtb", "wb");
        fwrite(buf, 1, size, f);
        fclose(f);
    }
#endif

    return size;
}

static void init_ram_and_rom(const VirtMachineParams *p, RISCVMachine *m)
{

    uint8_t *ram_ptr = get_ram_ptr(m, RAM_BASE_ADDR);
    memcpy(ram_ptr, p->ram_image.buf, std::min(p->ram_image.len, p->ram_size));

    uint8_t *rom_ptr = get_ram_ptr(m, ROM_BASE_ADDR);

    if (!p->rom_image.buf) {
        uint32_t fdt_addr = 8 * 8;
        //??D should check for error here.
        fdt_build_riscv(p, m, rom_ptr + fdt_addr, ROM_SIZE-fdt_addr);
        /* jump_addr = RAM_BASE_ADDR */
        uint32_t *q = (uint32_t *)(rom_ptr);
        /* la t0, jump_addr */
        q[0] = 0x297 + RAM_BASE_ADDR - ROM_BASE_ADDR; /* auipc t0, 0x80000000-0x1000 */
        /* la a1, fdt_addr */
          q[1] = 0x597; /* auipc a1, 0  (a1 := 0x1004) */
          q[2] = 0x58593 + ((fdt_addr - (ROM_BASE_ADDR+4)) << 20); /* addi a1, a1, 60 */
        q[3] = 0xf1402573; /* csrr a0, mhartid */
        q[4] = 0x00028067; /* jr t0 */
    } else {
        memcpy(rom_ptr, p->rom_image.buf, std::min(p->rom_image.len, ROM_SIZE));
    }
}

static void riscv_flush_tlb_write_range(void *opaque, uint8_t *ram_addr,
                                        size_t ram_size)
{
    RISCVMachine *m = reinterpret_cast<RISCVMachine *>(opaque);
    riscv_cpu_flush_tlb_write_range_ram(m->cpu_state, ram_addr, ram_size);
}

void virt_machine_set_defaults(VirtMachineParams *p)
{
    memset(p, 0, sizeof(*p));
}

static HTIFConsole *htif_console_init(void) {
    struct termios tty;
    HTIFConsole *con = reinterpret_cast<HTIFConsole *>(calloc(1, sizeof(*con)));
    memset(&tty, 0, sizeof(tty));
    tcgetattr (0, &tty);
    con->oldtty = tty;
    con->old_fd0_flags = fcntl(0, F_GETFL);
    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
    tty.c_lflag &= ~ISIG;
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    tcsetattr (0, TCSANOW, &tty);
    /* Note: the glibc does not properly test the return value of
       write() in printf, so some messages on stdout may be lost */
    fcntl(con->stdin_fd, F_SETFL, O_NONBLOCK);
    return con;
}

static void htif_console_end(HTIFConsole *con) {
    tcsetattr (0, TCSANOW, &con->oldtty);
    fcntl(0, F_SETFL, con->old_fd0_flags);
    free(con);
}

VirtMachine *virt_machine_init(const VirtMachineParams *p)
{
    int i;

    if (!p->ram_image.buf && !p->rom_image.buf) {
        fprintf(stderr, "No ROM or RAM images\n");
        return NULL;
    }

    if (p->rom_image.buf && p->rom_image.len > ROM_SIZE) {
        fprintf(stderr, "ROM image too big (%d vs %d)\n", (int) p->ram_image.len, (int) ROM_SIZE);
        return NULL;
    }

    if (p->ram_image.len >  p->ram_size) {
        fprintf(stderr, "RAM image too big (%d vs %d)\n", (int) p->ram_image.len, (int) p->ram_size);
        return NULL;
    }

    RISCVMachine *m = reinterpret_cast<RISCVMachine *>(calloc(1, sizeof(*m)));

    m->ram_size = p->ram_size;
    m->mem_map = phys_mem_map_init();
    /* needed to handle the RAM dirty bits */
    m->mem_map->opaque = m;
    m->mem_map->flush_tlb_write_range = riscv_flush_tlb_write_range;

    m->cpu_state = riscv_cpu_init(m->mem_map);

    /* RAM */
    cpu_register_ram(m->mem_map, RAM_BASE_ADDR, p->ram_size, 0);
    cpu_register_ram(m->mem_map, ROM_BASE_ADDR, ROM_SIZE, 0);

    /* flash */
    for (i = 0; i < p->flash_count; i++) {
        cpu_register_backed_ram(m->mem_map, p->tab_flash[i].address,
            p->tab_flash[i].size, p->tab_flash[i].backing,
            p->tab_flash[i].shared? DEVRAM_FLAG_SHARED: 0);
    }

    cpu_register_device(m->mem_map, CLINT_BASE_ADDR, CLINT_SIZE, m,
                        clint_read, clint_write, DEVIO_SIZE32);

    cpu_register_device(m->mem_map, HTIF_BASE_ADDR, HTIF_SIZE, m,
        htif_read, htif_write, DEVIO_SIZE32);

    init_ram_and_rom(p, m);

    if (p->interactive) {
        m->htif_console = htif_console_init();
    }

    return (VirtMachine *)m;
}

void virt_machine_end(VirtMachine *v)
{
    RISCVMachine *m = (RISCVMachine *)v;
    if (m->htif_console) {
        htif_console_end(m->htif_console);
    }
    riscv_cpu_end(m->cpu_state);
    phys_mem_map_end(m->mem_map);
    free(m);
}

uint64_t virt_machine_get_mcycle(VirtMachine *v) {
    RISCVMachine *m = (RISCVMachine *)v;
    return riscv_cpu_get_mcycle(m->cpu_state);
}

uint64_t virt_machine_get_htif_tohost(VirtMachine *v) {
    RISCVMachine *m = (RISCVMachine *)v;
    return m->htif_tohost;
}

const char *virt_machine_get_name(void)
{
    return "riscv64";
}

int virt_machine_run(VirtMachine *v, uint64_t cycles_end)
{
    RISCVMachine *m = (RISCVMachine *)v;
    RISCVCPUState *c = m->cpu_state;
    HTIFConsole *con = m->htif_console;


    for (;;) {

        uint64_t cycles = riscv_cpu_get_mcycle(c);

        uint64_t cycles_div_end = cycles + RTC_FREQ_DIV -
            cycles % RTC_FREQ_DIV;
        uint64_t this_cycles_end = cycles_end > cycles_div_end?
            cycles_div_end: cycles_end;

        /* execute as many cycles as possible until shuthost
         * or powerdown */
        riscv_cpu_run(c, this_cycles_end);
        cycles = riscv_cpu_get_mcycle(c);

        /* if we reached our target number of cycles, break */
        if (cycles >= cycles_end) {
            return 0;
        }

        /* if we were shutdown, break */
        if (riscv_cpu_get_shuthost(c)) {
            return 1;
        }

        /* check for timer interrupts */
        /* if the timer interrupt is not already pending */
        if (!(riscv_cpu_get_mip(c) & MIP_MTIP)) {
            uint64_t timer_cycles = rtc_time_to_cycles(m->timecmp);
            /* if timer expired, raise interrupt */
            if (timer_cycles <= cycles) {
                riscv_cpu_set_mip(c, MIP_MTIP);
            /* otherwise, if the cpu is powered down, waiting for interrupts,
             * skip time */
            } else if (riscv_cpu_get_power_down(c)) {
                if (timer_cycles < cycles_end) {
                    riscv_cpu_set_mcycle(c, timer_cycles);
                } else {
                    riscv_cpu_set_mcycle(c, cycles_end);
                }
            }
        }

        /* check for I/O with console */
        if (con) {
            /* if the character we made available has
             * already been consumed */
            if (!con->char_pending) {
                /* if we don't have any characters left in
                 * buffer, try to obtain more from stdin */
                if (con->buf_pos >= con->buf_len) {
                    fd_set rfds, wfds, efds;
                    int fd_max, ret;
                    struct timeval tv;
                    FD_ZERO(&rfds);
                    FD_ZERO(&wfds);
                    FD_ZERO(&efds);
                    fd_max = con->stdin_fd;
                    FD_SET(con->stdin_fd, &rfds);
                    tv.tv_sec = 0;
                    tv.tv_usec = riscv_cpu_get_power_down(c)? 1000: 0;
                    ret = select(fd_max + 1, &rfds, &wfds, &efds, &tv);
                    if (ret > 0 && FD_ISSET(con->stdin_fd, &rfds)) {
                        con->buf_pos = 0;
                        con->buf_len = read(con->stdin_fd, con->buf,
                            HTIF_CONSOLE_BUF_SIZE);
                        // If stdin is closed, return EOF
                        if (con->buf_len <= 0) {
                            con->buf_len = 1;
                            con->buf[0] = 4; /* CTRL+D */
                        }
                    }
                }
                if (con->buf_pos < con->buf_len) {
                    /* feed another character and wake the cpu */
                    m->htif_fromhost = ((uint64_t)1 << 56) |
                            ((uint64_t)0 << 48) | con->buf[con->buf_pos++];
                    con->char_pending = true;
                    riscv_cpu_set_power_down(c, false);
                }
            }
        }
    }
}