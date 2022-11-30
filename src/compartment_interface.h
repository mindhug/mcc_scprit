/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#ifndef __ASSEMBLY__

#include <stddef.h>
#include <stdint.h>

#include <type_traits>

using CompartmentId = size_t;

// Allocated compartment IDs (statically to simplify things).
constexpr CompartmentId kClientCompartmentId = 0;
constexpr CompartmentId kServerCompartmentId = 1;
constexpr CompartmentId kComputeNodeACompartmentId = 3;
constexpr CompartmentId kComputeNodeBCompartmentId = 4;
constexpr CompartmentId kComputeNodeCCompartmentId = 5;

// Call into the compartment with the requested ID, with 0 to 6 arguments (they must all be passed
// in registers, so using a variadic prototype would not be a good idea).
// The compartment returns one value.
// This can be used from both the compartment manager (running in Executive), and compartments
// (running in Restricted).
uintcap_t CompartmentCall(CompartmentId id,
                          uintcap_t arg0 = 0, uintcap_t arg1 = 0, uintcap_t arg2 = 0,
                          uintcap_t arg3 = 0, uintcap_t arg4 = 0, uintcap_t arg5 = 0);

// Converts a variable of any type that is normally stored in an X or C register to uintcap_t,
// without extraneous instructions. Useful for functions that take uintcap_t as a catch-all type.
// Usage example:
// char* __capability data_cap = ...;
// CompartmentCall(id, AsUintcap(4), AsUintcap(kEnumVal), AsUintcap(data_cap));
//
// This overload is strictly for types fitting in an X register (scalar types whose size is 8 or
// less).
template <typename T, typename = std::enable_if_t<std::is_scalar<T>::value && sizeof(T) <= 8>>
static inline uintcap_t AsUintcap(T arg) {
  // There's no easy way to tell the compiler that a variable in an X register should be moved
  // to a C register, without conversion. Work around this by placing the argument in x0 and the
  // return value in c0.
  register T arg_ asm("x0") = arg;
  register uintcap_t ret asm("c0");
  // Let the compiler know that ret has been initialised by the register allocation above.
  asm("" : "=C"(ret) : "r"(arg_));
  return ret;
}

static inline uintcap_t AsUintcap(const void* __capability arg) {
  return reinterpret_cast<uintcap_t>(arg);
}

#endif // __ASSEMBLY__

// The macros below define the symbols that must be defined by every compartment and are looked up
// by the compartment manager.
// Apart from the entry symbol, all symbols are initialized by the compartment manager.
#define COMPARTMENT_ENTRY_SYMBOL __compartment_entry
#define COMPARTMENT_MANAGER_CALL_CAPABILITY_SYMBOL __compartment_manager_call
#define COMPARTMENT_MANAGER_RETURN_CAPABILITY_SYMBOL __compartment_manager_return
#define COMPARTMENT_MMAP_RANGE_BASE_SYMBOL __compartment_mmap_range_base
#define COMPARTMENT_MMAP_RANGE_TOP_SYMBOL __compartment_mmap_range_top
