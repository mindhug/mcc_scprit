/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "compartment_interface.h"

// Causes the compartment to return to its caller (through the compartment manager).
// ret specifies the return value.
// Attention: when a compartment returns, all the stack frames between the compartment entry and
// return points are implicitly discarded.
[[noreturn]] void CompartmentReturn(uintcap_t ret = 0);

// Define the compartment's entry point, with 0 to 6 arguments. A compartment must define exactly
// one entry point. For instance:
// COMPARTMENT_ENTRY_POINT(int a, char b) {
//   ...
// }
#define COMPARTMENT_ENTRY_POINT(...) extern "C" void COMPARTMENT_ENTRY_SYMBOL(__VA_ARGS__)
