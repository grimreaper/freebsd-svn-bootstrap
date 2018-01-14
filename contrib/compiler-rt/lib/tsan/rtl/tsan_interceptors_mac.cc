//===-- tsan_interceptors_mac.cc ------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Mac-specific interceptors.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_MAC

#include "interception/interception.h"
#include "tsan_interceptors.h"
#include "tsan_interface.h"
#include "tsan_interface_ann.h"

#include <libkern/OSAtomic.h>

#if defined(__has_include) && __has_include(<xpc/xpc.h>)
#include <xpc/xpc.h>
#endif  // #if defined(__has_include) && __has_include(<xpc/xpc.h>)

typedef long long_t;  // NOLINT

namespace __tsan {

// The non-barrier versions of OSAtomic* functions are semantically mo_relaxed,
// but the two variants (e.g. OSAtomicAdd32 and OSAtomicAdd32Barrier) are
// actually aliases of each other, and we cannot have different interceptors for
// them, because they're actually the same function.  Thus, we have to stay
// conservative and treat the non-barrier versions as mo_acq_rel.
static const morder kMacOrderBarrier = mo_acq_rel;
static const morder kMacOrderNonBarrier = mo_acq_rel;

#define OSATOMIC_INTERCEPTOR(return_t, t, tsan_t, f, tsan_atomic_f, mo) \
  TSAN_INTERCEPTOR(return_t, f, t x, volatile t *ptr) {                 \
    SCOPED_TSAN_INTERCEPTOR(f, x, ptr);                                 \
    return tsan_atomic_f((volatile tsan_t *)ptr, x, mo);                \
  }

#define OSATOMIC_INTERCEPTOR_PLUS_X(return_t, t, tsan_t, f, tsan_atomic_f, mo) \
  TSAN_INTERCEPTOR(return_t, f, t x, volatile t *ptr) {                        \
    SCOPED_TSAN_INTERCEPTOR(f, x, ptr);                                        \
    return tsan_atomic_f((volatile tsan_t *)ptr, x, mo) + x;                   \
  }

#define OSATOMIC_INTERCEPTOR_PLUS_1(return_t, t, tsan_t, f, tsan_atomic_f, mo) \
  TSAN_INTERCEPTOR(return_t, f, volatile t *ptr) {                             \
    SCOPED_TSAN_INTERCEPTOR(f, ptr);                                           \
    return tsan_atomic_f((volatile tsan_t *)ptr, 1, mo) + 1;                   \
  }

#define OSATOMIC_INTERCEPTOR_MINUS_1(return_t, t, tsan_t, f, tsan_atomic_f, \
                                     mo)                                    \
  TSAN_INTERCEPTOR(return_t, f, volatile t *ptr) {                          \
    SCOPED_TSAN_INTERCEPTOR(f, ptr);                                        \
    return tsan_atomic_f((volatile tsan_t *)ptr, 1, mo) - 1;                \
  }

#define OSATOMIC_INTERCEPTORS_ARITHMETIC(f, tsan_atomic_f, m)                  \
  m(int32_t, int32_t, a32, f##32, __tsan_atomic32_##tsan_atomic_f,             \
    kMacOrderNonBarrier)                                                       \
  m(int32_t, int32_t, a32, f##32##Barrier, __tsan_atomic32_##tsan_atomic_f,    \
    kMacOrderBarrier)                                                          \
  m(int64_t, int64_t, a64, f##64, __tsan_atomic64_##tsan_atomic_f,             \
    kMacOrderNonBarrier)                                                       \
  m(int64_t, int64_t, a64, f##64##Barrier, __tsan_atomic64_##tsan_atomic_f,    \
    kMacOrderBarrier)

#define OSATOMIC_INTERCEPTORS_BITWISE(f, tsan_atomic_f, m, m_orig)             \
  m(int32_t, uint32_t, a32, f##32, __tsan_atomic32_##tsan_atomic_f,            \
    kMacOrderNonBarrier)                                                       \
  m(int32_t, uint32_t, a32, f##32##Barrier, __tsan_atomic32_##tsan_atomic_f,   \
    kMacOrderBarrier)                                                          \
  m_orig(int32_t, uint32_t, a32, f##32##Orig, __tsan_atomic32_##tsan_atomic_f, \
    kMacOrderNonBarrier)                                                       \
  m_orig(int32_t, uint32_t, a32, f##32##OrigBarrier,                           \
    __tsan_atomic32_##tsan_atomic_f, kMacOrderBarrier)

OSATOMIC_INTERCEPTORS_ARITHMETIC(OSAtomicAdd, fetch_add,
                                 OSATOMIC_INTERCEPTOR_PLUS_X)
OSATOMIC_INTERCEPTORS_ARITHMETIC(OSAtomicIncrement, fetch_add,
                                 OSATOMIC_INTERCEPTOR_PLUS_1)
OSATOMIC_INTERCEPTORS_ARITHMETIC(OSAtomicDecrement, fetch_sub,
                                 OSATOMIC_INTERCEPTOR_MINUS_1)
OSATOMIC_INTERCEPTORS_BITWISE(OSAtomicOr, fetch_or, OSATOMIC_INTERCEPTOR_PLUS_X,
                              OSATOMIC_INTERCEPTOR)
OSATOMIC_INTERCEPTORS_BITWISE(OSAtomicAnd, fetch_and,
                              OSATOMIC_INTERCEPTOR_PLUS_X, OSATOMIC_INTERCEPTOR)
OSATOMIC_INTERCEPTORS_BITWISE(OSAtomicXor, fetch_xor,
                              OSATOMIC_INTERCEPTOR_PLUS_X, OSATOMIC_INTERCEPTOR)

#define OSATOMIC_INTERCEPTORS_CAS(f, tsan_atomic_f, tsan_t, t)              \
  TSAN_INTERCEPTOR(bool, f, t old_value, t new_value, t volatile *ptr) {    \
    SCOPED_TSAN_INTERCEPTOR(f, old_value, new_value, ptr);                  \
    return tsan_atomic_f##_compare_exchange_strong(                         \
        (volatile tsan_t *)ptr, (tsan_t *)&old_value, (tsan_t)new_value,    \
        kMacOrderNonBarrier, kMacOrderNonBarrier);                          \
  }                                                                         \
                                                                            \
  TSAN_INTERCEPTOR(bool, f##Barrier, t old_value, t new_value,              \
                   t volatile *ptr) {                                       \
    SCOPED_TSAN_INTERCEPTOR(f##Barrier, old_value, new_value, ptr);         \
    return tsan_atomic_f##_compare_exchange_strong(                         \
        (volatile tsan_t *)ptr, (tsan_t *)&old_value, (tsan_t)new_value,    \
        kMacOrderBarrier, kMacOrderNonBarrier);                             \
  }

OSATOMIC_INTERCEPTORS_CAS(OSAtomicCompareAndSwapInt, __tsan_atomic32, a32, int)
OSATOMIC_INTERCEPTORS_CAS(OSAtomicCompareAndSwapLong, __tsan_atomic64, a64,
                          long_t)
OSATOMIC_INTERCEPTORS_CAS(OSAtomicCompareAndSwapPtr, __tsan_atomic64, a64,
                          void *)
OSATOMIC_INTERCEPTORS_CAS(OSAtomicCompareAndSwap32, __tsan_atomic32, a32,
                          int32_t)
OSATOMIC_INTERCEPTORS_CAS(OSAtomicCompareAndSwap64, __tsan_atomic64, a64,
                          int64_t)

#define OSATOMIC_INTERCEPTOR_BITOP(f, op, clear, mo)             \
  TSAN_INTERCEPTOR(bool, f, uint32_t n, volatile void *ptr) {    \
    SCOPED_TSAN_INTERCEPTOR(f, n, ptr);                          \
    volatile char *byte_ptr = ((volatile char *)ptr) + (n >> 3); \
    char bit = 0x80u >> (n & 7);                                 \
    char mask = clear ? ~bit : bit;                              \
    char orig_byte = op((volatile a8 *)byte_ptr, mask, mo);      \
    return orig_byte & bit;                                      \
  }

#define OSATOMIC_INTERCEPTORS_BITOP(f, op, clear)               \
  OSATOMIC_INTERCEPTOR_BITOP(f, op, clear, kMacOrderNonBarrier) \
  OSATOMIC_INTERCEPTOR_BITOP(f##Barrier, op, clear, kMacOrderBarrier)

OSATOMIC_INTERCEPTORS_BITOP(OSAtomicTestAndSet, __tsan_atomic8_fetch_or, false)
OSATOMIC_INTERCEPTORS_BITOP(OSAtomicTestAndClear, __tsan_atomic8_fetch_and,
                            true)

TSAN_INTERCEPTOR(void, OSAtomicEnqueue, OSQueueHead *list, void *item,
                 size_t offset) {
  SCOPED_TSAN_INTERCEPTOR(OSAtomicEnqueue, list, item, offset);
  __tsan_release(item);
  REAL(OSAtomicEnqueue)(list, item, offset);
}

TSAN_INTERCEPTOR(void *, OSAtomicDequeue, OSQueueHead *list, size_t offset) {
  SCOPED_TSAN_INTERCEPTOR(OSAtomicDequeue, list, offset);
  void *item = REAL(OSAtomicDequeue)(list, offset);
  if (item) __tsan_acquire(item);
  return item;
}

// OSAtomicFifoEnqueue and OSAtomicFifoDequeue are only on OS X.
#if !SANITIZER_IOS

TSAN_INTERCEPTOR(void, OSAtomicFifoEnqueue, OSFifoQueueHead *list, void *item,
                 size_t offset) {
  SCOPED_TSAN_INTERCEPTOR(OSAtomicFifoEnqueue, list, item, offset);
  __tsan_release(item);
  REAL(OSAtomicFifoEnqueue)(list, item, offset);
}

TSAN_INTERCEPTOR(void *, OSAtomicFifoDequeue, OSFifoQueueHead *list,
                 size_t offset) {
  SCOPED_TSAN_INTERCEPTOR(OSAtomicFifoDequeue, list, offset);
  void *item = REAL(OSAtomicFifoDequeue)(list, offset);
  if (item) __tsan_acquire(item);
  return item;
}

#endif

TSAN_INTERCEPTOR(void, OSSpinLockLock, volatile OSSpinLock *lock) {
  CHECK(!cur_thread()->is_dead);
  if (!cur_thread()->is_inited) {
    return REAL(OSSpinLockLock)(lock);
  }
  SCOPED_TSAN_INTERCEPTOR(OSSpinLockLock, lock);
  REAL(OSSpinLockLock)(lock);
  Acquire(thr, pc, (uptr)lock);
}

TSAN_INTERCEPTOR(bool, OSSpinLockTry, volatile OSSpinLock *lock) {
  CHECK(!cur_thread()->is_dead);
  if (!cur_thread()->is_inited) {
    return REAL(OSSpinLockTry)(lock);
  }
  SCOPED_TSAN_INTERCEPTOR(OSSpinLockTry, lock);
  bool result = REAL(OSSpinLockTry)(lock);
  if (result)
    Acquire(thr, pc, (uptr)lock);
  return result;
}

TSAN_INTERCEPTOR(void, OSSpinLockUnlock, volatile OSSpinLock *lock) {
  CHECK(!cur_thread()->is_dead);
  if (!cur_thread()->is_inited) {
    return REAL(OSSpinLockUnlock)(lock);
  }
  SCOPED_TSAN_INTERCEPTOR(OSSpinLockUnlock, lock);
  Release(thr, pc, (uptr)lock);
  REAL(OSSpinLockUnlock)(lock);
}

TSAN_INTERCEPTOR(void, os_lock_lock, void *lock) {
  CHECK(!cur_thread()->is_dead);
  if (!cur_thread()->is_inited) {
    return REAL(os_lock_lock)(lock);
  }
  SCOPED_TSAN_INTERCEPTOR(os_lock_lock, lock);
  REAL(os_lock_lock)(lock);
  Acquire(thr, pc, (uptr)lock);
}

TSAN_INTERCEPTOR(bool, os_lock_trylock, void *lock) {
  CHECK(!cur_thread()->is_dead);
  if (!cur_thread()->is_inited) {
    return REAL(os_lock_trylock)(lock);
  }
  SCOPED_TSAN_INTERCEPTOR(os_lock_trylock, lock);
  bool result = REAL(os_lock_trylock)(lock);
  if (result)
    Acquire(thr, pc, (uptr)lock);
  return result;
}

TSAN_INTERCEPTOR(void, os_lock_unlock, void *lock) {
  CHECK(!cur_thread()->is_dead);
  if (!cur_thread()->is_inited) {
    return REAL(os_lock_unlock)(lock);
  }
  SCOPED_TSAN_INTERCEPTOR(os_lock_unlock, lock);
  Release(thr, pc, (uptr)lock);
  REAL(os_lock_unlock)(lock);
}

#if defined(__has_include) && __has_include(<xpc/xpc.h>)

TSAN_INTERCEPTOR(void, xpc_connection_set_event_handler,
                 xpc_connection_t connection, xpc_handler_t handler) {
  SCOPED_TSAN_INTERCEPTOR(xpc_connection_set_event_handler, connection,
                          handler);
  Release(thr, pc, (uptr)connection);
  xpc_handler_t new_handler = ^(xpc_object_t object) {
    {
      SCOPED_INTERCEPTOR_RAW(xpc_connection_set_event_handler);
      Acquire(thr, pc, (uptr)connection);
    }
    handler(object);
  };
  REAL(xpc_connection_set_event_handler)(connection, new_handler);
}

TSAN_INTERCEPTOR(void, xpc_connection_send_barrier, xpc_connection_t connection,
                 dispatch_block_t barrier) {
  SCOPED_TSAN_INTERCEPTOR(xpc_connection_send_barrier, connection, barrier);
  Release(thr, pc, (uptr)connection);
  dispatch_block_t new_barrier = ^() {
    {
      SCOPED_INTERCEPTOR_RAW(xpc_connection_send_barrier);
      Acquire(thr, pc, (uptr)connection);
    }
    barrier();
  };
  REAL(xpc_connection_send_barrier)(connection, new_barrier);
}

TSAN_INTERCEPTOR(void, xpc_connection_send_message_with_reply,
                 xpc_connection_t connection, xpc_object_t message,
                 dispatch_queue_t replyq, xpc_handler_t handler) {
  SCOPED_TSAN_INTERCEPTOR(xpc_connection_send_message_with_reply, connection,
                          message, replyq, handler);
  Release(thr, pc, (uptr)connection);
  xpc_handler_t new_handler = ^(xpc_object_t object) {
    {
      SCOPED_INTERCEPTOR_RAW(xpc_connection_send_message_with_reply);
      Acquire(thr, pc, (uptr)connection);
    }
    handler(object);
  };
  REAL(xpc_connection_send_message_with_reply)
  (connection, message, replyq, new_handler);
}

TSAN_INTERCEPTOR(void, xpc_connection_cancel, xpc_connection_t connection) {
  SCOPED_TSAN_INTERCEPTOR(xpc_connection_cancel, connection);
  Release(thr, pc, (uptr)connection);
  REAL(xpc_connection_cancel)(connection);
}

#endif  // #if defined(__has_include) && __has_include(<xpc/xpc.h>)

// On macOS, libc++ is always linked dynamically, so intercepting works the
// usual way.
#define STDCXX_INTERCEPTOR TSAN_INTERCEPTOR

namespace {
struct fake_shared_weak_count {
  volatile a64 shared_owners;
  volatile a64 shared_weak_owners;
  virtual void _unused_0x0() = 0;
  virtual void _unused_0x8() = 0;
  virtual void on_zero_shared() = 0;
  virtual void _unused_0x18() = 0;
  virtual void on_zero_shared_weak() = 0;
};
}  // namespace

// The following code adds libc++ interceptors for:
//     void __shared_weak_count::__release_shared() _NOEXCEPT;
//     bool __shared_count::__release_shared() _NOEXCEPT;
// Shared and weak pointers in C++ maintain reference counts via atomics in
// libc++.dylib, which are TSan-invisible, and this leads to false positives in
// destructor code. These interceptors re-implements the whole functions so that
// the mo_acq_rel semantics of the atomic decrement are visible.
//
// Unfortunately, the interceptors cannot simply Acquire/Release some sync
// object and call the original function, because it would have a race between
// the sync and the destruction of the object.  Calling both under a lock will
// not work because the destructor can invoke this interceptor again (and even
// in a different thread, so recursive locks don't help).

STDCXX_INTERCEPTOR(void, _ZNSt3__119__shared_weak_count16__release_sharedEv,
                   fake_shared_weak_count *o) {
  if (!flags()->shared_ptr_interceptor)
    return REAL(_ZNSt3__119__shared_weak_count16__release_sharedEv)(o);

  SCOPED_TSAN_INTERCEPTOR(_ZNSt3__119__shared_weak_count16__release_sharedEv,
                          o);
  if (__tsan_atomic64_fetch_add(&o->shared_owners, -1, mo_release) == 0) {
    Acquire(thr, pc, (uptr)&o->shared_owners);
    o->on_zero_shared();
    if (__tsan_atomic64_fetch_add(&o->shared_weak_owners, -1, mo_release) ==
        0) {
      Acquire(thr, pc, (uptr)&o->shared_weak_owners);
      o->on_zero_shared_weak();
    }
  }
}

STDCXX_INTERCEPTOR(bool, _ZNSt3__114__shared_count16__release_sharedEv,
                   fake_shared_weak_count *o) {
  if (!flags()->shared_ptr_interceptor)
    return REAL(_ZNSt3__114__shared_count16__release_sharedEv)(o);

  SCOPED_TSAN_INTERCEPTOR(_ZNSt3__114__shared_count16__release_sharedEv, o);
  if (__tsan_atomic64_fetch_add(&o->shared_owners, -1, mo_release) == 0) {
    Acquire(thr, pc, (uptr)&o->shared_owners);
    o->on_zero_shared();
    return true;
  }
  return false;
}

namespace {
struct call_once_callback_args {
  void (*orig_func)(void *arg);
  void *orig_arg;
  void *flag;
};

void call_once_callback_wrapper(void *arg) {
  call_once_callback_args *new_args = (call_once_callback_args *)arg;
  new_args->orig_func(new_args->orig_arg);
  __tsan_release(new_args->flag);
}
}  // namespace

// This adds a libc++ interceptor for:
//     void __call_once(volatile unsigned long&, void*, void(*)(void*));
// C++11 call_once is implemented via an internal function __call_once which is
// inside libc++.dylib, and the atomic release store inside it is thus
// TSan-invisible. To avoid false positives, this interceptor wraps the callback
// function and performs an explicit Release after the user code has run.
STDCXX_INTERCEPTOR(void, _ZNSt3__111__call_onceERVmPvPFvS2_E, void *flag,
                   void *arg, void (*func)(void *arg)) {
  call_once_callback_args new_args = {func, arg, flag};
  REAL(_ZNSt3__111__call_onceERVmPvPFvS2_E)(flag, &new_args,
                                            call_once_callback_wrapper);
}

}  // namespace __tsan

#endif  // SANITIZER_MAC
