/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>
#include <vector>

#include "compartment_interface.h"

void CompartmentManagerInit();

// Add a compartment to the manager and initialize it (run it until main()).
// Arguments:
// - id: compartment ID, must be less than MAX_COMPARTMENTS and not allocated to an existing
//       compartment.
// - path: path to the compartment ELF file
// - args: arguments to pass to the compartment when initializing it
// - memory_range_length: size of the range reserved to the compartment
void CompartmentAdd(CompartmentId id, const std::string& path, const std::vector<std::string>& args,
                    size_t memory_range_length);
