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

#ifndef I_UARCH_STATE_ACCESS_H
#define I_UARCH_STATE_ACCESS_H

#include "bracket-note.h"
#include "pma.h"

namespace cartesi {

// Interface for microarchitecture state access
template <typename DERIVED>
class i_uarch_step_state_access { // CRTP

    __device__ DERIVED &derived(void) {
        return *static_cast<DERIVED *>(this);
    }

    __device__ const DERIVED &derived(void) const {
        return *static_cast<const DERIVED *>(this);
    }

public:
    /// \brief Adds an annotation bracket to the log
    /// \param type Type of bracket
    /// \param text String with the text for the annotation
    __device__ void push_bracket(bracket_type type, const char *text) {
        return derived().do_push_bracket(type, text);
    }

    /// \brief Adds annotations to the state, bracketing a scope
    /// \param text String with the text for the annotation
    /// \returns An object that, when constructed and destroyed issues an annonation.
    __device__ auto make_scoped_note(const char *text) {
        return derived().do_make_scoped_note(text);
    }

    __device__ auto read_x(int r) {
        return derived().do_read_x(r);
    }

    __device__ auto write_x(int r, uint64_t v) {
        return derived().do_write_x(r, v);
    }

    __device__ auto read_pc() {
        return derived().do_read_pc();
    }

    __device__ auto write_pc(uint64_t v) {
        return derived().do_write_pc(v);
    }

    __device__ auto read_cycle() {
        return derived().do_read_cycle();
    }

    __device__ auto read_halt_flag() {
        return derived().do_read_halt_flag();
    }

    __device__ auto set_halt_flag() {
        return derived().do_set_halt_flag();
    }

    __device__ auto reset_halt_flag() {
        return derived().do_reset_halt_flag();
    }

    __device__ auto write_cycle(uint64_t v) {
        return derived().do_write_cycle(v);
    }

    __device__ uint64_t read_word(uint64_t paddr) {
        return derived().do_read_word(paddr);
    }

    __device__ void write_word(uint64_t paddr, uint64_t data) {
        return derived().do_write_word(paddr, data);
    }
};

} // namespace cartesi

#endif
