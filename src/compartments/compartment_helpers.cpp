/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "compartment_helpers.h"

#include "compartment_globals.h"

void CompartmentReturn(uintcap_t ret) {
  COMPARTMENT_MANAGER_RETURN_CAPABILITY_SYMBOL(ret);
}
