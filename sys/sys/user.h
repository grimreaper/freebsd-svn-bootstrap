/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)user.h	8.2 (Berkeley) 9/23/93
 * $Id: user.h,v 1.18 1999/01/07 21:23:47 julian Exp $
 */

#ifndef _SYS_USER_H_
#define _SYS_USER_H_

#include <machine/pcb.h>
#ifndef KERNEL
/* stuff that *used* to be included by user.h, or is now needed */
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/ucred.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <vm/vm.h>		/* XXX */
#include <vm/vm_param.h>	/* XXX */
#include <vm/pmap.h>		/* XXX */
#include <sys/lock.h>		/* XXX */
#include <vm/vm_map.h>		/* XXX */
#endif /* !KERNEL */
#ifndef _SYS_RESOURCEVAR_H_
#include <sys/resourcevar.h>
#endif
#ifndef _SYS_SIGNALVAR_H_
#include <sys/signalvar.h>
#endif

/*
 * KERN_PROC subtype ops return arrays of augmented proc structures:
 */
struct kinfo_proc {
	struct	proc kp_proc;			/* proc structure */
	struct	eproc {
		struct	proc *e_paddr;		/* address of proc */
		struct	session *e_sess;	/* session pointer */
		struct	pcred e_pcred;		/* process credentials */
		struct	ucred e_ucred;		/* current credentials */
#ifdef COMPAT_LINUX_THREADS
		struct  procsig e_procsig;	/* shared signal structure */
#endif
		struct	vmspace e_vm;		/* address space */
		pid_t	e_ppid;			/* parent process id */
		pid_t	e_pgid;			/* process group id */
		short	e_jobc;			/* job control counter */
		dev_t	e_tdev;			/* controlling tty dev */
		pid_t	e_tpgid;		/* tty process group id */
		struct	session *e_tsess;	/* tty session pointer */
#define	WMESGLEN	7
		char	e_wmesg[WMESGLEN+1];	/* wchan message */
		segsz_t e_xsize;		/* text size */
		short	e_xrssize;		/* text rss */
		short	e_xccount;		/* text references */
		short	e_xswrss;
		long	e_flag;
#define	EPROC_CTTY	0x01	/* controlling tty vnode active */
#define	EPROC_SLEADER	0x02	/* session leader */
		char	e_login[roundup(MAXLOGNAME, sizeof(long))];	/* setlogin() name */
		long	e_spare[2];
	} kp_eproc;
};
void fill_eproc __P((struct proc *, struct eproc *));


/*
 * Per process structure containing data that isn't needed in core
 * when the process isn't running (esp. when swapped out).
 * This structure may or may not be at the same kernel address
 * in all processes.
 */

struct	user {
	struct	pcb u_pcb;
	struct	sigacts u_sigacts;	/* p_sigacts points here (use it!) */
	struct	pstats u_stats;		/* p_stats points here (use it!) */
	/*
	 * Remaining fields only for core dump and/or ptrace--
	 * not valid at other times!
	 */
	struct	kinfo_proc u_kproc;	/* proc + eproc */
	struct	md_coredump u_md;	/* machine dependent glop */
};

/*
 * Redefinitions to make the debuggers happy for now...  This subterfuge
 * brought to you by coredump() and trace_req().  These fields are *only*
 * valid at those times!
 */
#ifdef __i386__
#define	U_ar0	u_kproc.kp_proc.p_md.md_regs /* copy of curproc->p_md.md_regs */
#endif
#ifdef __alpha__
#define	U_ar0	u_kproc.kp_proc.p_md.md_tf /* copy of curproc->p_md.md_tf */
#endif
#define	U_tsize	u_kproc.kp_eproc.e_vm.vm_tsize
#define	U_dsize	u_kproc.kp_eproc.e_vm.vm_dsize
#define	U_ssize	u_kproc.kp_eproc.e_vm.vm_ssize
#ifndef COMPAT_LINUX_THREADS
#define	U_sig	u_sigacts.ps_sig
#define	U_code	u_sigacts.ps_code
#else
#define	U_sig	u_kproc.kp_proc.p_sig
#define	U_code	u_kproc.kp_proc.p_code
#endif /* COMPAT_LINUX_THREADS */

#ifndef KERNEL
#define	u_ar0	U_ar0
#define	u_tsize	U_tsize
#define	u_dsize	U_dsize
#define	u_ssize	U_ssize
#define	u_sig	U_sig
#define	u_code	U_code
#endif /* KERNEL */

#endif
