/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "compartment_interface.h"

#include "compartment_globals.h"
#include "compartment_interface_impl.h"

uintcap_t CompartmentCall(CompartmentId id,
                          uintcap_t arg0, uintcap_t arg1, uintcap_t arg2,
                          uintcap_t arg3, uintcap_t arg4, uintcap_t arg5) {
  // Call into the compartment manager using the capability function pointer it provided to the
  // compartment.
  return CompartmentCallImpl(id, arg0, arg1, arg2, arg3, arg4, arg5,
                             COMPARTMENT_MANAGER_CALL_CAPABILITY_SYMBOL);
}
