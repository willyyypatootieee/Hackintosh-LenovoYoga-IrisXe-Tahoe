// Serialization shared by native PCODE transactions and TC-cold requests.
// This is an ownership gate, not a spinlock held with interrupts disabled.
// No contender sleeps while owning the gate. The owner may sleep only when
// its transaction's existing calling contract permits it.
#ifndef kern_tgl_pcode_gate_hpp
#define kern_tgl_pcode_gate_hpp
#include <stdint.h>

struct TGLPcodeGate {
 enum class Status { Acquired, Busy, Reentrant, Invalid };
 enum class Wait { None, Sleep1ms, Delay1us };
 uintptr_t owner {};
 Status tryAcquire(uintptr_t who) {
  if (!who) return Status::Invalid;
  uintptr_t expected=0;
  if (__atomic_compare_exchange_n(&owner,&expected,who,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))
   return Status::Acquired;
  return expected==who ? Status::Reentrant : Status::Busy;
 }
 bool release(uintptr_t who) {
  if (!who) return false;
  return __atomic_compare_exchange_n(&owner,&who,uintptr_t(0),false,__ATOMIC_RELEASE,__ATOMIC_RELAXED);
 }
 template<class IO> Status acquire(IO &io,uintptr_t who,Wait mode) {
  // Match the native initial-busy policy: at most 500 waits. A recursive
  // request cannot wait for itself and must never start another transaction.
  for (unsigned count=0;;++count) {
   const auto result=tryAcquire(who);
   if (result!=Status::Busy || mode==Wait::None || count==500) return result;
   if (mode==Wait::Delay1us) io.delayUs(1);
   else io.sleepMs(1);
  }
 }
 // Each caller must retain ownership through completion and any DATA reads.
 // Acquire failure means no MMIO and no release; never fall back to an
 // unprotected transaction. Native wrappers preserve their existing timeout
 // return convention, while TC-cold reports a distinct Busy result.
 template<class IO> struct Lease {
  TGLPcodeGate &gate;uintptr_t who;Status status;
  Lease(TGLPcodeGate &g,IO &io,uintptr_t token,Wait mode)
   :gate(g),who(token),status(g.acquire(io,token,mode)) {}
  ~Lease() { if (status==Status::Acquired) gate.release(who); }
  bool acquired() const { return status==Status::Acquired; }
  Lease(const Lease &)=delete;
  Lease &operator=(const Lease &)=delete;
 };
};
#endif
