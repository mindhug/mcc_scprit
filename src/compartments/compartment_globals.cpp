/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "compartment_globals.h"

// All the variables below are initialised by the compartment manager.
extern "C" {
  uintcap_t (* __capability COMPARTMENT_MANAGER_CALL_CAPABILITY_SYMBOL)(
        CompartmentId, uintcap_t, uintcap_t, uintcap_t, uintcap_t, uintcap_t, uintcap_t);
  void (* __capability COMPARTMENT_MANAGER_RETURN_CAPABILITY_SYMBOL)(uintcap_t)
      __attribute__((noreturn));

  ptraddr_t COMPARTMENT_MMAP_RANGE_BASE_SYMBOL;
  ptraddr_t COMPARTMENT_MMAP_RANGE_TOP_SYMBOL;
}
