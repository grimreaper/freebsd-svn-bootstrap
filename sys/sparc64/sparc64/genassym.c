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
 *	from: @(#)genassym.c	5.11 (Berkeley) 5/10/91
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/assym.h>
#include <sys/errno.h>
#include <sys/ktr.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/ucontext.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_zone.h>

#include <machine/asi.h>
#include <machine/vmparam.h>
#include <machine/cpufunc.h>
#include <machine/fp.h>
#include <machine/frame.h>
#include <machine/fsr.h>
#include <machine/intr_machdep.h>
#include <machine/lsu.h>
#include <machine/pcb.h>
#include <machine/pstate.h>
#include <machine/setjmp.h>
#include <machine/sigframe.h>
#include <machine/smp.h>
#include <machine/pv.h>
#include <machine/tte.h>
#include <machine/tlb.h>
#include <machine/tsb.h>
#include <machine/tstate.h>
#include <machine/utrap.h>

ASSYM(KERNBASE, KERNBASE);

ASSYM(EFAULT, EFAULT);
ASSYM(ENAMETOOLONG, ENAMETOOLONG);

ASSYM(KSTACK_PAGES, KSTACK_PAGES);
ASSYM(KSTACK_GUARD_PAGES, KSTACK_GUARD_PAGES);
ASSYM(UAREA_PAGES, UAREA_PAGES);
ASSYM(PAGE_SIZE, PAGE_SIZE);

ASSYM(PIL_TICK, PIL_TICK);

ASSYM(FPRS_DL, FPRS_DL);
ASSYM(FPRS_DU, FPRS_DU);
ASSYM(FPRS_FEF, FPRS_FEF);

ASSYM(LSU_VW, LSU_VW);

ASSYM(TAR_VPN_SHIFT, TAR_VPN_SHIFT);

ASSYM(TLB_DEMAP_NUCLEUS, TLB_DEMAP_NUCLEUS);
ASSYM(TLB_DEMAP_SECONDARY, TLB_DEMAP_SECONDARY);
ASSYM(TLB_DEMAP_CONTEXT, TLB_DEMAP_CONTEXT);
ASSYM(TLB_DEMAP_PAGE, TLB_DEMAP_PAGE);

ASSYM(TLB_DTLB, TLB_DTLB);
ASSYM(TLB_ITLB, TLB_ITLB);

ASSYM(TSB_BUCKET_ADDRESS_BITS, TSB_BUCKET_ADDRESS_BITS);
ASSYM(TSB_BUCKET_SHIFT, TSB_BUCKET_SHIFT);
ASSYM(TSB_KERNEL_MASK, TSB_KERNEL_MASK);

ASSYM(INT_SHIFT, INT_SHIFT);
ASSYM(PTR_SHIFT, PTR_SHIFT);

ASSYM(PAGE_SHIFT, PAGE_SHIFT);
ASSYM(PAGE_MASK, PAGE_MASK);

ASSYM(CPU_INITED, CPU_INITED);
ASSYM(CPU_STARTED, CPU_STARTED);

ASSYM(CSA_MID, offsetof(struct cpu_start_args, csa_mid));
ASSYM(CSA_STATE, offsetof(struct cpu_start_args, csa_state));
ASSYM(CSA_DATA, offsetof(struct cpu_start_args, csa_data));
ASSYM(CSA_VA, offsetof(struct cpu_start_args, csa_va));

ASSYM(KTR_COMPILE, KTR_COMPILE);
ASSYM(KTR_PROC, KTR_PROC);
ASSYM(KTR_TRAP, KTR_TRAP);
ASSYM(KTR_SMP, KTR_SMP);
ASSYM(KTR_SYSC, KTR_SYSC);
ASSYM(KTR_INTR, KTR_INTR);
ASSYM(KTR_CT1, KTR_CT1);
ASSYM(KTR_CT2, KTR_CT2);
ASSYM(KTR_CT3, KTR_CT3);
ASSYM(KTR_CT4, KTR_CT4);

ASSYM(KTR_SIZEOF, sizeof(struct ktr_entry));
ASSYM(KTR_DESC, offsetof(struct ktr_entry, ktr_desc));
ASSYM(KTR_PARM1, offsetof(struct ktr_entry, ktr_parm1));
ASSYM(KTR_PARM2, offsetof(struct ktr_entry, ktr_parm2));
ASSYM(KTR_PARM3, offsetof(struct ktr_entry, ktr_parm3));
ASSYM(KTR_PARM4, offsetof(struct ktr_entry, ktr_parm4));
ASSYM(KTR_PARM5, offsetof(struct ktr_entry, ktr_parm5));
ASSYM(KTR_PARM6, offsetof(struct ktr_entry, ktr_parm6));

ASSYM(TTE_VPN, offsetof(struct tte, tte_vpn));
ASSYM(TTE_DATA, offsetof(struct tte, tte_data));
ASSYM(TTE_SHIFT, TTE_SHIFT);

ASSYM(TD_EXEC, TD_EXEC);
ASSYM(TD_REF, TD_REF);
ASSYM(TD_SW, TD_SW);
ASSYM(TD_L, TD_L);
ASSYM(TD_W, TD_W);

ASSYM(V_INTR, offsetof(struct vmmeter, v_intr));

ASSYM(PC_CURTHREAD, offsetof(struct pcpu, pc_curthread));
ASSYM(PC_CURPCB, offsetof(struct pcpu, pc_curpcb));
ASSYM(PC_CPUID, offsetof(struct pcpu, pc_cpuid));
ASSYM(PC_IQ, offsetof(struct pcpu, pc_iq));
ASSYM(PC_MID, offsetof(struct pcpu, pc_mid));
ASSYM(PC_SIZEOF, sizeof(struct pcpu));

ASSYM(IH_SHIFT, IH_SHIFT);

ASSYM(IQ_MASK, IQ_MASK);
ASSYM(IQ_HEAD, offsetof(struct intr_queue, iq_head));
ASSYM(IQ_TAIL, offsetof(struct intr_queue, iq_tail));

ASSYM(IQE_SHIFT, IQE_SHIFT);
ASSYM(IQE_TAG, offsetof(struct iqe, iqe_tag));
ASSYM(IQE_PRI, offsetof(struct iqe, iqe_pri));
ASSYM(IQE_VEC, offsetof(struct iqe, iqe_vec));
ASSYM(IQE_FUNC, offsetof(struct iqe, iqe_func));
ASSYM(IQE_ARG, offsetof(struct iqe, iqe_arg));

ASSYM(ILA_LEVEL, offsetof(struct ipi_level_args, ila_level));

ASSYM(ITA_TLB, offsetof(struct ipi_tlb_args, ita_tlb));
ASSYM(ITA_CTX, offsetof(struct ipi_tlb_args, ita_ctx));
ASSYM(ITA_START, offsetof(struct ipi_tlb_args, ita_start));
ASSYM(ITA_END, offsetof(struct ipi_tlb_args, ita_end));
ASSYM(ITA_VA, offsetof(struct ipi_tlb_args, ita_va));

ASSYM(IV_SHIFT, IV_SHIFT);
ASSYM(IV_FUNC, offsetof(struct intr_vector, iv_func));
ASSYM(IV_ARG, offsetof(struct intr_vector, iv_arg));
ASSYM(IV_PRI, offsetof(struct intr_vector, iv_pri));

ASSYM(NIV, NIV);

ASSYM(KEF_ASTPENDING, KEF_ASTPENDING);
ASSYM(KEF_NEEDRESCHED, KEF_NEEDRESCHED);

ASSYM(MD_UTRAP, offsetof(struct mdproc, md_utrap));

ASSYM(P_COMM, offsetof(struct proc, p_comm));
ASSYM(P_MD, offsetof(struct proc, p_md));
ASSYM(P_PID, offsetof(struct proc, p_pid));
ASSYM(P_SFLAG, offsetof(struct proc, p_sflag));
ASSYM(P_VMSPACE, offsetof(struct proc, p_vmspace));

ASSYM(RW_SHIFT, RW_SHIFT);

ASSYM(KE_FLAGS, offsetof(struct kse, ke_flags));

ASSYM(TD_FRAME, offsetof(struct thread, td_frame));
ASSYM(TD_KSE, offsetof(struct thread, td_kse));
ASSYM(TD_KSTACK, offsetof(struct thread, td_kstack));
ASSYM(TD_PCB, offsetof(struct thread, td_pcb));
ASSYM(TD_PROC, offsetof(struct thread, td_proc));

ASSYM(PCB_SIZEOF, sizeof(struct pcb));
ASSYM(PCB_FPSTATE, offsetof(struct pcb, pcb_fpstate));
ASSYM(PCB_FP, offsetof(struct pcb, pcb_fp));
ASSYM(PCB_PC, offsetof(struct pcb, pcb_pc));
ASSYM(PCB_Y, offsetof(struct pcb, pcb_pc));
ASSYM(PCB_ONFAULT, offsetof(struct pcb, pcb_onfault));
ASSYM(PCB_NSAVED, offsetof(struct pcb, pcb_nsaved));
ASSYM(PCB_RWSP, offsetof(struct pcb, pcb_rwsp));
ASSYM(PCB_RW, offsetof(struct pcb, pcb_rw));

ASSYM(VM_PMAP, offsetof(struct vmspace, vm_pmap));
ASSYM(PM_ACTIVE, offsetof(struct pmap, pm_active));
ASSYM(PM_CONTEXT, offsetof(struct pmap, pm_context));
ASSYM(PM_TSB, offsetof(struct pmap, pm_tsb));

ASSYM(FP_FB0, offsetof(struct fpstate, fp_fb[0]));
ASSYM(FP_FB1, offsetof(struct fpstate, fp_fb[1]));
ASSYM(FP_FB2, offsetof(struct fpstate, fp_fb[2]));
ASSYM(FP_FB3, offsetof(struct fpstate, fp_fb[3]));

ASSYM(CCFSZ, sizeof(struct frame));
ASSYM(SPOFF, SPOFF);

ASSYM(SF_UC, offsetof(struct sigframe, sf_uc));

ASSYM(_JB_FP, offsetof(struct _jmp_buf, _jb[_JB_FP]));
ASSYM(_JB_PC, offsetof(struct _jmp_buf, _jb[_JB_PC]));
ASSYM(_JB_SP, offsetof(struct _jmp_buf, _jb[_JB_SP]));
ASSYM(_JB_SIGFLAG, offsetof(struct _jmp_buf, _jb[_JB_SIGFLAG]));
ASSYM(_JB_SIGMASK, offsetof(struct _jmp_buf, _jb[_JB_SIGMASK]));

ASSYM(TF_G0, offsetof(struct trapframe, tf_global[0]));
ASSYM(TF_G1, offsetof(struct trapframe, tf_global[1]));
ASSYM(TF_G2, offsetof(struct trapframe, tf_global[2]));
ASSYM(TF_G3, offsetof(struct trapframe, tf_global[3]));
ASSYM(TF_G4, offsetof(struct trapframe, tf_global[4]));
ASSYM(TF_G5, offsetof(struct trapframe, tf_global[5]));
ASSYM(TF_G6, offsetof(struct trapframe, tf_global[6]));
ASSYM(TF_G7, offsetof(struct trapframe, tf_global[7]));
ASSYM(TF_O0, offsetof(struct trapframe, tf_out[0]));
ASSYM(TF_O1, offsetof(struct trapframe, tf_out[1]));
ASSYM(TF_O2, offsetof(struct trapframe, tf_out[2]));
ASSYM(TF_O3, offsetof(struct trapframe, tf_out[3]));
ASSYM(TF_O4, offsetof(struct trapframe, tf_out[4]));
ASSYM(TF_O5, offsetof(struct trapframe, tf_out[5]));
ASSYM(TF_O6, offsetof(struct trapframe, tf_out[6]));
ASSYM(TF_O7, offsetof(struct trapframe, tf_out[7]));
ASSYM(TF_FSR, offsetof(struct trapframe, tf_fsr));
ASSYM(TF_SFAR, offsetof(struct trapframe, tf_sfar));
ASSYM(TF_LEVEL, offsetof(struct trapframe, tf_level));
ASSYM(TF_TAR, offsetof(struct trapframe, tf_tar));
ASSYM(TF_TNPC, offsetof(struct trapframe, tf_tnpc));
ASSYM(TF_TPC, offsetof(struct trapframe, tf_tpc));
ASSYM(TF_TSTATE, offsetof(struct trapframe, tf_tstate));
ASSYM(TF_SFSR, offsetof(struct trapframe, tf_sfsr));
ASSYM(TF_TYPE, offsetof(struct trapframe, tf_type));
ASSYM(TF_Y, offsetof(struct trapframe, tf_y));
ASSYM(TF_FPRS, offsetof(struct trapframe, tf_fprs));
ASSYM(TF_PIL, offsetof(struct trapframe, tf_pil));
ASSYM(TF_WSTATE, offsetof(struct trapframe, tf_wstate));
ASSYM(TF_SIZEOF, sizeof(struct trapframe));

ASSYM(UT_MAX, UT_MAX);
