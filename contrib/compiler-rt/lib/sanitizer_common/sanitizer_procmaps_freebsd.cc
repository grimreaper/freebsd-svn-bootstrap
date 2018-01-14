//===-- sanitizer_procmaps_freebsd.cc -------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Information about the process mappings (FreeBSD and NetBSD-specific parts).
//===----------------------------------------------------------------------===//

#include "sanitizer_platform.h"
#if SANITIZER_FREEBSD || SANITIZER_NETBSD
#include "sanitizer_common.h"
#if SANITIZER_FREEBSD
#include "sanitizer_freebsd.h"
#endif
#include "sanitizer_procmaps.h"

#include <unistd.h>
#include <sys/sysctl.h>
#if SANITIZER_FREEBSD
#include <sys/user.h>
#endif

// Fix 'kinfo_vmentry' definition on FreeBSD prior v9.2 in 32-bit mode.
#if SANITIZER_FREEBSD && (SANITIZER_WORDSIZE == 32)
# include <osreldate.h>
# if __FreeBSD_version <= 902001  // v9.2
#  define kinfo_vmentry xkinfo_vmentry
# endif
#endif

namespace __sanitizer {

void ReadProcMaps(ProcSelfMapsBuff *proc_maps) {
  const int Mib[] = {
#if SANITIZER_FREEBSD
    CTL_KERN,
    KERN_PROC,
    KERN_PROC_VMMAP,
    getpid()
#else
    CTL_VM,
    VM_PROC,
    VM_PROC_MAP,
    getpid(),
    sizeof(struct kinfo_vmentry)
#endif
  };

  size_t Size = 0;
  int Err = sysctl(Mib, ARRAY_SIZE(Mib), NULL, &Size, NULL, 0);
  CHECK_EQ(Err, 0);
  CHECK_GT(Size, 0);

  size_t MmapedSize = Size * 4 / 3;
  void *VmMap = MmapOrDie(MmapedSize, "ReadProcMaps()");
  Size = MmapedSize;
  Err = sysctl(Mib, ARRAY_SIZE(Mib), VmMap, &Size, NULL, 0);
  CHECK_EQ(Err, 0);

  proc_maps->data = (char*)VmMap;
  proc_maps->mmaped_size = MmapedSize;
  proc_maps->len = Size;
}

bool MemoryMappingLayout::Next(MemoryMappedSegment *segment) {
  char *last = data_.proc_self_maps.data + data_.proc_self_maps.len;
  if (data_.current >= last) return false;
  struct kinfo_vmentry *VmEntry = (struct kinfo_vmentry *)data_.current;

  segment->start = (uptr)VmEntry->kve_start;
  segment->end = (uptr)VmEntry->kve_end;
  segment->offset = (uptr)VmEntry->kve_offset;

  segment->protection = 0;
  if ((VmEntry->kve_protection & KVME_PROT_READ) != 0)
    segment->protection |= kProtectionRead;
  if ((VmEntry->kve_protection & KVME_PROT_WRITE) != 0)
    segment->protection |= kProtectionWrite;
  if ((VmEntry->kve_protection & KVME_PROT_EXEC) != 0)
    segment->protection |= kProtectionExecute;

  if (segment->filename != NULL && segment->filename_size > 0) {
    internal_snprintf(segment->filename,
                      Min(segment->filename_size, (uptr)PATH_MAX), "%s",
                      VmEntry->kve_path);
  }

#if SANITIZER_FREEBSD
  data_.current += VmEntry->kve_structsize;
#else
  data_.current += sizeof(*VmEntry);
#endif

  return true;
}

}  // namespace __sanitizer

#endif  // SANITIZER_FREEBSD || SANITIZER_NETBSD
