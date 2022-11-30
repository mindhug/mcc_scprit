/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "compartment_interface.h"

#pragma clang diagnostic push
// Clang is unhappy about clobbering FP (see asm() below) when there is already a function call in
// the same function, namely the compartment manager's CompartmentCall() implementation. Since
// we do not actually clobber FP in the asm() statement, it is safe to ignore this warning here.
#pragma clang diagnostic ignored "-Winline-asm"

template <typename Fn>
static inline
uintcap_t CompartmentCallImpl(CompartmentId id,
                              uintcap_t arg0, uintcap_t arg1, uintcap_t arg2,
                              uintcap_t arg3, uintcap_t arg4, uintcap_t arg5,
                              Fn comp_switch_c_ptr) {
  register uintcap_t c0 asm("c0") = AsUintcap(id);
  register uintcap_t c1 asm("c1") = arg0;
  register uintcap_t c2 asm("c2") = arg1;
  register uintcap_t c3 asm("c3") = arg2;
  register uintcap_t c4 asm("c4") = arg3;
  register uintcap_t c5 asm("c5") = arg4;
  register uintcap_t c6 asm("c6") = arg5;

  asm("blr %[fn]"
      : "+C"(c0)
      : [fn]"C"(comp_switch_c_ptr), "C"(c1), "C"(c2), "C"(c3), "C"(c4), "C"(c5), "C"(c6)
      // Callee-saved registers are not preserved by the compartment switcher, so mark all of them
      // as clobbered to get the compiler to save and restore them.
      // Also mark FP and LR as clobbered, because we are effectively making a function call and
      // therefore the compiler should create a frame record.
      // Note that FP is not actually clobbered, because CompartmentSwitch() does preserve FP.
      : "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "fp", "lr");

  return c0;
}

#pragma clang diagnostic pop
