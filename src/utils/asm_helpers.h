/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#ifndef __ASSEMBLY__
#error "This file must be included from assembly"
#endif

#define ENTRY(f) \
    .globl f; \
    .balign 16; \
    .type f, %function; \
    f: \
    .cfi_startproc

#define END(f) \
    .cfi_endproc; \
    .size f, .-f;

// Create a frame record at `sp + offset`, update fp accordingly and add CFI
// information to use the saved frame record.
// Note that it is assumed that the frame record is stored at the top of the
// frame, i.e. that `sp + offset + 16` is equal to the caller's stack pointer.
.macro create_frame_record offset:req
	stp	fp, lr, [sp, #\offset]
	add	fp, sp, #\offset
	.cfi_def_cfa fp, 16
	.cfi_offset lr, -8
	.cfi_offset fp, -16
.endm
