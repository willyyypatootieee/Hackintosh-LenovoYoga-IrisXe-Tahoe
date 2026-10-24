// TGL FIA register operations. Not connector admission and not a substitute
// for TC-cold/display-lane lifetime management. The IO adapter must own a
// cold reference throughout each operation; it serializes shared FIA RMW.
#ifndef kern_tgl_fia_hpp
#define kern_tgl_fia_hpp
#include "kern_tgl_port_map.hpp"
struct TGLFia {
 enum class Status { Success, Invalid, NoPower, Unavailable, NotReady, Timeout, WrongMode, ReadbackFailed };
 struct Result {
  Status status {Status::Invalid}; TGLFiaPort port {};
  uint32_t before {}, after {}, polls {};
  bool wrote {};
  bool ok() const { return status==Status::Success; }
 };
 // hasColdReference must refer to a reference owned by this operation's
 // caller, not just an unrelated port's positive global power count.
 // readable checks the BAR bounds without touching MMIO.
 template<class IO> static Result resolve(IO &io,uint32_t ddi) {
  Result r;TGLTypeCPort tc {};
  if (!TGLTypeCPort::resolve(ddi,tc)) return r;
  if (!io.hasColdReference(ddi)) {r.status=Status::NoPower;return r;}
  if (!io.readable(0x1638A0)) {r.status=Status::Unavailable;return r;}
  const auto first=io.read(0x1638A0);
  if (first==0xFFFFFFFFU) {r.status=Status::Unavailable;return r;}
  if (!tc.resolveFia(first,r.port)) return r;
  if (!io.readable(r.port.live)||!io.readable(r.port.ready)||!io.readable(r.port.owned)) {
   r.status=Status::Unavailable;return r;
  }
  r.status=Status::Success;return r;
 }
 // A legacy DP connection must not steal a PHY reported as DP-alt/TBT.
 // Keep the donor's 100 polls / 1ms interval, without adding the separate
 // -noAUXDelay policy here. That delay belongs to the native mode adapter.
 template<class IO> static Result waitLegacyReady(IO &io,uint32_t ddi) {
  auto r=resolve(io,ddi);if(!r.ok())return r;
  for(unsigned n=0;n<100;++n) {
   auto live=io.read(r.port.live);auto ready=io.read(r.port.ready);++r.polls;
   if(live==0xFFFFFFFFU || ready==0xFFFFFFFFU) {r.status=Status::Unavailable;return r;}
   if(live&r.port.altMask) {r.status=Status::WrongMode;return r;}
   if(ready&r.port.readyMask) return r;
   io.sleepMs(1);
  }
  r.status=Status::Timeout;return r;
 }
 // The caller serializes mode changes on its port. The shared register lock
 // covers only reads/write/readback, never native calls or sleeps. Release
 // is allowed only after the caller has quiesced all users of this PHY;
 // this low-level function deliberately does not infer hardware idle.
 template<class IO> static Result setOwned(IO &io,uint32_t ddi,bool take) {
  auto r=resolve(io,ddi);if(!r.ok())return r;
  io.fiaLock(r.port.instance);
  struct Unlock {IO &io;uint32_t i;~Unlock(){io.fiaUnlock(i);}} unlock {io,r.port.instance};
  if(take) {
   const auto live=io.read(r.port.live),ready=io.read(r.port.ready);
   if(live==0xFFFFFFFFU || ready==0xFFFFFFFFU) {r.status=Status::Unavailable;return r;}
   if(live&r.port.altMask) {r.status=Status::WrongMode;return r;}
   if(!(ready&r.port.readyMask)) {r.status=Status::NotReady;return r;}
  }
  r.before=io.read(r.port.owned);
  if(r.before==0xFFFFFFFFU) {r.status=Status::Unavailable;return r;}
  const auto value=take ? r.before|r.port.readyMask : r.before&~r.port.readyMask;
  if(value!=r.before) {io.write(r.port.owned,value);r.wrote=true;}
  r.after=io.read(r.port.owned);
  if(r.after==0xFFFFFFFFU) {r.status=Status::Unavailable;return r;}
  if(bool(r.after&r.port.readyMask)!=take)r.status=Status::ReadbackFailed;
  // A failed readback after a write leaves ownership uncertain. The caller
  // must retain its cold/lane references and reconcile; never blindly undo
  // the whole previous register, which could overwrite another port.
  return r;
 }
};
#endif
