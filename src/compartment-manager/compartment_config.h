/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <archcap.h>

// Default length of a compartment's memory range.
constexpr size_t kCompartmentMemoryRangeLength = 256 * 1024 * 1024;

constexpr size_t kCompartmentStackSize = 1024 * 1024;

// Environment variables propagated to the compartments.
constexpr const char* kCompartmentPropagatedEnv[] = {
  "PATH",
};

// Only keep the minimum permissions for compartment capabilities.
constexpr archcap_perms_t kCompartmentDataPerms =
    ARCHCAP_PERM_LOAD | ARCHCAP_PERM_LOAD_CAP | ARCHCAP_PERM_MORELLO_MUTABLE_LOAD |
    ARCHCAP_PERM_STORE | ARCHCAP_PERM_STORE_CAP | ARCHCAP_PERM_STORE_LOCAL_CAP |
    ARCHCAP_PERM_GLOBAL;
constexpr archcap_perms_t kCompartmentExecPerms =
    ARCHCAP_PERM_EXECUTE |
    ARCHCAP_PERM_GLOBAL;
