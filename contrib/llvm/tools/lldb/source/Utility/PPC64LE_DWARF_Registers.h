//===-- PPC64LE_DWARF_Registers.h -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef utility_PPC64LE_DWARF_Registers_h_
#define utility_PPC64LE_DWARF_Registers_h_

#include "lldb/lldb-private.h"

namespace ppc64le_dwarf {

enum {
  dwarf_r0_ppc64le = 0,
  dwarf_r1_ppc64le,
  dwarf_r2_ppc64le,
  dwarf_r3_ppc64le,
  dwarf_r4_ppc64le,
  dwarf_r5_ppc64le,
  dwarf_r6_ppc64le,
  dwarf_r7_ppc64le,
  dwarf_r8_ppc64le,
  dwarf_r9_ppc64le,
  dwarf_r10_ppc64le,
  dwarf_r11_ppc64le,
  dwarf_r12_ppc64le,
  dwarf_r13_ppc64le,
  dwarf_r14_ppc64le,
  dwarf_r15_ppc64le,
  dwarf_r16_ppc64le,
  dwarf_r17_ppc64le,
  dwarf_r18_ppc64le,
  dwarf_r19_ppc64le,
  dwarf_r20_ppc64le,
  dwarf_r21_ppc64le,
  dwarf_r22_ppc64le,
  dwarf_r23_ppc64le,
  dwarf_r24_ppc64le,
  dwarf_r25_ppc64le,
  dwarf_r26_ppc64le,
  dwarf_r27_ppc64le,
  dwarf_r28_ppc64le,
  dwarf_r29_ppc64le,
  dwarf_r30_ppc64le,
  dwarf_r31_ppc64le,
  dwarf_f0_ppc64le,
  dwarf_f1_ppc64le,
  dwarf_f2_ppc64le,
  dwarf_f3_ppc64le,
  dwarf_f4_ppc64le,
  dwarf_f5_ppc64le,
  dwarf_f6_ppc64le,
  dwarf_f7_ppc64le,
  dwarf_f8_ppc64le,
  dwarf_f9_ppc64le,
  dwarf_f10_ppc64le,
  dwarf_f11_ppc64le,
  dwarf_f12_ppc64le,
  dwarf_f13_ppc64le,
  dwarf_f14_ppc64le,
  dwarf_f15_ppc64le,
  dwarf_f16_ppc64le,
  dwarf_f17_ppc64le,
  dwarf_f18_ppc64le,
  dwarf_f19_ppc64le,
  dwarf_f20_ppc64le,
  dwarf_f21_ppc64le,
  dwarf_f22_ppc64le,
  dwarf_f23_ppc64le,
  dwarf_f24_ppc64le,
  dwarf_f25_ppc64le,
  dwarf_f26_ppc64le,
  dwarf_f27_ppc64le,
  dwarf_f28_ppc64le,
  dwarf_f29_ppc64le,
  dwarf_f30_ppc64le,
  dwarf_f31_ppc64le,
  dwarf_lr_ppc64le = 65,
  dwarf_ctr_ppc64le,
  dwarf_cr_ppc64le = 68,
  dwarf_xer_ppc64le = 76,
  dwarf_vr0_ppc64le,
  dwarf_vr1_ppc64le,
  dwarf_vr2_ppc64le,
  dwarf_vr3_ppc64le,
  dwarf_vr4_ppc64le,
  dwarf_vr5_ppc64le,
  dwarf_vr6_ppc64le,
  dwarf_vr7_ppc64le,
  dwarf_vr8_ppc64le,
  dwarf_vr9_ppc64le,
  dwarf_vr10_ppc64le,
  dwarf_vr11_ppc64le,
  dwarf_vr12_ppc64le,
  dwarf_vr13_ppc64le,
  dwarf_vr14_ppc64le,
  dwarf_vr15_ppc64le,
  dwarf_vr16_ppc64le,
  dwarf_vr17_ppc64le,
  dwarf_vr18_ppc64le,
  dwarf_vr19_ppc64le,
  dwarf_vr20_ppc64le,
  dwarf_vr21_ppc64le,
  dwarf_vr22_ppc64le,
  dwarf_vr23_ppc64le,
  dwarf_vr24_ppc64le,
  dwarf_vr25_ppc64le,
  dwarf_vr26_ppc64le,
  dwarf_vr27_ppc64le,
  dwarf_vr28_ppc64le,
  dwarf_vr29_ppc64le,
  dwarf_vr30_ppc64le,
  dwarf_vr31_ppc64le,
  dwarf_vscr_ppc64le = 110,
  dwarf_vrsave_ppc64le = 117,
  dwarf_pc_ppc64le,
  dwarf_softe_ppc64le,
  dwarf_trap_ppc64le,
  dwarf_origr3_ppc64le,
  dwarf_fpscr_ppc64le,
  dwarf_msr_ppc64le,
  dwarf_vs0_ppc64le,
  dwarf_vs1_ppc64le,
  dwarf_vs2_ppc64le,
  dwarf_vs3_ppc64le,
  dwarf_vs4_ppc64le,
  dwarf_vs5_ppc64le,
  dwarf_vs6_ppc64le,
  dwarf_vs7_ppc64le,
  dwarf_vs8_ppc64le,
  dwarf_vs9_ppc64le,
  dwarf_vs10_ppc64le,
  dwarf_vs11_ppc64le,
  dwarf_vs12_ppc64le,
  dwarf_vs13_ppc64le,
  dwarf_vs14_ppc64le,
  dwarf_vs15_ppc64le,
  dwarf_vs16_ppc64le,
  dwarf_vs17_ppc64le,
  dwarf_vs18_ppc64le,
  dwarf_vs19_ppc64le,
  dwarf_vs20_ppc64le,
  dwarf_vs21_ppc64le,
  dwarf_vs22_ppc64le,
  dwarf_vs23_ppc64le,
  dwarf_vs24_ppc64le,
  dwarf_vs25_ppc64le,
  dwarf_vs26_ppc64le,
  dwarf_vs27_ppc64le,
  dwarf_vs28_ppc64le,
  dwarf_vs29_ppc64le,
  dwarf_vs30_ppc64le,
  dwarf_vs31_ppc64le,
  dwarf_vs32_ppc64le,
  dwarf_vs33_ppc64le,
  dwarf_vs34_ppc64le,
  dwarf_vs35_ppc64le,
  dwarf_vs36_ppc64le,
  dwarf_vs37_ppc64le,
  dwarf_vs38_ppc64le,
  dwarf_vs39_ppc64le,
  dwarf_vs40_ppc64le,
  dwarf_vs41_ppc64le,
  dwarf_vs42_ppc64le,
  dwarf_vs43_ppc64le,
  dwarf_vs44_ppc64le,
  dwarf_vs45_ppc64le,
  dwarf_vs46_ppc64le,
  dwarf_vs47_ppc64le,
  dwarf_vs48_ppc64le,
  dwarf_vs49_ppc64le,
  dwarf_vs50_ppc64le,
  dwarf_vs51_ppc64le,
  dwarf_vs52_ppc64le,
  dwarf_vs53_ppc64le,
  dwarf_vs54_ppc64le,
  dwarf_vs55_ppc64le,
  dwarf_vs56_ppc64le,
  dwarf_vs57_ppc64le,
  dwarf_vs58_ppc64le,
  dwarf_vs59_ppc64le,
  dwarf_vs60_ppc64le,
  dwarf_vs61_ppc64le,
  dwarf_vs62_ppc64le,
  dwarf_vs63_ppc64le,
};

} // namespace ppc64le_dwarf

#endif // utility_PPC64LE_DWARF_Registers_h_
