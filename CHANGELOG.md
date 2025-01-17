# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.8.0] - 2021-12-28

### Added

- Added control of concurrency to emulator runtime config
- Added new remote-cartesi-machine-proxy
- Added several new Merkle tree implementations with different flavors
- Added new --log2-word-size option to merkle-tree-hash
- Added new cartesi-server-manager to support input/output with rollups
- Added coverage tests with gcc and clang
- Added new --load-config and --store-config options to cartesi-machine.lua
- Added new rollup device in emulator to support Cartesi Servers
- Added rollup-memory-range.lua utility to encode/decode rollup inputs/outputs
- Added more and better tests
- Added new C API to machine class, exposed by libcartesi.so
- Added support for simulating rollups advance/inspect state to cartesi-machine.lua

### Fixed

- Fixed missing method to get CSR addresses in Lua bind
- Fixed missing DHD CSRs in Lua bind
- Fixed potential mcycle overflow in emulator
- Fixed machine::step by moving RTC interrup handling from machine::run to interpret
- Fixed gRPC threading by stopping/restarting server before/after fork in remote-cartesi-machine
- Fixed terminal configuration in remote-cartesi-machine

### Changed

- Changed marchid to 9
- Changed machine::run to only return on yield, halt, or when max\_mcycle is reached
- Changed WFI to noop to simplify code, thus eliminating flag I from iflags CSR
- Changed cartesi-machine-server to remote-cartesi-machine
- Changed Merkle tree proof structures to be more general
- Changed code with improvements suggested by clang-tidy
- Changed code with clang-format
- Changed Lua bind to use C API, cartesi.so links to libcartesi.so
- Changed from luapp to stock Lua interpreter
- Changed remote-cartesi-machine to check-in with client when starting/rollback/snapshot
- Changed machine::replace\_flash\_drive to machine::replace\_memory\_range
- Changed dependency from system provided gRPC libraries to a specific version added to third-party dependencies

## [Previous Versions]

[0.8.0]: https://github.com/cartesi/machine-emulator/releases/tag/v0.8.0
[0.7.0]: https://github.com/cartesi/machine-emulator/releases/tag/v0.7.0
[0.6.0]: https://github.com/cartesi/machine-emulator/releases/tag/v0.6.0
[0.5.1]: https://github.com/cartesi/machine-emulator/releases/tag/v0.5.1
[0.5.0]: https://github.com/cartesi/machine-emulator/releases/tag/v0.5.0
[0.4.0]: https://github.com/cartesi/machine-emulator/releases/tag/v0.4.0
[0.3.0]: https://github.com/cartesi/machine-emulator/releases/tag/v0.3.0
[0.2.0]: https://github.com/cartesi/machine-emulator/releases/tag/v0.2.0
[0.1.0]: https://github.com/cartesi/machine-emulator/releases/tag/v0.1.0
