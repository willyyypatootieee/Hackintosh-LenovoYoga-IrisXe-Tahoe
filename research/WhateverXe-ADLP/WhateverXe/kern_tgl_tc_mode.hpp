// Legacy DP mode lifetime adapter. Native port serialization and controller
// lifecycle admission belong to the host. Never enable admission until EFI/
// native outputs are adopted and every PCODE/FIA writer is coordinated.
// The host must retain the controller, port object and live MMIO mapping
// while this slot is bound, including failed acquisition/cleanup states.
#ifndef kern_tgl_tc_mode_hpp
#define kern_tgl_tc_mode_hpp
#include "kern_tgl_tc_cold.hpp"
#include "kern_tgl_fia.hpp"
struct TGLTCMode {
 enum class Phase { Idle, Active, NeedsCleanup };
 enum class Status { Success, Invalid, NotAdmitted, WrongOwner, NeedsAdoption, NativeError, HardwareError, PowerError, InUse };
 struct Result {
  Status status {Status::Invalid};uint32_t native {};
  TGLTCCold::Result power {};TGLFia::Result hardware {};
  bool ok() const{return status==Status::Success;}
 };
 uintptr_t owner {},port {};uint32_t ddi {};
 Phase phase {Phase::Idle};bool coldHeld {},auxHeld {},laneHeld {};
 TGLTCMode()=default;TGLTCMode(const TGLTCMode&)=delete;TGLTCMode& operator=(const TGLTCMode&)=delete;
 // IO protocol adds admissionReady/nativeMode/nativeSetMode/nativeNeedsAux/
 // nativeGetAux/nativePutAux/quiescent to the PCODE and FIA IO protocols.
 // nativeGetLanes/nativePutLanes own a separate display-lane power reference;
 // lanesReady checks actual state after acquisition (native transitions may
 // panic, not return an error). A skipped native AUX ref never skips lanes.
 // quiescent must prove no training, AUX, scanout or other port use remains;
 // a software mode value or an absent HPD bit is not sufficient. New uses
 // must remain excluded throughout disconnect, not just at one snapshot.
 template<class IO> Result connect(IO &io,TGLTCCold &cold,uintptr_t who,uintptr_t identity,uint32_t d) {
  Result r;if(!valid(who,identity,d))return r;
  if(owner && (owner!=who || port!=identity || ddi!=d)){r.status=Status::WrongOwner;return r;}
  if(!io.admissionReady()){r.status=Status::NotAdmitted;return r;}
  if(phase==Phase::Active){
   if(io.nativeMode()!=4){r.status=Status::NativeError;return r;}
   if(!hold(io,cold,r))return r;
   r.status=Status::Success;return r;
  }
  if(phase==Phase::NeedsCleanup){r.status=Status::InUse;return r;}
  // Do not take over an inherited live port just because the software slot
  // has no token yet. The host must perform explicit bootstrap adoption.
  if(io.nativeMode()!=0){r.status=Status::NeedsAdoption;return r;}
  owner=who;port=identity;ddi=d;phase=Phase::NeedsCleanup;
  if(!hold(io,cold,r))return r;
  r.native=io.nativeSetMode(4);
  if(r.native || io.nativeMode()!=4){r.status=Status::NativeError;return r;}
  if(io.nativeNeedsAux()){io.nativeGetAux();auxHeld=true;}
  FiaIO<IO> access {io,*this};
  r.hardware=TGLFia::waitLegacyReady(access,ddi);
  if(!r.hardware.ok()){r.status=Status::HardwareError;return r;}
  r.hardware=TGLFia::setOwned(access,ddi,true);
  if(!r.hardware.ok()){r.status=Status::HardwareError;return r;}
  if(r.hardware.before&r.hardware.port.readyMask){r.status=Status::NeedsAdoption;return r;}
  phase=Phase::Active;r.status=Status::Success;return r;
 }
 // Bootstrap-only adoption of a native legacy mode. The host must verify
 // its hardware/lifetime identity and identify the one AUX reference owed
 // by that mode; never infer this from the total native AUX count alone.
 template<class IO> Result adopt(IO &io,TGLTCCold &cold,uintptr_t who,uintptr_t identity,uint32_t d,bool nativeAuxOwed) {
  Result r;if(!valid(who,identity,d))return r;
  if(owner){r.status=Status::InUse;return r;}
  if(!io.inheritedVerified() || io.nativeMode()!=4){r.status=Status::NeedsAdoption;return r;}
  owner=who;port=identity;ddi=d;phase=Phase::NeedsCleanup;auxHeld=nativeAuxOwed;
  if(!hold(io,cold,r))return r;
  FiaIO<IO> access {io,*this};r.hardware=TGLFia::resolve(access,ddi);
  if(!r.hardware.ok()){r.status=Status::HardwareError;return r;}
  access.fiaLock(r.hardware.port.instance);
  const auto value=access.read(r.hardware.port.owned);
  access.fiaUnlock(r.hardware.port.instance);
  r.hardware.before=r.hardware.after=value;
  if(value==0xFFFFFFFFU || !(value&r.hardware.port.readyMask)){
   r.hardware.status=value==0xFFFFFFFFU?TGLFia::Status::Unavailable:TGLFia::Status::NotReady;
   r.status=Status::HardwareError;return r;
  }
  phase=Phase::Active;r.status=Status::Success;return r;
 }
 // Firmware has hardware state but port init reset native software mode to 0.
 // firmwareVerified is a HOST proof of legacy wiring, matching live objects,
 // and held display-lane/readout power, under lifecycle exclusion. It must
 // not be derived from nativeMode()==0 or HPD alone. Keep bootstrap held.
 // Take a NEW native mode AUX reference; do not consume an inherited native
 // boot-display/AUX debt just because its total count is already nonzero.
 template<class IO> Result adoptFirmware(IO &io,TGLTCCold &cold,uintptr_t who,uintptr_t identity,uint32_t d) {
  Result r;if(!valid(who,identity,d))return r;
  if(owner){r.status=Status::InUse;return r;}
  if(!io.firmwareVerified() || io.nativeMode()!=0 || !cold.bootstrapReady(io,who)) {
   r.status=Status::NeedsAdoption;return r;
  }
  owner=who;port=identity;ddi=d;phase=Phase::NeedsCleanup;
  if(!hold(io,cold,r))return r;
  FiaIO<IO> access {io,*this};
  r.hardware=TGLFia::waitLegacyReady(access,ddi);
  if(!r.hardware.ok()){r.status=Status::HardwareError;return r;}
  r.native=io.nativeSetMode(4);
  if(r.native || io.nativeMode()!=4){r.status=Status::NativeError;return r;}
  if(io.nativeNeedsAux()){io.nativeGetAux();auxHeld=true;}
  // Already-owned legacy PHY: read/confirm without writing. BIOS can also
  // leave a legacy enabled port unowned; Linux intel_tc_port_init_mode permits
  // this ownership repair without first disabling that legacy output.
  r.hardware=TGLFia::setOwned(access,ddi,true);
  if(!r.hardware.ok()){r.status=Status::HardwareError;return r;}
  phase=Phase::Active;r.status=Status::Success;return r;
 }
 template<class IO> Result disconnect(IO &io,TGLTCCold &cold,uintptr_t who,uintptr_t identity,uint32_t d) {
  Result r;if(!valid(who,identity,d))return r;
  if(!owner){r.status=io.nativeMode()==0?Status::Success:Status::NeedsAdoption;return r;}
  if(owner!=who || port!=identity || ddi!=d){r.status=Status::WrongOwner;return r;}
  // Stopping admission does not strand existing references. The host must
  // keep the mapping/transport alive while this drain is in progress.
  if(!io.quiescent() || !cold.exclusivelyHeld(io,owner,ddi,TGLTCCold::Use::Mode,coldHeld?1:0)){r.status=Status::InUse;return r;}
  phase=Phase::NeedsCleanup;
  if(!hold(io,cold,r))return r;
  FiaIO<IO> access {io,*this};r.hardware=TGLFia::setOwned(access,ddi,false);
  if(!r.hardware.ok()){r.status=Status::HardwareError;return r;}
  // Retry cleanup without replaying already completed native operations.
  if(io.nativeMode()!=0){
   r.native=io.nativeSetMode(0);
   if(r.native || io.nativeMode()!=0){r.status=Status::NativeError;return r;}
  }
  if(auxHeld){io.nativePutAux();auxHeld=false;}
  if(laneHeld){io.nativePutLanes();laneHeld=false;}
  r.power=cold.release(io,owner,ddi,TGLTCCold::Use::Mode);
  if(!r.power.ok()){r.status=Status::PowerError;return r;}
  coldHeld=false;owner=port=0;ddi=0;phase=Phase::Idle;r.status=Status::Success;return r;
 }
private:
 static bool valid(uintptr_t who,uintptr_t identity,uint32_t d){return who && identity && d>=3 && d<=8;}
 template<class IO> bool hold(IO &io,TGLTCCold &cold,Result &r){
  r.power=coldHeld ? cold.refresh(io,owner,ddi,TGLTCCold::Use::Mode) : cold.acquire(io,owner,ddi,TGLTCCold::Use::Mode);
  if(!r.power.ok()){r.status=Status::PowerError;return false;}
  coldHeld=true;
  if(!laneHeld){io.nativeGetLanes();laneHeld=true;}
  if(!io.lanesReady()){r.status=Status::PowerError;r.hardware.status=TGLFia::Status::NoPower;return false;}
  return true;
 }
 // Created only after acquisition/refresh succeeded. This token belongs to
 // this slot, not merely to a positive controller-wide reference count.
 template<class IO> struct FiaIO {
  IO &io;TGLTCMode &slot;
  bool hasColdReference(uint32_t d){return slot.coldHeld && d==slot.ddi;}
  bool readable(uint32_t r){return io.readable(r);}
  uint32_t read(uint32_t r){return io.read(r);}
  void write(uint32_t r,uint32_t v){io.write(r,v);}
  void sleepMs(unsigned n){io.sleepMs(n);}
  void fiaLock(unsigned i){io.fiaLock(i);}void fiaUnlock(unsigned i){io.fiaUnlock(i);}
 };
};
#endif
