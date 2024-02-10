// Copyright Cartesi and individual authors (see AUTHORS)
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along
// with this program (see COPYING). If not, see <https://www.gnu.org/licenses/>.
//

#ifndef UARCH_STATE_ACCESS_H
#define UARCH_STATE_ACCESS_H

#include "i-uarch-step-state-access.h"

namespace cartesi {

class uarch_step_state_access : public i_uarch_step_state_access<uarch_step_state_access> {
public:
    /// \brief Constructor from machine and uarch states.
    /// \param um Reference to uarch state.
    /// \param m Reference to machine state.
    explicit __device__ uarch_step_state_access() { ; }
    /// \brief No copy constructor
    uarch_step_state_access(const uarch_step_state_access &) = delete;
    /// \brief No copy assignment
    uarch_step_state_access &operator=(const uarch_step_state_access &) = delete;
    /// \brief No move constructor
    uarch_step_state_access(uarch_step_state_access &&) = delete;
    /// \brief No move assignment
    uarch_step_state_access &operator=(uarch_step_state_access &&) = delete;
    /// \brief Default destructor
    ~uarch_step_state_access() = default;

private:
    friend i_uarch_step_state_access<uarch_step_state_access>;
    uint64_t x[32];
    uint64_t pc;
    uint64_t cycle;
    bool halt_flag;

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    __device__ void do_push_bracket(bracket_type type, const char *text) {
        (void) type;
        (void) text;
    }

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    __device__ int do_make_scoped_note(const char *text) {
        (void) text;
        return 0;
    }

    __device__ uint64_t do_read_x(int reg) const {
        return x[reg];
    }

    __device__ void do_write_x(int reg, uint64_t val) {
        assert(reg != 0);
        x[reg] = val;
    }

    __device__ uint64_t do_read_pc() const {
        return pc;
    }

    __device__ void do_write_pc(uint64_t val) {
        pc = val;
    }

    __device__ uint64_t do_read_cycle() const {
        return cycle;
    }

    __device__ void do_write_cycle(uint64_t val) {
        cycle = val;
    }

    __device__ bool do_read_halt_flag() const {
        return halt_flag;
    }

    __device__ void do_set_halt_flag() {
        halt_flag = true;
    }

    __device__ void do_reset_halt_flag() {
        halt_flag = false;
    }

    __device__ uint64_t do_read_word(uint64_t paddr) {
    }

    /// \brief Fallback to error on all other word sizes
    __device__ void do_write_word(uint64_t paddr, uint64_t data) {
    }
};

} // namespace cartesi

#endif
