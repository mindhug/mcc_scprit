/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#include <archcap.h>

#include "compartment_globals.h"
#include "utils/align.h"

// This file overrides mmap and related functions by using the wrapper technique: ld is instructed
// to redirect all references to <sym> to __wrap_<sym> instead, and the original function can be
// called by using __real_<sym>. See --wrap in ld(1) for more information.
// Note that all this only works for static executables, because we need to override references to
// mmap() inside libc itself. In the dynamic case, we would need to use another technique (probably
// symbol interposition).

extern "C" void* __real_mmap(void*, size_t, int, int, int, off_t);

// Restrict mappings to the compartment's range by using MAP_FIXED and global variables, initialised
// by the compartment manager to the base and top of the range reserved to this compartment's
// mappings.
// This is effectively a very crude memory allocator, carving out pages from the compartment's
// range for every mmap() call, without making them available to the compartment again on munmap().
extern "C" void* __wrap_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
  if (flags & MAP_FIXED) {
    // Allow MAP_FIXED as long as the mapping is within the range of DDC.
    uintcap_t ddc = archcap_c_ddc_get();
    ptraddr_t ddc_base = archcap_c_base_get(ddc);
    ptraddr_t ddc_limit = archcap_c_limit_get(ddc);
    ptraddr_t map_base = reinterpret_cast<ptraddr_t>(addr);

    // Check for overflow, and then the bounds.
    if (map_base + length >= map_base &&
        ddc_base < map_base && map_base + length < ddc_limit) {
      return __real_mmap(addr, length, prot, flags, fd, offset);
    }

    // Out of bound mapping or overflow.
    errno = EINVAL;
    return MAP_FAILED;
  }

  size_t aligned_length = align_up(length, sysconf(_SC_PAGE_SIZE));

  // Refuse to allocate more than the remaining range allows.
  if (aligned_length > COMPARTMENT_MMAP_RANGE_TOP_SYMBOL - COMPARTMENT_MMAP_RANGE_BASE_SYMBOL) {
    errno = ENOMEM;
    return MAP_FAILED;
  }

  // Ignore addr if MAP_FIXED is not specified.
  ptraddr_t map_addr = COMPARTMENT_MMAP_RANGE_TOP_SYMBOL - aligned_length;

  void* res = __real_mmap(reinterpret_cast<void*>(map_addr), length, prot, flags | MAP_FIXED,
                          fd, offset);

  // Update the top of the remaining range. This is clearly not thread-safe, some kind of atomics or
  // mutex would be needed to support compartments with multiple threads.
  if (res != MAP_FAILED)
    COMPARTMENT_MMAP_RANGE_TOP_SYMBOL = map_addr;

  return res;
}

// The compartment manager maps the entire compartment's range as PROT_NONE, to reserve the range
// for this compartment. Therefore, we must not create any hole in this mapping.
//
// We could simply use mprotect() with PROT_NONE to revert the permissions, but this does not have
// all the desirable properties munmap() has, notably: allow the kernel to discard the backing
// memory for private mappings, and sync modifications and remove the file reference for
// file-backed, shared mappings.  Instead, we mmap() the range again, which does exactly what we
// want: atomically munmap() the range (with all the desirable side effects) and create a new
// mapping with the same range. Another nice side effect is that the range will be checked against
// DDC like a normal mmap().
//
// Note that the allocation algorithm is very simplistic: mappings never get recycled.
extern "C" int __wrap_munmap(void* addr, size_t length) {
  void* ret = __wrap_mmap(addr, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  return (ret == MAP_FAILED ? -1 : 0);
}

// Supporting brk() and sbrk() is tricky, as the program break is a property of the process.
// They could potentially be emulated, but since there is virtually no reason for compartments to
// use them, we just make them error out.
extern "C" int __wrap_brk(void*) {
  errno = ENOMEM;
  return -1;
}

extern "C" void* __wrap_sbrk(intptr_t) {
  errno = ENOMEM;
  return reinterpret_cast<void*>(-1);
}
