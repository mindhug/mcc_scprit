/*
 * Copyright (c) 2020 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "compartment_manager_asm.h"

#include "utils/asm_helpers.h"

// Register aliases. We would normally use .req, but we use macros instead so
// that we can use the aliases as arguments to clear_all_registers_except (see
// below).
#define comp_desc		x8
#define comp_id			x9
#define xtmp			x10
#define wtmp			w11
#define ctmp			c6
#define ctmp2			c7
#define comp_entry		c24
#define comp_csp		c25
#define comp_ddc		c26
#define comp_ctpidr		c27

// We use .irp for iterating over the register numbers (we can't use symbols
// because we need to do string concatenation), so we need to list them
// explicitly.
#define ALL_REGS_NR \
	0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,\
	16,17,18,19,20,21,22,23,24,25,26,27,28,29,30

// Clear all the general-purpose registers (including LR), except those passed
// as arguments.
.macro clear_all_registers_except exclusion_list:vararg
	// Iterate over all the registers.
	.irp	cur_reg, ALL_REGS_NR
	.set	clear_reg, 1

	// Iterate over the excluded register number list.
	.irp	excluded_reg, \exclusion_list
	// Registers can be specified as either x<n> or c<n>.
	.ifc	\excluded_reg, x\cur_reg
	.set	clear_reg, 0
	.else
	.ifc	\excluded_reg, c\cur_reg
	.set	clear_reg, 0
	.endif
	.endif
	.endr // .irp excluded_reg

	.if	clear_reg == 1
	mov	x\cur_reg, xzr
	.endif
	.endr // .irp cur_reg
.endm

ENTRY(CompartmentSwitch)
	// Frame record + space for a Compartment struct.
	sub	sp, sp, #(16 + COMPARTMENT_STRUCT_SIZE)
	create_frame_record offset=COMPARTMENT_STRUCT_SIZE

	// Shuffle around the arguments for the entry point right now to
	// simplify register allocation.
	mov	comp_id, x0
	mov	c0, c1
	mov	c1, c2
	mov	c2, c3
	mov	c3, c4
	mov	c4, c5
	mov	c5, c6

	// Check that the compartment ID is valid.
	cmp	comp_id, #MAX_COMPARTMENTS
	b.ge	.Linvalid_id

	// Get a pointer to the compartment descriptor.
	adrp	comp_desc, cm_compartments
	add	comp_desc, comp_desc, :lo12:cm_compartments
	mov	xtmp, #COMPARTMENT_STRUCT_SIZE
	madd	comp_desc, xtmp, comp_id, comp_desc

	// Load the compartment descriptor.
	ldp	comp_csp, comp_ddc, [comp_desc, #COMPARTMENT_STRUCT_CSP_OFFSET]
	ldp	comp_ctpidr, comp_entry, [comp_desc, #COMPARTMENT_STRUCT_CTPIDR_OFFSET]
	ldrb	wtmp, [comp_desc, #COMPARTMENT_STRUCT_UPDATE_ON_RETURN_OFFSET]

	// Check that the compartment descriptor has been initialised
	// (valid entry point).
	chktgd	comp_entry
	b.cc	.Linvalid_id // Branch if tag not set

	// Save Restricted capability registers, and CLR so that we know where
	// to return. We use the same layout as the Compartment struct.
	mrs	ctmp, rcsp_el0
	mrs	ctmp2, rddc_el0
	stp	ctmp, ctmp2, [sp, #COMPARTMENT_STRUCT_CSP_OFFSET]
	mrs	ctmp, rctpidr_el0
	stp	ctmp, clr, [sp, #COMPARTMENT_STRUCT_CTPIDR_OFFSET]
	// If update_on_return is set, store a pointer to the compartment
	// descriptor, otherwise store a null pointer.
	cmp	wtmp, #0
	csel	xtmp, comp_desc, xzr, ne
	str	xtmp, [sp, #COMPARTMENT_STRUCT_UPDATE_ON_RETURN_OFFSET]

	// Setup Restricted registers for the target compartment.
	msr	rcsp_el0, comp_csp
	msr	rddc_el0, comp_ddc
	msr	rctpidr_el0, comp_ctpidr

	// Clear all registers, except those we want to pass to the compartment
	// (arguments in c0 to c5) and the capability function pointer.
	// We also preserve FP (x29) to help with backtracing.
	// CLR must be cleared, otherwise the target compartment would obtain
	// an executable capability to the caller.
	clear_all_registers_except c0, c1, c2, c3, c4, c5, comp_entry, x29

	// Transfer control to the compartment. This may involve a switch from
	// Executive to Restricted (if the caller is the compartment manager),
	// so we must use BRR.
	brr	comp_entry

	.globl CompartmentSwitchReturn
CompartmentSwitchReturn:
	// The compartment has returned.
	// If a pointer to the compartment descriptor has been stored, save the
	// ambient capabilities of the compartment that has just returned.
	ldr	comp_desc, [sp, #COMPARTMENT_STRUCT_UPDATE_ON_RETURN_OFFSET]
	cbz	comp_desc, 1f
	mrs	ctmp, rcsp_el0
	mrs	ctmp2, rddc_el0
	stp	ctmp, ctmp2, [comp_desc, #COMPARTMENT_STRUCT_CSP_OFFSET]
	mrs	ctmp, rctpidr_el0
	str	ctmp, [comp_desc, #COMPARTMENT_STRUCT_CTPIDR_OFFSET]

1:
	// Restore the restricted state environment and return to the caller.
	ldp	ctmp, ctmp2, [sp, #COMPARTMENT_STRUCT_CSP_OFFSET]
	msr	rcsp_el0, ctmp
	msr	rddc_el0, ctmp2
	ldp	ctmp, clr, [sp, #COMPARTMENT_STRUCT_CTPIDR_OFFSET]
	msr	rctpidr_el0, ctmp
	ldr	fp, [sp, #COMPARTMENT_STRUCT_SIZE]
	add	sp, sp, #(16 + COMPARTMENT_STRUCT_SIZE)

	// We also need to clear the registers on the return path (which is
	// really just a reverse compartment call). c0 is the return value and
	// thefore is preserved.
	clear_all_registers_except c0, x29, c30

	// Return to the caller, which may be another compartment, so we must
	// use RETR to allow switching to Restricted.
	retr	clr

.Linvalid_id:
	// TODO: some error message.
	b abort
END(CompartmentSwitch)
