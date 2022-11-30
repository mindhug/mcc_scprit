/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "compartment_interface.h"

#include <archcap.h>

#include "compartment_interface_impl.h"
#include "compartment_manager_asm.h"

uintcap_t CompartmentCall(CompartmentId id,
                          uintcap_t arg0, uintcap_t arg1, uintcap_t arg2,
                          uintcap_t arg3, uintcap_t arg4, uintcap_t arg5) {
  // CompartmentCallImpl() uses a capability function pointer to call CompartmentSwitch(), derive
  // one from PCC. A capability branch is needed anyway, because CompartmentSwitch() returns to the
  // caller using CLR.
  return CompartmentCallImpl(id, arg0, arg1, arg2, arg3, arg4, arg5,
                             archcap_c_from_pcc(&CompartmentSwitch));
}
