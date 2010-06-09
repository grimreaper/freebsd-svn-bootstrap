//=====-- X86MCAsmInfo.h - X86 asm properties -----------------*- C++ -*--====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the X86MCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef X86TARGETASMINFO_H
#define X86TARGETASMINFO_H

#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAsmInfoCOFF.h"
#include "llvm/MC/MCAsmInfoDarwin.h"

namespace llvm {
  class Triple;

  struct X86MCAsmInfoDarwin : public MCAsmInfoDarwin {
    explicit X86MCAsmInfoDarwin(const Triple &Triple);
  };

  struct X86ELFMCAsmInfo : public MCAsmInfo {
    explicit X86ELFMCAsmInfo(const Triple &Triple);
    virtual const MCSection *getNonexecutableStackSection(MCContext &Ctx) const;
  };

  struct X86MCAsmInfoCOFF : public MCAsmInfoCOFF {
    explicit X86MCAsmInfoCOFF(const Triple &Triple);
  };
} // namespace llvm

#endif
