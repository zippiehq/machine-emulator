#!/usr/bin/env lua5.3

-- Copyright 2019-2021 Cartesi Pte. Ltd.
--
-- This file is part of the machine-emulator. The machine-emulator is free
-- software: you can redistribute it and/or modify it under the terms of the GNU
-- Lesser General Public License as published by the Free Software Foundation,
-- either version 3 of the License, or (at your option) any later version.
--
-- The machine-emulator is distributed in the hope that it will be useful, but
-- WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
-- FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
-- for more details.
--
-- You should have received a copy of the GNU Lesser General Public License
-- along with the machine-emulator. If not, see http://www.gnu.org/licenses/.
--

local cartesi = require"cartesi"
local util = require"cartesi.util"

local function stderr(fmt, ...)
    io.stderr:write(string.format(fmt, ...))
end

local function adjust_images_path(path)
    if not path then return "" end
    return string.gsub(path, "/*$", "") .. "/"
end

-- Print help and exit
local function help()
    stderr([=[
Usage:

  %s [options] [command] [arguments]

where options are:

  --remote-address=<address>
    use a remote cartesi machine listenning to <address> instead of
    running a local cartesi machine.
    (requires --checkin-address)

  --checkin-address=<address>
    address of the local checkin server to run.

  --remote-shutdown
    shutdown the remote cartesi machine after the execution.

  --ram-image=<filename>
    name of file containing RAM image (default: "linux.bin").

  --no-ram-image
    forget settings for RAM image.

  --ram-length=<number>
    set RAM length.

  --rom-image=<filename>
    name of file containing ROM image (default: "rom.bin").

  --no-rom-bootargs
    clear default bootargs.

  --append-rom-bootargs=<string>
    append <string> to bootargs.

  --no-root-flash-drive
    clear default root flash drive and associated bootargs parameters.

  --flash-drive=<key>:<value>[,<key>:<value>[,...]...]
    defines a new flash drive, or modify an existing flash drive definition
    flash drives appear as /dev/mtdblock[1-7].

    <key>:<value> is one of
        label:<label>
        filename:<filename>
        start:<number>
        length:<number>
        shared

        label (mandatory)
        identifies the flash drive. init attempts to mount it as /mnt/<label>.

        filename (optional)
        gives the name of the file containing the image for the flash drive.
        when omitted or set to the empty, the drive starts filled with 0.

        start (optional)
        sets the starting physical memory offset for flash drive in bytes.
        when omitted, drives start at 2 << 63 and are spaced by 2 << 60.
        if any start offset is set, all of them must be set.

        length (optional)
        gives the length of the flash drive in bytes (must be multiple of 4Ki).
        if omitted, the length is computed from the image in filename.
        if length and filename are set, the image file size must match length.

        shared (optional)
        target modifications to flash drive modify image file as well.
        by default, image files are not modified and changes are lost.

    (an option "--flash-drive=label:root,filename:rootfs.ext2" is implicit)

  --replace-flash-drive=<key>:<value>[,<key>:<value>[,...]...]
  --replace-memory-range=<key>:<value>[,<key>:<value>[,...]...]
    replaces an existing flash drive or rollup memory range right after
    machine instantiation.
    (typically used in conjunction with the --load=<directory> option.)

    <key>:<value> is one of
        filename:<filename>
        start:<number>
        length:<number>
        shared

    semantics are the same as for the --flash-drive option with the following
    difference: start and length are mandatory, and must match those of a
    previously existing flash drive or rollup memory memory range.

  --dhd=<key>:<value>[,<key>:<value>[,...]...]
    configures the dehashing device.
    by default, the device is not present.

    <key>:<value> is one of
        filename:<filename>
        tstart:<number>
        tlength:<number>

        filename (optional)
        gives the name of the file containing the initial dehashed data.
        when omitted or set to the empty, the data starts filled with 0.

        tstart (mandatory when device present)
        sets the start of target physical memory range for output data.
        must be aligned to tlength.

        tlength (mandatory when device present)
        gives the length of target physical memory range for output data.
        must be a power of 2 greater than 4Ki, or 0 when device not present.

  --dhd-source=<address>
    address of server acting as source for dehashed data.

  --rollup-rx-buffer=<key>:<value>[,<key>:<value>[,...]...]
  --rollup-tx-buffer=<key>:<value>[,<key>:<value>[,...]...]
  --rollup-input-metadata=<key>:<value>[,<key>:<value>[,...]...]
  --rollup-voucher-hashes=<key>:<value>[,<key>:<value>[,...]...]
  --rollup-notice-hashes=<key>:<value>[,<key>:<value>[,...]...]
    defines the individual the memory ranges used by rollups.

    <key>:<value> is one of
        filename:<filename>
        start:<number>
        length:<number>
        shared

    semantics are the same as for the --flash-drive option with the following
    difference: start and length are mandatory.

  --rollup
    defines appropriate values for rollup-rx-buffer, rollup-tx-buffer,
    rollup-input-metadata, rollup-voucher-hashes, rollup-notice hashes,
    and htif yield for use with rollups.
    equivalent to the following options:

    --rollup-rx-buffer=start:0x60000000,length:2<<20
    --rollup-tx-buffer=start:0x60200000,length:2<<20
    --rollup-input-metadata=start:0x60400000,length:4096
    --rollup-voucher-hashes=start:0x60600000,length:2<<20
    --rollup-notice-hashes=start:0x60800000,length:2<<20
    --htif-yield-manual
    --htif-yield-automatic

  --rollup-advance-state=<key>:<value>[,<key>:<value>[,...]...]
    advances the state of the machine through a number of inputs in an epoch

    <key>:<value> is one of
        epoch_index:<number>
        input:<filename-pattern>
        input_metadata:<filename-pattern>
        input_index_begin:<number>
        input_index_end:<number>
        voucher:<filename-pattern>
        voucher_hashes: <filename>
        notice:<filename-pattern>
        notice_hashes: <filename>
        report:<filename-pattern>
        hashes

        epoch_index
        the index of the epoch (the value of %%e).

        input (default: "epoch-%%e-input-%%i.bin")
        the pattern that derives the name of the file read for input %%i
        of epoch index %%e.

        input_index_begin (default: 0)
        index of first input to advance (the first value of %%i).

        input_index_end (default: 0)
        one past index of last input to advance (one past last value of %%i).

        input_metadata (default: "epoch-%%e-input-metadata-%%i.bin")
        the pattern that derives the name of the file read for
        input metadata %%i of epoch index %%e.

        voucher (default: "epoch-%%e-input-%%i-voucher-%%o.bin")
        the pattern that derives the name of the file written for voucher %%o
        of input %%i of epoch %%e.

        voucher_hashes (default: "epoch-%%e-input-%%i-voucher-hashes.bin")
        the pattern that derives the name of the file written for the voucher
        hashes of input %%i of epoch %%e.

        notice (default: "epoch-%%e-input-%%i-notice-%%o.bin")
        the pattern that derives the name of the file written for notice %%o
        of input %%i of epoch %%e.

        notice_hashes (default: "epoch-%%e-input-%%i-notice-hashes.bin")
        the pattern that derives the name of the file written for the notice
        hashes of input %%i of epoch %%e.

        report (default: "epoch-%%e-input-%%i-report-%%o.bin")
        the pattern that derives the name of the file written for report %%o
        of input %%i of epoch %%e.

        hashes
        print out hashes before every input.

    the input index ranges in {input_index_begin, ..., input_index_end-1}.
    for each input, "%%e" is replaced by the epoch index, "%%i" by the
    input index, and "%%o" by the voucher, notice, or report index.

  --rollup-inspect-state=<key>:<value>[,<key>:<value>[,...]...]
    inspect the state of the machine with a query.
    the query happens after the end of --rollup-advance-state.

    <key>:<value> is one of
        query:<filename>
        report:<filename-pattern>

        query (default: "query.bin")
        the name of the file from which to read the query.

        report (default: "query-report-%%o.bin")
        the pattern that derives the name of the file written for report %%o
        of the query.

    while the query is processed, "%%o" is replaced by the current report index.

  --concurrency=<key>:<value>[,<key>:<value>[,...]...]
    configures the number of threads used in some implementation parts.

    <key>:<value> is one of
        update_merkle_tree:<number>

        update_merkle_tree (optional)
        defines the number of threads to use while calculating the merkle tree.
        when ommited or defined as 0, the number of hardware threads is used if
        it can be identified or else a single thread is used.

  --max-mcycle=<number>
    stop at a given mcycle (default: 2305843009213693952).

  -i or --htif-console-getchar
    run in interactive mode.

  --htif-yield-manual
    honor yield requests with manual reset by target.

  --htif-yield-automatic
    honor yield requests with automatic reset by target.

  --store=<directory>
    store machine to <directory>.

  --load=<directory>
    load machine previously stored in <directory>.

  --initial-hash
    print initial state hash before running machine.

  --final-hash
    print final state hash when done.

  --periodic-hashes=<number-period>[,<number-start>]
    prints root hash every <number-period> cycles. If <number-start> is given,
    the periodic hashing will start at that mcycle. This option implies
    --initial-hash and --final-hash.
    (default: none)

  --step
    print step log for 1 additional cycle when done.

  --json-steps=<filename>
    output json with step logs for all cycles to <filename>.

  --store-config[=<filename>]
    store initial machine config to <filename>. If <filename> is omitted,
    print the initial machine config to stderr.

  --load-config=<filename>
    load initial machine config from <filename>. If a field is omitted on
    machine_config table, it will fall back into the respective command-line
    argument or into the default value.

  --dump-pmas
    dump all PMA ranges to disk when done.

and command and arguments:

  command
    the full path to the program inside the target system.
    (default: /bin/sh)

  arguments
    the given command arguments.

<number> can be specified in decimal (e.g., 16) or hexadeximal (e.g., 0x10),
with a suffix multiplier (i.e., Ki, Mi, Gi for 2^10, 2^20, 2^30, respectively),
or a left shift (e.g., 2 << 20).

<address> is one of the following formats:
  <host>:<port>
   unix:<path>

<host> can be a host name, IPv4 or IPv6 address.

]=], arg[0])
    os.exit()
end

local remote_address = nil
local checkin_address = nil
local remote_shutdown = false
local images_path = adjust_images_path(os.getenv('CARTESI_IMAGES_PATH'))
local flash_image_filename = { root = images_path .. "rootfs.ext2" }
local flash_label_order = { "root" }
local flash_shared = { }
local flash_start = { }
local flash_length = { }
local memory_range_replace = { }
local ram_image_filename = images_path .. "linux.bin"
local ram_length = 64 << 20
local rom_image_filename = images_path .. "rom.bin"
local rom_bootargs = "console=hvc0 rootfstype=ext2 root=/dev/mtdblock0 rw quiet"
local dhd_tstart = 0
local dhd_tlength = 0
local dhd_image_filename = nil
local dhd_source_address = nil
local rollup_rx_buffer = { start = 0, length = 0 }
local rollup_tx_buffer = { start = 0, length = 0 }
local rollup_input_metadata = { start = 0, length = 0 }
local rollup_voucher_hashes = { start = 0, length = 0 }
local rollup_notice_hashes = { start = 0, length = 0 }
local rollup_advance = nil
local rollup_inspect = nil
local concurrency_update_merkle_tree = 0
local append_rom_bootargs = ""
local htif_console_getchar = false
local htif_yield_automatic = false
local htif_yield_manual = false
local initial_hash = false
local final_hash = false
local initial_proof = {}
local final_proof = {}
local periodic_hashes_period = math.maxinteger
local periodic_hashes_start = 0
local dump_pmas = false
local max_mcycle = math.maxinteger
local json_steps
local step = false
local store_dir = nil
local load_dir = nil
local cmdline_opts_finished = false
local store_config = false
local load_config = false
local exec_arguments = {}

local function parse_memory_range(opts, what, all)
    local f = util.parse_options(opts, {
        filename = true,
        shared = true,
        length = true,
        start = true
    })
    f.image_filename = f.filename
    f.filename = nil
    if f.image_filename == true then f.image_filename = "" end
    assert(not f.shared or f.shared == true,
        "invalid " .. what .. " shared value in " .. all)
    f.start = assert(util.parse_number(f.start),
        "invalid " .. what .. " start in " .. all)
    f.length = assert(util.parse_number(f.length),
        "invalid " .. what .. " length in " .. all)
    return f
end

-- List of supported options
-- Options are processed in order
-- For each option,
--   first entry is the pattern to match
--   second entry is a callback
--     if callback returns true, the option is accepted.
--     if callback returns false, the option is rejected.
local options = {
    { "^%-h$", function(all)
        if not all then return false end
        help()
        return true
    end },
    { "^%-%-help$", function(all)
        if not all then return false end
        help()
        return true
    end },
    { "^%-%-rom%-image%=(.*)$", function(o)
        if not o or #o < 1 then return false end
        rom_image_filename = o
        return true
    end },
    { "^%-%-no%-rom%-bootargs$", function(all)
        if not all then return false end
        rom_bootargs = ""
        return true
    end },
    { "^%-%-append%-rom%-bootargs%=(.*)$", function(o)
        if not o or #o < 1 then return false end
        append_rom_bootargs = o
        return true
    end },
    { "^%-%-ram%-length%=(.+)$", function(n)
        if not n then return false end
        ram_length = assert(util.parse_number(n), "invalid RAM length " .. n)
        return true
    end },
    { "^%-%-ram%-image%=(.*)$", function(o)
        if not o or #o < 1 then return false end
        ram_image_filename = o
        return true
    end },
    { "^%-%-no%-ram%-image$", function(all)
        if not all then return false end
        ram_image_filename = ""
        return true
    end },
    { "^%-%-htif%-console%-getchar$", function(all)
        if not all then return false end
        htif_console_getchar = true
        return true
    end },
    { "^%-i$", function(all)
        if not all then return false end
        htif_console_getchar = true
        return true
    end },
    { "^%-%-htif%-yield%-manual$", function(all)
        if not all then return false end
        htif_yield_manual = true
        return true
    end },
    { "^%-%-htif%-yield%-automatic$", function(all)
        if not all then return false end
        htif_yield_automatic = true
        return true
    end },
    { "^%-%-rollup$", function(all)
        if not all then return false end
        rollup_rx_buffer = { start = 0x60000000, length = 2 << 20 }
        rollup_tx_buffer = { start = 0x60200000, length = 2 << 20 }
        rollup_input_metadata = { start = 0x60400000, length = 4096 }
        rollup_voucher_hashes = { start = 0x60600000, length = 2 << 20 }
        rollup_notice_hashes = { start = 0x60800000, length = 2 << 20 }
        htif_yield_automatic = true
        htif_yield_manual = true
        return true
    end },
    { "^(%-%-flash%-drive%=(.+))$", function(all, opts)
        if not opts then return false end
        local f = util.parse_options(opts, {
            label = true,
            filename = true,
            shared = true,
            length = true,
            start = true
        })
        assert(f.label, "missing flash drive label in " .. all)
        f.image_filename = f.filename
        f.filename = nil
        if f.image_filename == true then f.image_filename = "" end
        assert(not f.shared or f.shared == true,
            "invalid flash drive shared value in " .. all)
        if f.start then
            f.start = assert(util.parse_number(f.start),
                "invalid flash drive start in " .. all)
        end
        if f.length then
            f.length = assert(util.parse_number(f.length),
                "invalid flash drive length in " .. all)
        end
        local d = f.label
        if not flash_image_filename[d] then
            flash_label_order[#flash_label_order+1] = d
            flash_image_filename[d] = ""
        end
        flash_image_filename[d] = f.image_filename or
            flash_image_filename[d]
        flash_start[d] = f.start or flash_start[d]
        flash_length[d] = f.length or flash_length[d]
        flash_shared[d] = f.shared or flash_shared[d]
        return true
    end },
    { "^(%-%-replace%-flash%-drive%=(.+))$", function(all, opts)
        if not opts then return false end
        memory_range_replace[#memory_range_replace+1] =
            parse_memory_range(opts, "flash drive", all)
        return true
    end },
    { "^(%-%-replace%-memory%-range%=(.+))$", function(all, opts)
        if not opts then return false end
        memory_range_replace[#memory_range_replace+1] =
            parse_memory_range(opts, "flash drive", all)
        return true
    end },
    { "^(%-%-rollup%-advance%-state%=(.+))$", function(all, opts)
        if not opts then return false end
        local r = util.parse_options(opts, {
            epoch_index = true,
            input = true,
            input_metadata = true,
            input_index_begin = true,
            input_index_end = true,
            voucher = true,
            voucher_hashes = true,
            notice = true,
            notice_hashes = true,
            report = true,
            hashes = true,
        })
        assert(not r.hashes or r.hashes == true, "invalid hashes value in " .. all)
        r.epoch_index = assert(util.parse_number(r.epoch_index), "invalid epoch index in " .. all)
        r.input = r.input or "epoch-%e-input-%i.bin"
        r.input_metadata = r.input_metadata or "epoch-%e-input-metadata-%i.bin"
        r.input_index_begin = r.input_index_begin or 0
        r.input_index_begin = assert(util.parse_number(r.input_index_begin), "invalid input index begin in " .. all)
        r.input_index_end = r.input_index_end or 0
        r.input_index_end = assert(util.parse_number(r.input_index_end), "invalid input index end in " .. all)
        r.voucher = r.voucher or "epoch-%e-input-%i-voucher-%o.bin"
        r.voucher_hashes = r.voucher_hashes or "epoch-%e-input-%i-voucher-hashes.bin"
        r.notice = r.notice or "epoch-%e-input-%i-notice-%o.bin"
        r.notice_hashes = r.notice_hashes or "epoch-%e-input-%i-notice-hashes.bin"
        r.report = r.report or "epoch-%e-input-%i-report-%o.bin"
        r.next_input_index = r.input_index_begin
        rollup_advance = r
        return true
    end },
    { "^(%-%-rollup%-inspect%-state%=(.+))$", function(all, opts)
        if not opts then return false end
        local r = util.parse_options(opts, {
            query = true,
            report = true,
        })
        r.query = r.query or "query.bin"
        r.report = r.report or "query-report-%o.bin"
        rollup_inspect = r
        return true
    end },
    { "^%-%-rollup%-inspect%-state$", function(all)
        if not all then return false end
        rollup_inspect = {
            query = "query.bin",
            report = "query-report-%o.bin"
        }
        return true
    end },
    { "^(%-%-dhd%=(.+))$", function(all, opts)
        if not opts then return false end
        local d = util.parse_options(opts, {
            filename = true,
            tlength = true,
            tstart = true
        })
        d.image_filename = d.filename
        d.filename = nil
        if d.image_filename == true then d.image_filename = "" end
        d.tstart = assert(util.parse_number(d.tstart),
                "invalid start of target in " .. all)
        d.tlength = assert(util.parse_number(d.tlength),
                "invalid length of target in " .. all)
        dhd_tstart = d.tstart
        dhd_tlength = d.tlength
        dhd_image_filename = d.image_filename
        return true
    end },
    { "^%-%-dhd%-source%=(.*)$", function(o)
        if not o or #o < 1 then return false end
        dhd_source_address = o
        return true
    end },
    { "^(%-%-concurrency%=(.+))$", function(all, opts)
        if not opts then return false end
        local c = util.parse_options(opts, {
            update_merkle_tree = true
        })
        c.update_merkle_tree = assert(util.parse_number(c.update_merkle_tree),
                "invalid update_merkle_tree number in " .. all)
        concurrency_update_merkle_tree = c.update_merkle_tree
        return true
    end },
    { "^(%-%-initial%-proof%=(.+))$", function(all, opts)
        if not opts then return false end
        local p = util.parse_options(opts, {
            address = true,
            log2_size = true,
            filename = true
        })
        p.cmdline = all
        p.address = assert(util.parse_number(p.address),
            "invalid address in " .. all)
        p.log2_size = assert(util.parse_number(p.log2_size),
            "invalid log2_size in " .. all)
        assert(p.log2_size >= 3,
            "log2_size must be at least 3 in " .. all)
        initial_proof[#initial_proof+1] = p
        return true
    end },
    { "^(%-%-final%-proof%=(.+))$", function(all, opts)
        if not opts then return false end
        local p = util.parse_options(opts, {
            address = true,
            log2_size = true,
            filename = true
        })
        p.cmdline = all
        p.address = assert(util.parse_number(p.address),
            "invalid address in " .. all)
        p.log2_size = assert(util.parse_number(p.log2_size),
            "invalid log2_size in " .. all)
        assert(p.log2_size >= 3,
            "log2_size must be at least 3 in " .. all)
        final_proof[#final_proof+1] = p
        return true
    end },
    { "^%-%-no%-root%-flash%-drive$", function(all)
        if not all then return false end
        assert(flash_image_filename.root and flash_label_order[1] == "root",
                 "no root flash drive to remove")
        flash_image_filename.root = nil
        flash_start.root = nil
        flash_length.root = nil
        flash_shared.root = nil
        table.remove(flash_label_order, 1)
        rom_bootargs = "console=hvc0"
        return true
    end },
    { "^%-%-dump%-pmas$", function(all)
        if not all then return false end
        dump_pmas = true
        return true
    end },
    { "^%-%-step$", function(all)
        if not all then return false end
        step = true
        return true
    end },
    { "^(%-%-max%-mcycle%=(.*))$", function(all, n)
        if not n then return false end
        max_mcycle = assert(util.parse_number(n), "invalid option " .. all)
        return true
    end },
    { "^%-%-load%=(.*)$", function(o)
        if not o or #o < 1 then return false end
        load_dir = o
        return true
    end },
    { "^%-%-store%=(.*)$", function(o)
        if not o or #o < 1 then return false end
        store_dir = o
        return true
    end },
    { "^%-%-remote%-address%=(.*)$", function(o)
        if not o or #o < 1 then return false end
        remote_address = o
        return true
    end },
    { "^%-%-checkin%-address%=(.*)$", function(o)
        if not o or #o < 1 then return false end
        checkin_address = o
        return true
    end },
    { "^%-%-remote%-shutdown$", function(o)
        if not o then return false end
        remote_shutdown = true
        return true
    end },
    { "^%-%-json%-steps%=(.*)$", function(o)
        if not o or #o < 1 then return false end
        json_steps = o
        return true
    end },
    { "^%-%-initial%-hash$", function(all)
        if not all then return false end
        initial_hash = true
        return true
    end },
    { "^%-%-final%-hash$", function(all)
        if not all then return false end
        final_hash = true
        return true
    end },
    { "^(%-%-periodic%-hashes%=(.*))$", function(all, v)
        if not v then return false end
        string.gsub(v, "^([^%,]+),(.+)$", function(p, s)
            periodic_hashes_period = assert(util.parse_number(p),
                "invalid period " .. all)
            periodic_hashes_start = assert(util.parse_number(s),
                "invalid start " .. all)
        end)
        if periodic_hashes_period == math.maxinteger then
            periodic_hashes_period = assert(util.parse_number(v),
                "invalid period " .. all)
            periodic_hashes_start = 0
        end
        initial_hash = true
        final_hash = true
        return true
    end },
    { "^%-%-store%-config(%=?)(%g*)$", function(o, v)
        if not o then return false end
        if o == '=' then
            if not v or #v < 1 then return false end
            store_config = v
        else
            store_config = stderr
        end
        return true
    end },
    { "^%-%-load%-config%=(%g*)$", function(o)
        if not o or #o < 1 then return false end
        load_config = o
        return true
    end },
    { "^(%-%-rollup%-rx%-buffer%=(.+))$", function(all, opts)
        if not opts then return false end
        rollup_rx_buffer = parse_memory_range(opts, "rollup rx buffer", all)
        return true
    end },
    { "^(%-%-rollup%-tx%-buffer%=(.+))$", function(all, opts)
        if not opts then return false end
        rollup_tx_buffer = parse_memory_range(opts, "tx buffer", all)
        return true
    end },
    { "^(%-%-rollup%-input%-metadata%=(.+))$", function(all, opts)
        if not opts then return false end
        rollup_input_metadata = parse_memory_range(opts, "rollup input metadata", all)
        return true
    end },
    { "^(%-%-rollup%-voucher%-hashes%=(.+))$", function(all, opts)
        if not opts then return false end
        rollup_voucher_hashes = parse_memory_range(opts, "rollup voucher hashes", all)
        return true
    end },
    { "^(%-%-rollup%-notice%-hashes%=(.+))$", function(all, opts)
        if not opts then return false end
        rollup_notice_hashes = parse_memory_range(opts, "rollup notice hashes", all)
        return true
    end },
    { ".*", function(all)
        if not all then return false end
        local not_option = all:sub(1,1) ~= "-"
        if not_option or all == "--" then
          cmdline_opts_finished = true
          if not_option then exec_arguments = { all } end
          return true
        end
        error("unrecognized option " .. all)
    end }
}

-- Process command line options
for i, a in ipairs(arg) do
    if not cmdline_opts_finished then
      for j, option in ipairs(options) do
          if option[2](a:match(option[1])) then
              break
          end
      end
    else
      exec_arguments[#exec_arguments+1] = a
    end
end

local function get_file_length(filename)
    local file = io.open(filename, "rb")
    if not file then return nil end
    local size = file:seek("end")    -- get file size
    file:close()
    return size
end

local function print_root_hash(cycles, machine)
    machine:update_merkle_tree()
    stderr("%u: %s\n", cycles, util.hexhash(machine:get_root_hash()))
end

local function store_memory_range(r, indent, output)
    local function comment_default(u, v)
        output(u == v and " -- default\n" or "\n")
    end
    output("{\n", i)
    output("%s  start = 0x%x,", indent, r.start)
    comment_default(0, r.start)
    output("%s  length = 0x%x,", indent, r.length)
    comment_default(0, r.length)
    if r.image_filename and r.image_filename ~= "" then
        output("%s  image_filename = %q,\n", indent, r.image_filename)
    end
    output("%s  shared = %s,", indent, tostring(r.shared or false))
    comment_default(false, r.shared)
    output("%s},\n", indent)
end

local function store_machine_config(config, output)
    local function comment_default(u, v)
        output(u == v and " -- default\n" or "\n")
    end

    local def
    if remote then
        def = remote.machine.get_default_config()
    else
        def = cartesi.machine.get_default_config()
    end
    output("machine_config = {\n")
    output("  processor = {\n")
    output("    x = {\n")
    local processor = config.processor or { x = {} }
    for i = 1, 31 do
        local xi = processor.x[i] or def.processor.x[i]
        output("      0x%x,",  xi)
        comment_default(xi, def.processor.x[i])
    end
    output("    },\n")
    local order = {}
    for i,v in pairs(def.processor) do
        if type(v) == "number" then
            order[#order+1] = i
        end
    end
    table.sort(order)
    for i,csr in ipairs(order) do
        local c = processor[csr] or def.processor[csr]
        output("    %s = 0x%x,", csr, c)
        comment_default(c,  def.processor[csr])
    end
    output("  },\n")
    local ram = config.ram or {}
    output("  ram = {\n")
    output("    length = 0x%x,", ram.length or def.ram.length)
    comment_default(ram.length, def.ram.length)
    output("    image_filename = %q,", ram.image_filename or def.ram.image_filename)
    comment_default(ram.image_filename, def.ram.image_filename)
    output("  },\n")
    local rom = config.rom or {}
    output("  rom = {\n")
    output("    image_filename = %q,", rom.image_filename or def.rom.image_filename)
    comment_default(rom.image_filename, def.rom.image_filename)
    output("    bootargs = %q,", rom.bootargs or def.rom.bootargs)
    comment_default(rom.bootargs, def.rom.bootargs)
    output("  },\n")
    local htif = config.htif or {}
    output("  htif = {\n")
    output("    tohost = 0x%x,", htif.tohost or def.htif.tohost)
    comment_default(htif.tohost, def.htif.tohost)
    output("    fromhost = 0x%x,", htif.fromhost or def.htif.fromhost)
    comment_default(htif.fromhost, def.htif.fromhost)
    output("    console_getchar = %s,", tostring(htif.console_getchar or false))
    comment_default(htif.console_getchar or false, def.htif.console_getchar)
    output("    yield_automatic = %s,", tostring(htif.yield_automatic or false))
    comment_default(htif.yield_automatic or false, def.htif.yield_automatic)
    output("    yield_manual = %s,", tostring(htif.yield_manual or false))
    comment_default(htif.yield_manual or false, def.htif.yield_manual)
    output("  },\n")
    local clint = config.clint or {}
    output("  clint = {\n")
    output("    mtimecmp = 0x%x,", clint.mtimecmp or def.clint.mtimecmp)
    comment_default(clint.mtimecmp, def.clint.mtimecmp)
    output("  },\n")
    local dhd = config.dhd or { h = {} }
    output("  dhd = {\n")
    output("    tstart = 0x%x,", dhd.tstart or def.dhd.tstart)
    comment_default(dhd.tstart, def.dhd.tstart)
    output("    tlength = 0x%x,", dhd.tlength or def.dhd.tlength)
    comment_default(dhd.tlength, def.dhd.tlength)
    if dhd.image_filename and dhd.image_filename ~= "" then
        output("      image_filename = %q,\n", dhd.image_filename)
    end
    output("    dlength = 0x%x,", dhd.dlength or def.dhd.dlength)
    comment_default(dhd.dlength, def.dhd.dlength)
    output("    hlength = 0x%x,", dhd.hlength or def.dhd.hlength)
    comment_default(dhd.hlength, def.dhd.hlength)
    output("    h = {\n")
    for i = 1, 4 do
        local hi = dhd.h[i] or def.dhd.h[i]
        output("      0x%x,",  hi)
        comment_default(hi, def.dhd.h[i])
    end
    output("    },\n")
    output("  },\n")
    output("  flash_drive = {\n")
    for i, f in ipairs(config.flash_drive) do
        output("    ")
        store_memory_range(f, "    ", output)
    end
    output("  },\n")
    output("  rollup = {\n")
    output("    rx_buffer = ")
    store_memory_range(config.rollup.rx_buffer, "    ", output)
    output("    tx_buffer = ")
    store_memory_range(config.rollup.tx_buffer, "    ", output)
    output("    input_metadata = ")
    store_memory_range(config.rollup.input_metadata, "    ", output)
    output("    voucher_hashes = ")
    store_memory_range(config.rollup.voucher_hashes, "    ", output)
    output("    notice_hashes = ")
    store_memory_range(config.rollup.notice_hashes, "    ", output)
    output("  },\n")
    output("}\n")
end

local function resolve_flash_lengths(label_order, image_filename, start, length)
    for i, label in ipairs(label_order) do
        local filename = image_filename[label]
        local len = length[label]
        local filelen
        if filename and filename ~= "" then
            filelen = assert(get_file_length(filename), string.format(
                "unable to find length of flash drive '%s' image file '%s'",
                label, filename))
            if len and len ~= filelen then
                error(string.format("flash drive '%s' length (%u) and image file '%s' length (%u) do not match", label,
                    len, filename, filelen))
            else
                length[label] = filelen
            end
        elseif not len then
            error(string.format(
                "flash drive '%s' nas no length or image file", label))
        end
    end
end

local function resolve_flash_starts(label_order, image_filename, start, length)
    local auto_start = 1<<63
    if next(start) == nil then
        for i, label in ipairs(label_order) do
            start[label] = auto_start
            auto_start = auto_start + (1 << 60)
        end
    else
        local missing = {}
        local found = {}
        for i, label in ipairs(label_order) do
            local quoted = string.format("'%s'", label)
            if start[label] then
                found[#found+1] = quoted
            else
                missing[#missing+1] = quoted
            end
        end
        if #missing > 0 then
            error(string.format("flash drive start set for %s but missing for %s",
                table.concat(found, ", "), table.concat(missing, ", ")))
        end
    end
end

local function dump_value_proofs(machine, desired_proofs, htif_console_getchar)
    if #desired_proofs > 0 then
        assert(not htif_console_getchar,
            "proofs are meaningless in interactive mode")
        machine:update_merkle_tree()
    end
    for i, desired in ipairs(desired_proofs) do
        local proof = machine:get_proof(desired.address, desired.log2_size)
        local out = desired.filename and assert(io.open(desired.filename, "wb"))
            or io.stdout
        out:write("{\n")
        util.dump_json_proof(proof, out, 1)
        out:write("}\n")
    end
end

local function append(a, b)
    a = a or ""
    b = b or ""
    if a == "" then return b end
    if b == "" then return a end
    return a .. " " .. b
end

local function create_machine(config_or_dir, runtime)
    if remote then
        return remote.machine(config_or_dir, runtime)
    end
    return cartesi.machine(config_or_dir, runtime)
end

local machine

if remote_address then
    assert(checkin_address, "checkin address missing")
    stderr("Listening for checkin at '%s'\n", checkin_address)
    stderr("Connecting to remote cartesi machine at '%s'\n", remote_address)
    cartesi.grpc = require"cartesi.grpc"
    remote = assert(cartesi.grpc.stub(remote_address, checkin_address))
    local v = assert(remote.get_version())
    stderr("Connected: remote version is %d.%d.%d\n", v.major, v.minor, v.patch)
    local shutdown = function() remote.shutdown() end
    if remote_shutdown then
        remote_shutdown = setmetatable({}, { __gc = function()
            stderr("Shutting down remote cartesi machine\n")
            pcall(shutdown)
        end })
    end
end

local runtime = {
    dhd = {
        source_address = dhd_source_address
    },
    concurrency = {
        update_merkle_tree = concurrency_update_merkle_tree
    }
}

if load_dir then
    stderr("Loading machine: please wait\n")
    machine = create_machine(load_dir, runtime)
else
    -- Resolve all device starts and lengths
    resolve_flash_lengths(flash_label_order, flash_image_filename, flash_start,
        flash_length)
    resolve_flash_starts(flash_label_order, flash_image_filename, flash_start,
        flash_length)

    -- Build machine config
    local config = {
        processor = {
            -- Request automatic default values for versioning CSRs
            mimpid = -1,
            marchid = -1,
            mvendorid = -1
        },
        rom = {
            image_filename = rom_image_filename,
            bootargs = rom_bootargs
        },
        ram = {
            image_filename = ram_image_filename,
            length = ram_length
        },
        htif = {
            console_getchar = htif_console_getchar,
            yield_automatic = htif_yield_automatic,
            yield_manual = htif_yield_manual
        },
        dhd = {
            tstart = dhd_tstart,
            tlength = dhd_tlength,
            image_filename = dhd_image_filename
        },
        rollup = {
            rx_buffer = rollup_rx_buffer,
            tx_buffer = rollup_tx_buffer,
            input_metadata = rollup_input_metadata,
            voucher_hashes = rollup_voucher_hashes,
            notice_hashes = rollup_notice_hashes
        },
        flash_drive = {},
    }

    local mtdparts = {}
    for i, label in ipairs(flash_label_order) do
        config.flash_drive[#config.flash_drive+1] = {
            image_filename = flash_image_filename[label],
            shared = flash_shared[label],
            start = flash_start[label],
            length = flash_length[label]
        }
        mtdparts[#mtdparts+1] = string.format("flash.%d:-(%s)", i-1, label)
    end
    if #mtdparts > 0 then
        config.rom.bootargs = append(config.rom.bootargs, "mtdparts=" ..
            table.concat(mtdparts, ";"))
    end

    config.rom.bootargs = append(config.rom.bootargs, append_rom_bootargs)

    if #exec_arguments > 0 then
        config.rom.bootargs = append(config.rom.bootargs, "-- " ..
            table.concat(exec_arguments, " "))
    end

    if load_config then
        local env = {}
        local ok, err = loadfile(load_config, 't', env)
        if ok then
            local chunk = ok
            ok, err = pcall(chunk)
        end
        if not ok then
            stderr("Failed to load machine config (%s):\n", load_config)
            error(err)
        end
        config = setmetatable(env.machine_config, {__index = config})
    end

    machine = create_machine(config, runtime)
end

-- obtain config from instantiated machine
local config = machine:get_initial_config()

for _,r in ipairs(memory_range_replace) do
    machine:replace_memory_range(r)
end

if type(store_config) == "string" then
    store_config = assert(io.open(store_config, "w"))
    store_machine_config(config, function (...) store_config:write(string.format(...)) end)
    store_config:close()
end

local htif_yield_reason = {
    [cartesi.machine.HTIF_YIELD_REASON_PROGRESS] = "progress",
    [cartesi.machine.HTIF_YIELD_REASON_RX_ACCEPTED] = "rx-accepted",
    [cartesi.machine.HTIF_YIELD_REASON_RX_REJECTED] = "rx-rejected",
    [cartesi.machine.HTIF_YIELD_REASON_TX_VOUCHER] = "tx-voucher",
    [cartesi.machine.HTIF_YIELD_REASON_TX_NOTICE] = "tx-notice",
    [cartesi.machine.HTIF_YIELD_REASON_TX_REPORT] = "tx-report",
}

local htif_yield_mode = {
    [cartesi.machine.HTIF_YIELD_MANUAL] = "Manual",
    [cartesi.machine.HTIF_YIELD_AUTOMATIC] = "Automatic",
}

local function is_power_of_two(value)
    return value > 0 and ((value & (value - 1)) == 0)
end

local function ilog2(value)
    value = assert(math.tointeger(value), "expected integer")
    assert(value ~= 0, "expected non-zero integer")
    local log = 0
    while value ~= 0 do
        log = log + 1
        value = value >> 1
    end
    return value
end

local function check_rollup_memory_range_config(range, name)
    assert(range, string.format("rollup range %s must be defined", name))
    assert(not range.shared, string.format("rollup range %s cannot be shared", name))
    assert(is_power_of_two(range.length), string.format("rollup range %s length not a power of two (%u)", name,
        range.length))
    local log = ilog2(range.length)
    local aligned_start = (range.start >> log) << log
    assert(aligned_start == range.start,
        string.format("rollup range %s start not aligned to its power of two size", name))
    range.image_filename = nil
end

local function check_rollup_htif_config(htif)
    assert(not htif.console_getchar, "console getchar must be disabled for rollup")
    assert(htif.yield_manual, "yield manual must be enabled for rollup")
    assert(htif.yield_automatic, "yield automatic must be enabled for rollup")
end

local function get_and_print_yield(machine, htif)
    local cmd = machine:read_htif_tohost_cmd()
    local data = machine:read_htif_tohost_data()
    local reason = data >> 32
    if cmd == cartesi.machine.HTIF_YIELD_AUTOMATIC and reason == cartesi.machine.HTIF_YIELD_REASON_PROGRESS then
        stderr("Progress: %6.2f" .. (htif.console_getchar and "\n" or "\r"), data/10)
    else
        local cmd_str = htif_yield_mode[cmd] or "Unknown"
        local reason_str = htif_yield_reason[reason] or "unknown"
        stderr("\n%s yield %s: %u\n", cmd_str, reason_str, data)
        stderr("Cycles: %u\n", machine:read_mcycle())
    end
    return cmd, reason, data
end

local function save_rollup_hashes(machine, range, filename)
    local hash_len = 32
    local f = assert(io.open(filename, "wb"))
    local zeros = string.rep("\0", hash_len)
    local offset = 0
    while offset < range.length do
        local hash = machine:read_memory(range.start+offset, 32)
        if hash == zeros then
            break
        end
        assert(f:write(hash))
        offset = offset + hash_len
    end
    f:close()
end

local function instantiate_filename(pattern, values)
    -- replace escaped % with something safe
    pattern = string.gsub(pattern, "%\\%%", "\0")
    pattern = string.gsub(pattern, "%%(%a)", function(s)
        return values[s] or s
    end)
    -- restore escaped %
    return (string.gsub(pattern, "\0", "%"))
end

local function save_rollup_voucher_and_notice_hashes(machine, config, advance)
    local values = { e = advance.epoch_index, i = advance.next_input_index - 1 }
    save_rollup_hashes(machine, config.voucher_hashes, instantiate_filename(advance.voucher_hashes, values))
    save_rollup_hashes(machine, config.notice_hashes, instantiate_filename(advance.notice_hashes, values))
end

local function load_memory_range(machine, config, filename)
    local f = assert(io.open(filename, "rb"))
    local s = assert(f:read("*a"))
    f:close()
    machine:write_memory(config.start, s)
end

local function load_rollup_input_and_metadata(machine, config, advance)
    local values = { e = advance.epoch_index, i = advance.next_input_index }
    machine:replace_memory_range(config.input_metadata) -- clear
    load_memory_range(machine, config.input_metadata, instantiate_filename(advance.input_metadata, values))
    machine:replace_memory_range(config.rx_buffer) -- clear
    load_memory_range(machine, config.rx_buffer, instantiate_filename(advance.input, values))
    machine:replace_memory_range(config.voucher_hashes) -- clear
    machine:replace_memory_range(config.notice_hashes) -- clear
end

local function load_rollup_query(machine, config, inspect)
    machine:replace_memory_range(config.rx_buffer) -- clear
    load_memory_range(machine, config.rx_buffer, inspect.query) -- load query payload
end

local function save_rollup_advance_state_voucher(machine, config, advance)
    local values = { e = advance.epoch_index, i = advance.next_input_index-1, o =  advance.voucher_index }
    local f = assert(io.open(instantiate_filename(advance.voucher, values), "wb"))
    -- skip address and offset to reach payload length
    local length = string.unpack(">I8", machine:read_memory(config.start+3*32-8, 8))
    -- add address, offset, and payload length to amount to be read
    length = length+3*32
    assert(f:write(machine:read_memory(config.start, length)))
    f:close()
end

local function save_rollup_advance_state_notice(machine, config, advance)
    local values = { e = advance.epoch_index, i = advance.next_input_index-1, o =  advance.notice_index }
    local f = assert(io.open(instantiate_filename(advance.notice, values), "wb"))
    -- skip offset to reach payload length
    local length = string.unpack(">I8", machine:read_memory(config.start+2*32-8, 8))
    -- add offset and payload length to amount to be read
    length = length+2*32
    assert(f:write(machine:read_memory(config.start, length)))
    f:close()
end

local function save_rollup_advance_state_report(machine, config, advance)
    local values = { e = advance.epoch_index, i = advance.next_input_index-1, o =  advance.report_index }
    local f = assert(io.open(instantiate_filename(advance.report, values), "wb"))
    -- skip offset to reach payload length
    local length = string.unpack(">I8", machine:read_memory(config.start+2*32-8, 8))
    -- add offset and payload length to amount to be read
    length = length+2*32
    assert(f:write(machine:read_memory(config.start, length)))
    f:close()
end

local function save_rollup_inspect_state_report(machine, config, inspect)
    local values = { o =  inspect.report_index }
    local f = assert(io.open(instantiate_filename(inspect.report, values), "wb"))
    -- skip offset to reach payload length
    local length = string.unpack(">I8", machine:read_memory(config.start+2*32-8, 8))
    -- add offset and payload length to amount to be read
    length = length+2*32
    assert(f:write(machine:read_memory(config.start, length)))
    f:close()
end

if json_steps then
    assert(not rollup_advance, "json-steps and rollup advance state are incompatible")
    assert(not rollup_inspect, "json-steps and rollup inspect state are incompatible")
    assert(not config.htif.console_getchar, "logs are meaningless in interactive mode")
    json_steps = assert(io.open(json_steps, "w"))
    json_steps:write("[\n")
    local log_type = {} -- no proofs or annotations
    local first_cycle = machine:read_mcycle()
    while not machine:read_iflags_H() do
        local init_cycles = machine:read_mcycle()
        if init_cycles == max_mcycle then break end
        machine:reset_iflags_Y() -- move past any potential yield
        local log = machine:step(log_type)
        local final_cycles = machine:read_mcycle()
        if init_cycles ~= first_cycle then json_steps:write(',\n') end
        util.dump_json_log(log, init_cycles, final_cycles, json_steps, 1)
        stderr("%u -> %u\n", init_cycles, final_cycles)
    end
    json_steps:write('\n]\n')
    json_steps:close()
    if store_dir then
        stderr("Storing machine: please wait\n")
        machine:store(store_dir)
    end
else
    if config.htif.console_getchar then
        stderr("Running in interactive mode!\n")
    end
    if store_config == stderr then
        store_machine_config(config, stderr)
    end
    if rollup_advance or rollup_inspect then
        check_rollup_htif_config(config.htif)
        assert(config.rollup, "rollup device must be present")
        assert(remote_address, "rollup requires --remote-address for snapshot/rollback")
        check_rollup_memory_range_config(config.rollup.tx_buffer, "tx-buffer")
        check_rollup_memory_range_config(config.rollup.rx_buffer, "rx-buffer")
        check_rollup_memory_range_config(config.rollup.input_metadata, "input-metadata")
        check_rollup_memory_range_config(config.rollup.voucher_hashes, "voucher-hashes")
        check_rollup_memory_range_config(config.rollup.notice_hashes, "notice-hashes")
    end
    local cycles = machine:read_mcycle()
    if initial_hash then
        assert(not config.htif.console_getchar, "hashes are meaningless in interactive mode")
        print_root_hash(cycles, machine)
    end
    dump_value_proofs(machine, initial_proof, config.htif.console_getchar)
    local payload = 0
    local next_hash_mcycle
    if periodic_hashes_start ~= 0 then
        next_hash_mcycle = periodic_hashes_start
    else
        next_hash_mcycle = periodic_hashes_period
    end
    -- the loop runs at most until max_mcycle. iterations happen because
    --   1) we stopped to print a hash
    --   2) the machine halted, so iflags_H is set
    --   3) the machine yielded manual, so iflags_Y is set
    --   4) the machine yielded automatic, so iflags_X is set
    -- if the user selected the rollup advance state, then at every yield manual we check the reason
    -- if the reason is rx-rejected, we rollback, otherwise it must be rx-accepted.
    -- we then feed the next input, reset iflags_Y, snapshot, and resume the machine
    -- the machine can now continue processing and may yield automatic to produce vouchers, notices, and reports we save
    -- once all inputs for advance state have been consumed, we check if the user selected rollup inspect state
    -- if so, we feed the query, reset iflags_Y, and resume the machine
    -- the machine can now continue processing and may yield automatic to produce reports we save
    while math.ult(cycles, max_mcycle) do
        machine:run(math.min(next_hash_mcycle, max_mcycle))
        cycles = machine:read_mcycle()
        -- deal with halt
        if machine:read_iflags_H() then
            payload = machine:read_htif_tohost_data() >> 1
            if payload ~= 0 then
                stderr("\nHalted with payload: %u\n", payload)
            else
                stderr("\nHalted\n")
            end
            stderr("Cycles: %u\n", cycles)
            break
        -- deal with yield manual
        elseif machine:read_iflags_Y() then
            local cmd, reason = get_and_print_yield(machine, config.htif)
            -- there are advance state inputs to feed
            if rollup_advance and rollup_advance.next_input_index < rollup_advance.input_index_end then
                if reason == cartesi.machine.HTIF_YIELD_REASON_RX_REJECTED then
                    machine:rollback()
                    cycles = machine:read_mcycle()
                else
                    assert(reason == cartesi.machine.HTIF_YIELD_REASON_RX_ACCEPTED, "invalid manual yield reason")
                end
                stderr("\nEpoch %d before input %d\n", rollup_advance.epoch_index, rollup_advance.next_input_index)
                if rollup_advance.hashes then
                    print_root_hash(cycles, machine)
                end
                -- save only if we have already run an input
                if rollup_advance.next_input_index > rollup_advance.input_index_begin then
                    save_rollup_voucher_and_notice_hashes(machine, config.rollup, rollup_advance)
                end
                machine:snapshot()
                load_rollup_input_and_metadata(machine, config.rollup, rollup_advance)
                machine:reset_iflags_Y()
                machine:write_htif_fromhost_data(0) -- tell machine it is an rollup_advance state, but this is default
                rollup_advance.voucher_index = 0
                rollup_advance.notice_index = 0
                rollup_advance.report_index = 0
                rollup_advance.next_input_index = rollup_advance.next_input_index + 1
            else
                -- there are outputs of a prevous advance state to save
                if rollup_advance and rollup_advance.next_input_index > rollup_advance.input_index_begin then
                    save_rollup_voucher_and_notice_hashes(machine, config.rollup, rollup_advance)
                end
                -- there is an inspect state query to feed
                if rollup_inspect and rollup_inspect.query then
                    stderr("\nBefore query\n")
                    load_rollup_query(machine, config.rollup, rollup_inspect)
                    machine:reset_iflags_Y()
                    machine:write_htif_fromhost_data(1) -- tell machine it is an inspect state
                    rollup_inspect.report_index = 0
                    rollup_inspect.query = nil
                    rollup_advance = nil
                end
            end
        -- deal with yield automatic
        elseif machine:read_iflags_X() then
            local cmd, reason = get_and_print_yield(machine, config.htif)
            -- we have fed an advance state input
            if rollup_advance and rollup_advance.next_input_index > rollup_advance.input_index_begin then
                if reason == cartesi.machine.HTIF_YIELD_REASON_TX_VOUCHER then
                    save_rollup_advance_state_voucher(machine, config.rollup.tx_buffer, rollup_advance)
                    rollup_advance.voucher_index = rollup_advance.voucher_index + 1
                elseif reason == cartesi.machine.HTIF_YIELD_REASON_TX_NOTICE then
                    save_rollup_advance_state_notice(machine, config.rollup.tx_buffer, rollup_advance)
                    rollup_advance.notice_index = rollup_advance.notice_index + 1
                elseif reason == cartesi.machine.HTIF_YIELD_REASON_TX_REPORT then
                    save_rollup_advance_state_report(machine, config.rollup.tx_buffer, rollup_advance)
                    rollup_advance.report_index = rollup_advance.report_index + 1
                end
                -- ignore other reasons
            -- we have feed the inspect state query
            elseif rollup_inspect and not rollup_inspect.query then
                if reason == cartesi.machine.HTIF_YIELD_REASON_TX_REPORT then
                    save_rollup_inspect_state_report(machine, config.rollup.tx_buffer, rollup_inspect)
                    rollup_inspect.report_index = rollup_inspect.report_index + 1
                end
                -- ignore other reasons
            end
            -- otherwise ignore
        end
        if machine:read_iflags_Y() then
            break
        end
        if cycles == next_hash_mcycle then
            print_root_hash(cycles, machine)
            next_hash_mcycle = next_hash_mcycle + periodic_hashes_period
        end
    end
    if not math.ult(cycles, max_mcycle) then
        stderr("\nCycles: %u\n", cycles)
    end
    if step then
        assert(not config.htif.console_getchar, "step proof is meaningless in interactive mode")
        stderr("Gathering step proof: please wait\n")
        util.dump_log(machine:step{ proofs = true, annotations = true }, io.stderr)
    end
    if dump_pmas then
        machine:dump_pmas()
    end
    if final_hash then
        assert(not config.htif.console_getchar, "hashes are meaningless in interactive mode")
        print_root_hash(cycles, machine)
    end
    dump_value_proofs(machine, final_proof, config.htif.console_getchar)
    if store_dir then
        assert(not config.htif.console_getchar, "hashes are meaningless in interactive mode")
        stderr("Storing machine: please wait\n")
        machine:store(store_dir)
    end
    machine:destroy()
    os.exit(payload, true)
end
