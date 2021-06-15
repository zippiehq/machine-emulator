// Copyright 2021 Cartesi Pte. Ltd.
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

/// \file
/// \brief Cartesi machine emulator C API interface

#ifndef CM_C_API_H
#define CM_C_API_H

#include <stdint.h>
#include <assert.h>

#ifndef __cplusplus

#include <stdbool.h>

#endif

#include "machine-c-defines.h"

#ifdef __cplusplus
extern "C" {
#if __GNUC__ >= 5
#pragma GCC visibility push(default)
#endif
#endif


// ---------------------------------
// API Structures
// ---------------------------------

typedef uint8_t cm_hash[CM_MACHINE_HASH_BYTE_SIZE];

/// \brief List of CSRs to use with read_csr and write_csr
typedef enum {
    CM_PROC_PC,
    CM_PROC_MVENDORID,
    CM_PROC_MARCHID,
    CM_PROC_MIMPID,
    CM_PROC_MCYCLE,
    CM_PROC_MINSTRET,
    CM_PROC_MSTATUS,
    CM_PROC_MTVEC,
    CM_PROC_MSCRATCH,
    CM_PROC_MEPC,
    CM_PROC_MCAUSE,
    CM_PROC_MTVAL,
    CM_PROC_MISA,
    CM_PROC_MIE,
    CM_PROC_MIP,
    CM_PROC_MEDELEG,
    CM_PROC_MIDELEG,
    CM_PROC_MCOUNTEREN,
    CM_PROC_STVEC,
    CM_PROC_SSCRATCH,
    CM_PROC_SEPC,
    CM_PROC_SCAUSE,
    CM_PROC_STVAL,
    CM_PROC_SATP,
    CM_PROC_SCOUNTEREN,
    CM_PROC_ILRSC,
    CM_PROC_IFLAGS,
    CM_PROC_CLINT_MTIMECMP,
    CM_PROC_HTIF_TOHOST,
    CM_PROC_HTIF_FROMHOST,
    CM_PROC_HTIF_IHALT,
    CM_PROC_HTIF_ICONSOLE,
    CM_PROC_HTIF_IYIELD,
    CM_PROC_DHD_TSTART,
    CM_PROC_DHD_TLENGTH,
    CM_PROC_DHD_DLENGTH,
    CM_PROC_DHD_HLENGTH
} CM_PROC_CSR;

typedef struct {
    uint64_t x[CM_MACHINE_X_REG_COUNT];          ///< Value of general-purpose registers
    uint64_t pc;                  ///< Value of pc
    uint64_t mvendorid;    ///< Value of mvendorid CSR
    uint64_t marchid;        ///< Value of marchid CSR
    uint64_t mimpid;          ///< Value of mimpid CSR
    uint64_t mcycle;          ///< Value of mcycle CSR
    uint64_t minstret;      ///< Value of minstret CSR
    uint64_t mstatus;        ///< Value of mstatus CSR
    uint64_t mtvec;            ///< Value of mtvec CSR
    uint64_t mscratch;      ///< Value of mscratch CSR
    uint64_t mepc;              ///< Value of mepc CSR
    uint64_t mcause;          ///< Value of mcause CSR
    uint64_t mtval;            ///< Value of mtval CSR
    uint64_t misa;              ///< Value of misa CSR
    uint64_t mie;                ///< Value of mie CSR
    uint64_t mip;                ///< Value of mip CSR
    uint64_t medeleg;        ///< Value of medeleg CSR
    uint64_t mideleg;        ///< Value of mideleg CSR
    uint64_t mcounteren;  ///< Value of mcounteren CSR
    uint64_t stvec;            ///< Value of stvec CSR
    uint64_t sscratch;      ///< Value of sscratch CSR
    uint64_t sepc;              ///< Value of sepc CSR
    uint64_t scause;          ///< Value of scause CSR
    uint64_t stval;            ///< Value of stval CSR
    uint64_t satp;              ///< Value of satp CSR
    uint64_t scounteren;  ///< Value of scounteren CSR
    uint64_t ilrsc;            ///< Value of ilrsc CSR
    uint64_t iflags;          ///< Value of iflags CSR
} cm_processor_config;

/// \brief RAM state configuration
typedef struct {
    uint64_t length; ///< RAM length
    const char *image_filename; ///< RAM image file name
} cm_ram_config;

/// \brief ROM state configuration
typedef struct {
    const char *bootargs; ///< Bootargs to pass to kernel
    const char *image_filename; ///< ROM image file
} cm_rom_config;

/// \brief Flash drive state configuration
typedef struct {
    uint64_t start;           ///< Flash drive start position
    uint64_t length;          ///< Flash drive length
    bool shared;              ///< Target changes to drive affect image file?
    const char *image_filename; ///< Flash drive image file name
} cm_flash_drive_config;

/// \brief CLINT device state configuration
typedef struct {
    uint64_t mtimecmp; ///< Value of mtimecmp CSR
} cm_clint_config;

/// \brief HTIF device state configuration
typedef struct {
    uint64_t fromhost; ///< Value of fromhost CSR
    uint64_t tohost;     ///< Value of tohost CSR
    bool console_getchar;      ///< Make console getchar available?
    bool yield_progress;       ///< Make yield progress available?
    bool yield_rollup;         ///< Make yield rollup available?
} cm_htif_config;

/// \brief DHD device state configuration
typedef struct {
    uint64_t tstart;           ///< Start of target physical memory range for output data
    uint64_t tlength;          ///< Length of target physical memory range for output data
    const char *image_filename; ///< Data image file name
    uint64_t dlength;          ///< Output data length CSR
    uint64_t hlength;          ///< Input hash length CSR
    uint64_t h[CM_MACHINE_DHD_H_REG_COUNT]; ///< Input hash words
} cm_dhd_config;

/// \brief Machine state configuration
typedef struct {
    cm_processor_config processor;
    cm_ram_config ram;
    cm_rom_config rom;
    cm_flash_drive_config *flash_drive;
    int flash_drive_count;
    cm_clint_config clint;
    cm_htif_config htif;
    cm_dhd_config dhd;
} cm_machine_config;


/// \brief DHD runtime configuration
typedef struct {
    const char *source_address; ///< Address of dehash source
} cm_dhd_runtime_config;

/// \brief Concurrency runtime configuration
typedef struct {
    uint64_t update_merkle_tree;
} cm_concurrency_config;

/// \brief Machine runtime configuration
typedef struct {
    cm_dhd_runtime_config dhd;
    cm_concurrency_config concurrency;
} cm_machine_runtime_config;

/// \brief Machine instance handle
typedef void cm_machine;

/// \brief Machine operation error message
typedef const char *error_message;


// ---------------------------------
// API function definitions
// ---------------------------------


/// \brief Create new machine config with default parameters
/// \returns Pointer to new default config object
/// \details Object acquired from this function must not be changed and
/// must be deleted with cm_delete_machine_config. To make a custom config based
/// on default config user must make a deep copy of returned object members and then
/// customize
const cm_machine_config *cm_new_default_machine_config();

/// \brief Delete machine config acquired from cm_new_default_machine_config
/// \returns void
void cm_delete_machine_config(const cm_machine_config *config);


/// \brief Creates new machine instance from configuration
/// \param config Machine configuration
/// \param runtime_config Machine runtime configuration
/// \param  new_machine Receives the pointer to new machine instance
/// \param error_msg Receives the message about result of the operation
/// \returns 0 for success, non zero code for error
int cm_create_machine(const cm_machine_config *config, const cm_machine_runtime_config *runtime_config,
                      cm_machine **new_machine, error_message *err_msg);

/// \brief Deletes machine instance
/// \param m Valid pointer to the existing machine instance
void cm_delete_machine(cm_machine *m);


/// \brief Runs the machine until mcycle reaches mcycle_end or the machine halts.
/// \param m Pointer to valid machine instance
/// \param mcycle_end End cycle value
/// \param error_msg Receives the message about result of the operation
/// \returns 0 for success, non zero code for error
int cm_machine_run(cm_machine *m, uint64_t mcycle_end, error_message *err_msg);

/// \brief Obtains the root hash of the Merkle tree.
/// \param m Pointer to valid machine instance
/// \param hash Valid pointer to cm_hash structure that  receives the hash.
void cm_get_root_hash(const cm_machine *m, cm_hash *hash);


/// \brief Verifies integrity of Merkle tree.
/// \param m Pointer to valid machine instance
/// \returns True if tree is self-consistent, false otherwise.
bool cm_verify_merkle_tree(const cm_machine *m);

/// \brief Read the value of any CSR
/// \param m Pointer to valid machine instance
/// \returns The value of the CSR
uint64_t cm_read_csr(const cm_machine *m, CM_PROC_CSR r);

/// \brief Write the value of any CSR
/// \param m Pointer to valid machine instance
/// \param w CSR to write
/// \param val Value to write
void cm_write_csr(cm_machine *m, CM_PROC_CSR w, uint64_t val);

/// \brief Gets the address of any CSR
/// \param w The CSR
/// \returns The address of the specified CSR
uint64_t cm_get_csr_address(CM_PROC_CSR w);

/// \brief Read the value of a word in the machine state.
/// \param m Pointer to valid machine instance
/// \param word_address Word address (aligned to 64-bit boundary).
/// \param word_value Receives word value.
/// \returns true if succeeded, false otherwise.
/// \warning The current implementation of this function is very slow!
bool cm_read_word(const cm_machine *m, uint64_t word_address, uint64_t *word_value);


/// \brief Reads a chunk of data from the machine memory.
/// \param m Pointer to valid machine instance
/// \param address Physical address to start reading.
/// \param data Receives chunk of memory.
/// \param length Size of chunk.
/// \details The entire chunk, from \p address to \p address + \p length must
/// be inside the same PMA region. Moreover, this PMA must be a memory PMA,
/// and not a device PMA.
void cm_read_memory(const cm_machine *m, uint64_t address, unsigned char *data, uint64_t length);

/// \brief Writes a chunk of data to the machine memory.
/// \param m Pointer to valid machine instance
/// \param address Physical address to start writing.
/// \param data Source for chunk of data.
/// \param length Size of chunk.
/// \details The entire chunk, from \p address to \p address + \p length must
/// be inside the same PMA region. Moreover, this PMA must be a memory PMA,
/// and not a device PMA.
void cm_write_memory(cm_machine *m, uint64_t address, const unsigned char *data, size_t length);

/// \brief Reads the value of a general-purpose register.
/// \param m Pointer to valid machine instance
/// \param i Register index. Between 0 and X_REG_COUNT-1, inclusive.
/// \returns The value of the register.
uint64_t cm_read_x(const cm_machine *m, int i);

/// \brief Writes the value of a general-purpose register.
/// \param m Pointer to valid machine instance
/// \param i Register index. Between 1 and X_REG_COUNT-1, inclusive.
/// \param val New register value.
void cm_write_x(cm_machine *m, int i, uint64_t val);

/// \brief Gets the address of a general-purpose register.
/// \param i Register index. Between 0 and X_REG_COUNT-1, inclusive.
/// \returns Address of the specified register
uint64_t cm_get_x_address(int i);

/// \brief Reads the value of the pc register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_pc(const cm_machine *m);

/// \brief Writes the value of the pc register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_pc(cm_machine *m, uint64_t val);

/// \brief Reads the value of the mvendorid register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_mvendorid(const cm_machine *m);

/// \brief Reads the value of the marchid register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_marchid(const cm_machine *m);

/// \brief Reads the value of the mimpid register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_mimpid(const cm_machine *m);

/// \brief Reads the value of the mcycle register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_mcycle(const cm_machine *m);

/// \brief Writes the value of the mcycle register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_mcycle(cm_machine *m, uint64_t val);

/// \brief Reads the value of the minstret register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_minstret(const cm_machine *m);

/// \brief Writes the value of the minstret register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_minstret(cm_machine *m, uint64_t val);

/// \brief Reads the value of the mstatus register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_mstatus(const cm_machine *m);

/// \brief Writes the value of the mstatus register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_mstatus(cm_machine *m, uint64_t val);

/// \brief Reads the value of the mtvec register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_mtvec(const cm_machine *m);

/// \brief Writes the value of the mtvec register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_mtvec(cm_machine *m, uint64_t val);

/// \brief Reads the value of the mscratch register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_mscratch(const cm_machine *m);

/// \brief Writes the value of the mscratch register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_mscratch(cm_machine *m, uint64_t val);

/// \brief Reads the value of the mepc register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_mepc(const cm_machine *m);

/// \brief Writes the value of the mepc register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_mepc(cm_machine *m, uint64_t val);

/// \brief Reads the value of the mcause register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_mcause(const cm_machine *m);

/// \brief Writes the value of the mcause register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_mcause(cm_machine *m, uint64_t val);

/// \brief Reads the value of the mtval register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_mtval(const cm_machine *m);

/// \brief Writes the value of the mtval register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_mtval(cm_machine *m, uint64_t val);

/// \brief Reads the value of the misa register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_misa(const cm_machine *m);

/// \brief Writes the value of the misa register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_misa(cm_machine *m, uint64_t val);

/// \brief Reads the value of the mie register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_mie(const cm_machine *m);

/// \brief Reads the value of the mie register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_mie(cm_machine *m, uint64_t val);

/// \brief Reads the value of the mip register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_mip(const cm_machine *m);

/// \brief Reads the value of the mip register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_mip(cm_machine *m, uint64_t val);

/// \brief Reads the value of the medeleg register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_medeleg(const cm_machine *m);

/// \brief Writes the value of the medeleg register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_medeleg(cm_machine *m, uint64_t val);

/// \brief Reads the value of the mideleg register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_mideleg(const cm_machine *m);

/// \brief Writes the value of the mideleg register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_mideleg(cm_machine *m, uint64_t val);

/// \brief Reads the value of the mcounteren register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_mcounteren(const cm_machine *m);

/// \brief Writes the value of the mcounteren register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_mcounteren(cm_machine *m, uint64_t val);

/// \brief Reads the value of the stvec register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_stvec(const cm_machine *m);


/// \brief Writes the value of the stvec register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_stvec(cm_machine *m, uint64_t val);

/// \brief Reads the value of the sscratch register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_sscratch(const cm_machine *m);

/// \brief Writes the value of the sscratch register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_sscratch(cm_machine *m, uint64_t val);

/// \brief Reads the value of the sepc register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_sepc(const cm_machine *m);

/// \brief Writes the value of the sepc register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_sepc(cm_machine *m, uint64_t val);

/// \brief Reads the value of the scause register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_scause(const cm_machine *m);

/// \brief Writes the value of the scause register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_scause(cm_machine *m, uint64_t val);

/// \brief Reads the value of the stval register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_stval(const cm_machine *m);

/// \brief Writes the value of the stval register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_stval(cm_machine *m, uint64_t val);

/// \brief Reads the value of the satp register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_satp(const cm_machine *m);

/// \brief Writes the value of the satp register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_satp(cm_machine *m, uint64_t val);

/// \brief Reads the value of the scounteren register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_scounteren(const cm_machine *m);

/// \brief Writes the value of the scounteren register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_scounteren(cm_machine *m, uint64_t val);

/// \brief Reads the value of the ilrsc register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_ilrsc(const cm_machine *m);

/// \brief Writes the value of the ilrsc register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_ilrsc(cm_machine *m, uint64_t val);

/// \brief Reads the value of the iflags register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_iflags(const cm_machine *m);

/// \brief Returns packed iflags from its component fields.
/// \returns The value of the register.
uint64_t cm_packed_iflags(int PRV, int Y, int H);

/// \brief Reads the value of the iflags register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_iflags(cm_machine *m, uint64_t val);

/// \brief Reads the value of HTIF's tohost register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_htif_tohost(const cm_machine *m);

/// \brief Reads the value of the device field of HTIF's tohost register.
/// \param m Pointer to valid machine instance
/// \returns The value of the field.
uint64_t cm_read_htif_tohost_dev(const cm_machine *m);

/// \brief Reads the value of the command field of HTIF's tohost register.
/// \param m Pointer to valid machine instance
/// \returns The value of the field.
uint64_t cm_read_htif_tohost_cmd(const cm_machine *m);

/// \brief Reads the value of the data field of HTIF's tohost register.
/// \param m Pointer to valid machine instance
/// \returns The value of the field.
uint64_t cm_read_htif_tohost_data(const cm_machine *m);

/// \brief Writes the value of HTIF's tohost register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_htif_tohost(cm_machine *m, uint64_t val);

/// \brief Reads the value of HTIF's fromhost register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_htif_fromhost(const cm_machine *m);

/// \brief Writes the value of HTIF's fromhost register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_htif_fromhost(cm_machine *m, uint64_t val);

/// \brief Writes the value of the data field in HTIF's fromhost register.
/// \param m Pointer to valid machine instance
/// \param val New value for the field.
void cm_write_htif_fromhost_data(cm_machine *m, uint64_t val);

/// \brief Reads the value of HTIF's halt register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_htif_ihalt(const cm_machine *m);

/// \brief Writes the value of HTIF's halt register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void write_htif_ihalt(cm_machine *m, uint64_t val);

/// \brief Reads the value of HTIF's console register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_htif_iconsole(const cm_machine *m);

/// \brief Writes the value of HTIF's console register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_htif_iconsole(cm_machine *m, uint64_t val);

/// \brief Reads the value of HTIF's yield register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_htif_iyield(const cm_machine *m);

/// \brief Writes the value of HTIF's yield register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_htif_iyield(cm_machine *m, uint64_t val);

/// \brief Reads the value of CLINT's mtimecmp register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_clint_mtimecmp(const cm_machine *m);

/// \brief Writes the value of CLINT's mtimecmp register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_clint_mtimecmp(cm_machine *m, uint64_t val);

/// \brief Reads the value of DHD's tstart register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_dhd_tstart(const cm_machine *m);

/// \brief Writes the value of DHD's tstart register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_dhd_tstart(cm_machine *m, uint64_t val);

/// \brief Reads the value of DHD's tlength register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_dhd_tlength(const cm_machine *m);

/// \brief Writes the value of DHD's tlength register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_dhd_tlength(cm_machine *m, uint64_t val);

/// \brief Reads the value of DHD's dlength register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_dhd_dlength(const cm_machine *m);

/// \brief Writes the value of DHD's dlength register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_dhd_dlength(cm_machine *m, uint64_t val);

/// \brief Reads the value of DHD's hlength register.
/// \param m Pointer to valid machine instance
/// \returns The value of the register.
uint64_t cm_read_dhd_hlength(const cm_machine *m);

/// \brief Writes the value of DHD's hlength register.
/// \param m Pointer to valid machine instance
/// \param val New register value.
void cm_write_dhd_hlength(cm_machine *m, uint64_t val);

/// \brief Writes the value of DHD's input hash word.
/// \param m Pointer to valid machine instance
/// \param i Index of input hash word.
/// Between 0 and DHD_H_REG_COUNT-1, inclusive.
/// \param val New value for word.
void cm_write_dhd_h(cm_machine *m, int i, uint64_t val);

/// \brief Gets the address of a DHD h register.
/// \param i Register index. Between 0 and DHD_H_REG_COUNT-1, inclusive.
/// \returns Address of the specified register
uint64_t cm_get_dhd_h_address(int i);

/// \brief Checks the value of the iflags_Y flag.
/// \param m Pointer to valid machine instance
/// \returns The flag value.
bool cm_read_iflags_Y(const cm_machine *m);

/// \brief Resets the value of the iflags_Y flag.
/// \param m Pointer to valid machine instance
void cm_reset_iflags_Y(cm_machine *m);

/// \brief Sets the iflags_Y flag.
/// \param m Pointer to valid machine instance
void cm_set_iflags_Y(cm_machine *m);

/// \brief Checks the value of the iflags_H flag.
/// \param m Pointer to valid machine instance
/// \returns The flag value.
bool cm_read_iflags_H(const cm_machine *m);

/// \brief Checks the value of the iflags_PRV field.
/// \param m Pointer to valid machine instance
/// \returns The field value.
uint8_t cm_read_iflags_PRV(const cm_machine *m);

/// \brief Sets the iflags_H flag.
/// \param m Pointer to valid machine instance
void cm_set_iflags_H(cm_machine *m);

/// \brief Sets bits in mip.
/// \param m Pointer to valid machine instance
/// \param mask Bits set in \p mask will also be set in mip
void cm_set_mip(cm_machine *m, uint32_t mask);

/// \brief Resets bits in mip.
/// \param m Pointer to valid machine instance
/// \param mask Bits set in \p mask will also be reset in mip
void cm_reset_mip(cm_machine *m, uint32_t mask);

/// \brief Dump all memory ranges to files in current working directory.
/// \param m Pointer to valid machine instance
void cm_dump_pmas(const cm_machine *m);


#ifdef __cplusplus

#if __GNUC__ >= 5
#pragma GCC visibility pop
#endif

}
#endif

#endif //CM_C_API_H