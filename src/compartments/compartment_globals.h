/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "compartment_interface.h"

#include <stdint.h>

extern "C" {
  extern uintcap_t (* __capability COMPARTMENT_MANAGER_CALL_CAPABILITY_SYMBOL)(
        CompartmentId, uintcap_t, uintcap_t, uintcap_t, uintcap_t, uintcap_t, uintcap_t);
  // Use the noreturn attribute because [[noreturn]] cannot be used on function pointers.
  extern void (* __capability COMPARTMENT_MANAGER_RETURN_CAPABILITY_SYMBOL)(uintcap_t)
      __attribute__((noreturn));

  extern ptraddr_t COMPARTMENT_MMAP_RANGE_BASE_SYMBOL;
  extern ptraddr_t COMPARTMENT_MMAP_RANGE_TOP_SYMBOL;
}
