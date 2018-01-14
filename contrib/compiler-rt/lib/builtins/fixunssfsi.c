/* ===-- fixunssfsi.c - Implement __fixunssfsi -----------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __fixunssfsi for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#define SINGLE_PRECISION
#include "fp_lib.h"
typedef su_int fixuint_t;
#include "fp_fixuint_impl.inc"

COMPILER_RT_ABI su_int
__fixunssfsi(fp_t a) {
    return __fixuint(a);
}

#if defined(__ARM_EABI__)
#if defined(COMPILER_RT_ARMHF_TARGET)
AEABI_RTABI su_int __aeabi_f2uiz(fp_t a) {
  return __fixunssfsi(a);
}
#else
AEABI_RTABI su_int __aeabi_f2uiz(fp_t a) COMPILER_RT_ALIAS(__fixunssfsi);
#endif
#endif
