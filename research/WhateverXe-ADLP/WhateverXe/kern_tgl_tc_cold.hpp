// TGL TC-cold protocol core. Kernel wiring is deliberately separate:
// all PCODE users must share the transport lock, and each scope must hold a
// lease for its complete hardware-use lifetime before this is enabled live.
// The adapter must adopt EFI/native active TC outputs as Scanout references
// before any release can reach zero. A zero software count alone cannot prove
// that inherited hardware is idle.
#ifndef kern_tgl_tc_cold_hpp
#define kern_tgl_tc_cold_hpp
#include <stdint.h>

struct TGLPcode {
 enum class Status { Success, Busy, Timeout, FirmwareError, Unavailable, ExitFailed, Invalid };
 struct Result {
  Status status {Status::Invalid};
  uint32_t mailbox {}, data {}, data1 {}, attempts {};
  bool submitted {};
  bool ok() const { return status == Status::Success; }
 };
 // IO contract: nonblocking pcodeTryLock/Unlock shared with every native
 // PCODE reader/writer; read/write/nowUs/delayUs/sleepMs. Registers are outside
 // forcewake (Linux v6.12 intel_pcode.c), but the MMIO mapping must be live.
 template<class IO> static Result transfer(IO &io, uint32_t command, uint32_t low, uint32_t high) {
  Result r;
  if (command & 0xFF000000U) return r;
  if (!io.pcodeTryLock()) { r.status = Status::Busy; return r; }
  struct Unlock { IO &io; ~Unlock() { io.pcodeUnlock(); } } unlock {io};
  r.mailbox = io.read(0x138124);
  if (r.mailbox == 0xFFFFFFFFU) { r.status = Status::Unavailable; return r; }
  if (r.mailbox & 0x80000000U) { r.status = Status::Busy; return r; }
  io.write(0x138128, low);
  io.write(0x13812C, high);
  io.write(0x138124, command | 0x80000000U);
  r.submitted = true;
  const auto start = io.nowUs();
  for (;;) {
   r.mailbox = io.read(0x138124);
   if (r.mailbox == 0xFFFFFFFFU) { r.status = Status::Unavailable; return r; }
   if (!(r.mailbox & 0x80000000U)) break;
   const auto elapsed = io.nowUs() - start;
   // Match snb_pcode_read's 500us fast + 20ms sleepable wait, not the
   // stale 500us-only comment at the TC-cold call site.
   if (elapsed >= 20500) { r.status = Status::Timeout; return r; }
   if (elapsed < 500) io.delayUs(10);
   else io.sleepMs(1);
  }
  // Preserve firmware status; an unknown nonzero code is not success.
  if (r.mailbox & 0xFF) { r.status = Status::FirmwareError; return r; }
  r.data = io.read(0x138128);
  r.data1 = io.read(0x13812C);
  r.status = Status::Success;
  return r;
 }
 template<class IO> static Result tcCold(IO &io, bool block) {
  Result r;
  bool everSubmitted = false;
  for (uint32_t attempt = 1; attempt <= 3; ++attempt) {
   r = transfer(io, 0x26, block ? 0 : 1, 0);
   everSubmitted |= r.submitted; r.submitted = everSubmitted;
   r.attempts = attempt;
   if (r.ok() && block && (r.data & 1)) r.status = Status::ExitFailed;
   if (r.ok() || r.status == Status::Unavailable || r.status == Status::Invalid) break;
   if (attempt < 3) io.sleepMs(1); // outside the mailbox lock
  }
  return r;
 }
};

struct TGLTCCold {
 enum class Use : uint32_t { Probe, AUX, Training, Scanout, Mode, Count };
 enum class Phase { Unknown, Blocked, Unblocked };
 enum class Status { Success, Invalid, WrongOwner, Unbalanced, Overflow, InUse, TransportFailed };
 struct Result {
  Status status {Status::Invalid};
  TGLPcode::Result transport {};
  bool requested {};
  bool ok() const { return status == Status::Success; }
 };
 uintptr_t owner {};
 static constexpr uint32_t UseCount=static_cast<uint32_t>(Use::Count);
 uint32_t refs[9][UseCount] {}, total {};
 Phase phase {Phase::Unknown};
 // Controller-wide early-start token. Included in total, never attributed
 // to an arbitrary DDI or borrowed as a per-port hardware-use reference.
 bool bootstrapHeld {};
 enum class BootDisposition { Unresolved, Quiescent, Referenced };
 struct BootInventory { BootDisposition ports[6] {}; }; // DDI3..8, all required

 // Call before native initialization can release inherited power state.
 // On a failed block the token/identity remain: EFI may still be scanning
 // out, so a normal temporary user's release must not reach last-unblock.
 // Caller retains controller/MMIO even on failure. Retrying adds no token.
 template<class IO> Result beginBootstrap(IO &io,uintptr_t who) {
  Result r;if(!who)return r;
  io.coldLock();struct Unlock {IO &io;~Unlock(){io.coldUnlock();}} unlock {io};
  if(owner && owner!=who){r.status=Status::WrongOwner;return r;}
  if(!bootstrapHeld){
   if(total){r.status=Status::InUse;return r;}
   owner=who;bootstrapHeld=true;total=1;
  }
  if(phase!=Phase::Blocked){
   r.requested=true;r.transport=TGLPcode::tcCold(io,true);
   if(!r.transport.ok()){phase=Phase::Unknown;r.status=Status::TransportFailed;return r;}
   phase=Phase::Blocked;
  }
  r.status=Status::Success;return r;
 }
 // Host holds the lifecycle exclusion from inventory acquisition through
 // this call. Quiescent proves no untracked inherited PHY/scanout use;
 // software mode0, HPD-low, a disabled power domain, or the single native
 // boot-pipe choice cannot establish it. Unreadable/unsupported => Unresolved.
 // Referenced requires a real long-lived token for THAT port. AUX/Probe or
 // another port's token cannot cover inherited scanout. No new token here.
 template<class IO> Result finishBootstrap(IO &io,uintptr_t who,const BootInventory &inventory) {
  Result r;if(!who)return r;
  io.coldLock();struct Unlock {IO &io;~Unlock(){io.coldUnlock();}} unlock {io};
  if(owner!=who){r.status=Status::WrongOwner;return r;}
  if(!bootstrapHeld || !total){r.status=Status::Unbalanced;return r;}
  for(uint32_t i=0;i<6;++i){
   const auto d=inventory.ports[i];
   if(d==BootDisposition::Quiescent)continue;
   if(d!=BootDisposition::Referenced ||
      (!refs[i+3][static_cast<uint32_t>(Use::Mode)] &&
       !refs[i+3][static_cast<uint32_t>(Use::Scanout)])) {
    r.status=Status::InUse;return r;
   }
  }
  if(phase!=Phase::Blocked){
   r.requested=true;r.transport=TGLPcode::tcCold(io,true);
   if(!r.transport.ok()){phase=Phase::Unknown;r.status=Status::TransportFailed;return r;}
   phase=Phase::Blocked;
  }
  if(total==1){
   r.requested=true;r.transport=TGLPcode::tcCold(io,false);
   if(!r.transport.ok()){phase=Phase::Unknown;r.status=Status::TransportFailed;return r;}
   phase=Phase::Unblocked;
  }
  --total;bootstrapHeld=false;if(!total)owner=0;
  r.status=Status::Success;return r;
 }

 // Host lifecycle exclusion must keep bootstrap in place after this check.
 // This only verifies the global bootstrap token; FIA users still acquire
 // their own per-port Mode/Probe reference before touching the hardware.
 template<class IO> bool bootstrapReady(IO &io,uintptr_t who) {
  io.coldLock();struct Unlock {IO &io;~Unlock(){io.coldUnlock();}} unlock {io};
  return who && owner==who && bootstrapHeld && phase==Phase::Blocked;
 }

 // IO has a separate sleepable coldLock/Unlock. Lock order is cold -> pcode;
 // native PCODE wrappers take only pcode and must never acquire cold inside it.
 // A stable owner token and balanced scope calls are required from the adapter.
 template<class IO> Result acquire(IO &io, uintptr_t who, uint32_t ddi, Use use) {
  Result r;
  const auto kind = static_cast<uint32_t>(use);
  if (!who || ddi < 3 || ddi > 8 || kind >= UseCount) return r;
  io.coldLock();
  struct Unlock { IO &io; ~Unlock() { io.coldUnlock(); } } unlock {io};
  if (owner && owner != who) { r.status = Status::WrongOwner; return r; }
  auto &count = refs[ddi][kind];
  if (total == UINT32_MAX || count == UINT32_MAX) { r.status = Status::Overflow; return r; }
  // An earlier timeout may have completed late. Unknown state must be
  // re-established even if a failed release retained a logical reference.
  if (phase != Phase::Blocked) {
   if (!owner) owner = who;
   r.requested = true; r.transport = TGLPcode::tcCold(io, true);
   if (!r.transport.ok()) { phase = Phase::Unknown; r.status = Status::TransportFailed; return r; }
   phase = Phase::Blocked;
  }
  owner = who; ++count; ++total; r.status = Status::Success; return r;
 }
 template<class IO> Result release(IO &io, uintptr_t who, uint32_t ddi, Use use) {
  Result r;
  const auto kind = static_cast<uint32_t>(use);
  if (!who || ddi < 3 || ddi > 8 || kind >= UseCount) return r;
  io.coldLock();
  struct Unlock { IO &io; ~Unlock() { io.coldUnlock(); } } unlock {io};
  if (owner != who) { r.status = Status::WrongOwner; return r; }
  auto &count = refs[ddi][kind];
  if (!count || !total) { r.status = Status::Unbalanced; return r; }
  if (total == 1) {
   r.requested = true; r.transport = TGLPcode::tcCold(io, false);
   if (!r.transport.ok()) { phase = Phase::Unknown; r.status = Status::TransportFailed; return r; }
   phase = Phase::Unblocked;
  }
  --count; --total;
  if (!total) owner = 0;
  r.status = Status::Success; return r;
 }
 // The host also excludes new users while it drains a port. Check our
 // own live accounting rather than accepting a quiescence snapshot alone.
 template<class IO> bool exclusivelyHeld(IO &io,uintptr_t who,uint32_t ddi,Use use,uint32_t count) {
  const auto kind=static_cast<uint32_t>(use);
  if(!who || ddi<3 || ddi>8 || kind>=UseCount)return false;
  io.coldLock();struct Unlock {IO &io;~Unlock(){io.coldUnlock();}} unlock {io};
  if(owner && owner!=who)return false;
  for(uint32_t k=0;k<UseCount;++k)if(refs[ddi][k]!=(k==kind?count:0))return false;
  return true;
 }
 // Re-establish a retained reference after an uncertain final release,
 // without manufacturing a second token or borrowing another port's use.
 template<class IO> Result refresh(IO &io,uintptr_t who,uint32_t ddi,Use use) {
  Result r;const auto kind=static_cast<uint32_t>(use);
  if(!who || ddi<3 || ddi>8 || kind>=UseCount)return r;
  io.coldLock();struct Unlock {IO &io;~Unlock(){io.coldUnlock();}} unlock {io};
  if(owner!=who){r.status=Status::WrongOwner;return r;}
  if(!refs[ddi][kind]){r.status=Status::Unbalanced;return r;}
  if(phase!=Phase::Blocked){
   r.requested=true;r.transport=TGLPcode::tcCold(io,true);
   if(!r.transport.ok()){phase=Phase::Unknown;r.status=Status::TransportFailed;return r;}
   phase=Phase::Blocked;
  }
  r.status=Status::Success;return r;
 }
 // Upper AUX attachment: cover native DC/power setup, retries and cleanup,
 // not just runAUXCommand. Operation must return normally (kernel contract).
 // The adapter must inspect executed/acquire before using operationResult.
 // A failed final release leaves a durable token in this object; the adapter
 // owns its retry/drain and may not discard the owner or MMIO mapping.
 struct AUXCall {
  bool executed {};
  uint32_t operationResult {0xFFFFFFFFU};
  Result acquire {}, release {};
  bool retainedReference() const { return executed && !release.ok(); }
 };
 template<class IO, class Operation> AUXCall invokeAUX(IO &io, uintptr_t who,
                                                     uint32_t ddi, Operation operation) {
  AUXCall r;
  r.acquire = acquire(io, who, ddi, Use::AUX);
  if (!r.acquire.ok()) return r;
  r.executed = true;
  r.operationResult = operation();
  r.release = release(io, who, ddi, Use::AUX);
  // Preserve native transfer success/error independently of cleanup status.
  // Turning a completed I2C write into a transfer error could replay it.
  return r;
 }
 // A failed initial acquire may have issued a block without returning a
 // token. Only that owner may reconcile it, and only with no live uses.
 // The adapter must first establish hardware idle (including inherited uses).
 template<class IO> Result reconcileIdle(IO &io, uintptr_t who) {
  Result r;
  if (!who) return r;
  io.coldLock();
  struct Unlock { IO &io; ~Unlock() { io.coldUnlock(); } } unlock {io};
  if (owner != who) { r.status = Status::WrongOwner; return r; }
  if (total) { r.status = Status::InUse; return r; }
  r.requested = true; r.transport = TGLPcode::tcCold(io, false);
  if (!r.transport.ok()) { phase = Phase::Unknown; r.status = Status::TransportFailed; return r; }
  phase = Phase::Unblocked; owner = 0; r.status = Status::Success; return r;
 }
 // No force-clear/implicit rollback: failed last release retains its token.
 // Controller teardown must explicitly drain/reconcile it while MMIO is valid.
};
#endif
