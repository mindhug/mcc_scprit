/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

// Member offsets and size of the Compartment struct, to be used from assembly.
#define COMPARTMENT_STRUCT_CSP_OFFSET                   0
#define COMPARTMENT_STRUCT_CTPIDR_OFFSET                32
#define COMPARTMENT_STRUCT_UPDATE_ON_RETURN_OFFSET      64
#define COMPARTMENT_STRUCT_SIZE                         80

#define MAX_COMPARTMENTS                                2

#ifndef __ASSEMBLY__

struct Compartment {
  void* __capability csp;
  void* __capability ddc;
  void* __capability ctpidr;
  void* __capability entry_point;
  // If set to true, when the compartment returns, CompartmentSwitch saves the compartment's new
  // register values (except PCC) to its descriptor.
  bool update_on_return;
};

// Make sure that the offsets and size match what the assembly implementation expects.
// Members are loaded in pairs, so we make sure that every other member follows the member that
// the assembly expects.
static_assert(offsetof(Compartment, csp) ==
              COMPARTMENT_STRUCT_CSP_OFFSET, "");
static_assert(offsetof(Compartment, ddc) ==
              COMPARTMENT_STRUCT_CSP_OFFSET + sizeof(void* __capability), "");
static_assert(offsetof(Compartment, ctpidr) ==
              COMPARTMENT_STRUCT_CTPIDR_OFFSET , "");
static_assert(offsetof(Compartment, entry_point) ==
              COMPARTMENT_STRUCT_CTPIDR_OFFSET + sizeof(void* __capability), "");
static_assert(offsetof(Compartment, update_on_return) ==
              COMPARTMENT_STRUCT_UPDATE_ON_RETURN_OFFSET, "");
static_assert(sizeof(Compartment) == COMPARTMENT_STRUCT_SIZE, "");

extern "C" {
  void CompartmentSwitch(CompartmentId id, uintcap_t, uintcap_t, uintcap_t,
                         uintcap_t, uintcap_t, uintcap_t);

  void CompartmentSwitchReturn();
};

#endif // __ASSEMBLY__
