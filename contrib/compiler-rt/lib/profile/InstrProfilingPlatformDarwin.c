/*===- InstrProfilingPlatformDarwin.c - Profile data on Darwin ------------===*\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|*
\*===----------------------------------------------------------------------===*/

#include "InstrProfiling.h"

#if defined(__APPLE__)
/* Use linker magic to find the bounds of the Data section. */
__attribute__((visibility("hidden")))
extern __llvm_profile_data DataStart __asm("section$start$__DATA$__llvm_prf_data");
__attribute__((visibility("hidden")))
extern __llvm_profile_data DataEnd   __asm("section$end$__DATA$__llvm_prf_data");
__attribute__((visibility("hidden")))
extern char NamesStart __asm("section$start$__DATA$__llvm_prf_names");
__attribute__((visibility("hidden")))
extern char NamesEnd   __asm("section$end$__DATA$__llvm_prf_names");
__attribute__((visibility("hidden")))
extern uint64_t CountersStart __asm("section$start$__DATA$__llvm_prf_cnts");
__attribute__((visibility("hidden")))
extern uint64_t CountersEnd   __asm("section$end$__DATA$__llvm_prf_cnts");

__attribute__((visibility("hidden")))
const __llvm_profile_data *__llvm_profile_begin_data(void) {
  return &DataStart;
}
__attribute__((visibility("hidden")))
const __llvm_profile_data *__llvm_profile_end_data(void) {
  return &DataEnd;
}
__attribute__((visibility("hidden")))
const char *__llvm_profile_begin_names(void) { return &NamesStart; }
__attribute__((visibility("hidden")))
const char *__llvm_profile_end_names(void) { return &NamesEnd; }
__attribute__((visibility("hidden")))
uint64_t *__llvm_profile_begin_counters(void) { return &CountersStart; }
__attribute__((visibility("hidden")))
uint64_t *__llvm_profile_end_counters(void) { return &CountersEnd; }
#endif
