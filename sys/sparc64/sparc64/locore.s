/*-
 * Copyright (c) 2001 Jake Burkholder.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/syscall.h>

#include <machine/asi.h>
#include <machine/asmacros.h>
#include <machine/pstate.h>
#include <machine/upa.h>

#include "assym.s"

	.register %g2,#ignore

	.globl	kernbase
	.set	kernbase,KERNBASE

/*
 * void _start(caddr_t metadata, u_int *state, u_int mid, u_int bootmid,
 *	       u_long ofw_vec)
 *
 * XXX: in am smp system the other cpus are started in the loader, but since
 * there's no way to look up a symbol there, we need to use the same entry
 * point.  So if the module id is not equal to bootcpu, jump to _mp_start.
 */
ENTRY(_start)
	/*
	 * Initialize misc state to known values.  Interrupts disabled, normal
	 * globals, windows flushed (cr = 0, cs = nwindows - 1), no clean
	 * windows, pil 0, and floating point disabled.
	 */
	wrpr	%g0, PSTATE_NORMAL, %pstate
	flushw
	wrpr	%g0, 0, %cleanwin
	wrpr	%g0, 0, %pil
	wr	%g0, 0, %fprs

#ifdef SMP
	/*
	 * If we're not the boot processor, go do other stuff.
	 */
	cmp	%o2, %o3
	be	%xcc, 1f
	 nop
	call	_mp_start
	 nop
	sir
1:
#endif

	/*
	 * Get onto our per-cpu panic stack, which precedes the struct pcpu in
	 * the per-cpu page.
	 */
	SET(pcpu0 + (PCPU_PAGES * PAGE_SIZE) - PC_SIZEOF, %l1, %l0)
	sub	%l0, SPOFF + CCFSZ, %sp

	/*
	 * Enable interrupts.
	 */
	wrpr	%g0, PSTATE_KERNEL, %pstate

	/*
	 * Do initial bootstrap to setup pmap and thread0.
	 */
	call	sparc64_init
	 nop

	/*
	 * Get onto thread0's kstack.
	 */
	sub	PCB_REG, SPOFF + CCFSZ, %sp

	/*
	 * And away we go.  This doesn't return.
	 */
	call	mi_startup
	 nop
	sir
	! NOTREACHED
END(_start)

/*
 * void cpu_setregs(struct pcpu *pc)
 */
ENTRY(cpu_setregs)
	ldx	[%o0 + PC_CURTHREAD], %o1
	ldx	[%o1 + TD_PCB], %o1

	/*
	 * Disable interrupts, normal globals.
	 */
	wrpr	%g0, PSTATE_NORMAL, %pstate

	/*
	 * Normal %g6 points to the current thread's pcb, and %g7 points to
	 * the per-cpu data structure.
	 */
	mov	%o1, PCB_REG
	mov	%o0, PCPU_REG

	/*
	 * Alternate globals.
	 */
	wrpr	%g0, PSTATE_ALT, %pstate

	/*
	 * Alternate %g5 points to a per-cpu panic stack, %g6 points to the
	 * current thread's pcb, and %g7 points to the per-cpu data structure.
	 */
	mov	%o0, ASP_REG
	mov	%o1, PCB_REG
	mov	%o0, PCPU_REG

	/*
	 * Interrupt globals.
	 */
	wrpr	%g0, PSTATE_INTR, %pstate

	/*
	 * Interrupt %g7 points to the per-cpu data structure.
	 */
	mov	%o0, PCPU_REG

	/*
	 * MMU globals.
	 */
	wrpr	%g0, PSTATE_MMU, %pstate

	/*
	 * MMU %g7 points to the user tsb.  Initialize it to something sane
	 * here to catch invalid use.
	 */
	mov	%g0, TSB_REG

	/*
	 * Normal globals again.
	 */
	wrpr	%g0, PSTATE_NORMAL, %pstate

	/*
	 * Force trap level 1 and take over the trap table.
	 */
	SET(tl0_base, %o2, %o1)
	wrpr	%g0, 1, %tl
	wrpr	%o1, 0, %tba

	/*
	 * Re-enable interrupts.
	 */
	wrpr	%g0, PSTATE_KERNEL, %pstate

	retl
	 nop
END(cpu_setregs)

/*
 * Signal trampoline, copied out to user stack.  Must be 16 byte aligned or
 * the argv and envp pointers can become misaligned.
 */
ENTRY(sigcode)
	call	%o4
	 nop
	add	%sp, SPOFF + CCFSZ + SF_UC, %o0
	mov	SYS_sigreturn, %g1
	ta	%xcc, 9
	mov	SYS_exit, %g1
	ta	%xcc, 9
	illtrap
	.align 16
esigcode:
END(sigcode)

DATA(szsigcode)
	.long	esigcode - sigcode
