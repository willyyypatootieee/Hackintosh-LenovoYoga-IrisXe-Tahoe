// Powered startup readout for the donor's three supported transcoders.
// Evidence for adoption, NOT an idle proof or an instruction to drop bootstrap.
// Native init may already have dropped inherited DDI/AUX wells. A powered-off
// observation must never be translated into BootDisposition::Quiescent.
#ifndef kern_tgl_boot_readout_hpp
#define kern_tgl_boot_readout_hpp
#include "kern_tgl_port_map.hpp"
struct TGLBootReadout {
 enum class Status:uint32_t {NotAdmitted,Success,CountsInvalid,PowerNotReady,Unavailable,CountsChanged};
 struct Port {uint32_t ddi {},status {},buffer {},live {},ready {},owned {},lane {},instance {},index {};};
 struct Result {
  uint32_t version=349;Status status=Status::NotAdmitted;
  uint32_t pgHeld {},getCalls {},putCalls {},before[4] {},held[4] {},after[4] {};
  uint32_t powerBefore {},powerHeld {},fuseHeld {},powerAfter {},func[3] {},activeMask {};
  Port ports[6] {};
 };
 // Caller excludes concurrent lifecycle changes, owns matching controller,
 // bootstrap and live MMIO, and uses the qualified native PG4 get/put pair.
 // The native power transition may panic; this cannot turn that into errno.
 template<class IO> static Result capture(IO &io) {
  return capture(io,[](const Result &){});
 }
 // Consumer executes before dropping PG4, under the caller's startup
 // exclusion. It may transfer cold ownership into durable per-port slots.
 // It must acquire its own long-lived lane/AUX refs; temporary PG4 is not
 // donated to it. The copied snapshot alone is not a future power guarantee.
 template<class IO,class Consumer> static Result capture(IO &io,Consumer consume) {
  Result r;if(!io.admissionReady())return r;
  for(unsigned i=0;i<4;++i){r.before[i]=io.refs(i);if(r.before[i]==UINT32_MAX){r.status=Status::CountsInvalid;return r;}}
  if(!io.readable(0x45404)||!io.readable(0x42000)){r.status=Status::Unavailable;return r;}
  r.powerBefore=io.read(0x45404);
  if(r.powerBefore==UINT32_MAX){r.status=Status::Unavailable;return r;}
  io.nativeGetPG4();r.getCalls=1;r.pgHeld=1;
  for(unsigned i=0;i<4;++i)r.held[i]=io.refs(i);
  // Unexpected native accounting cannot be fixed by blindly putting a token
  // whose ownership is no longer known. Retain owner/map and report it.
  for(unsigned i=0;i<4;++i)if(r.held[i]!=r.before[i]+1){r.status=Status::CountsChanged;return r;}
  r.powerHeld=io.read(0x45404);r.fuseHeld=io.read(0x42000);
  r.status=Status::PowerNotReady;
  if(r.powerHeld!=UINT32_MAX && r.fuseHeld!=UINT32_MAX &&
     (r.powerHeld&0x55)==0x55 && (r.fuseHeld&0x07800000)==0x07800000) {
   r.status=Status::Success;
   for(unsigned pipe=0;pipe<3;++pipe){
    const auto reg=0x60400+0x1000*pipe;
    if(!io.readable(reg)){r.status=Status::Unavailable;break;}
    r.func[pipe]=io.read(reg);
    if(r.func[pipe]==UINT32_MAX){r.status=Status::Unavailable;break;}
    if(r.func[pipe]&0x80000000)r.activeMask|=1u<<pipe;
   }
   // TC lanes are covered by PW3; FIA additionally requires our TC-cold hold.
   if(!io.readable(0x1638A0))r.status=Status::Unavailable;
   else {
    const auto first=io.read(0x1638A0);
    if(first==UINT32_MAX)r.status=Status::Unavailable;
    for(unsigned i=0;i<6;++i){
     auto &p=r.ports[i];p.ddi=i+3;TGLTypeCPort tc;TGLFiaPort fia;
     TGLTypeCPort::resolve(p.ddi,tc);
     // Unresolved non-modular TC5/6 is not evidence that those ports are idle.
     if(!tc.resolveFia(first,fia)){p.status=first==UINT32_MAX?3:1;continue;}
     p.instance=fia.instance;p.index=fia.index;
     if(!io.readable(tc.ddiBuffer)||!io.readable(fia.live)||!io.readable(fia.ready)||
        !io.readable(fia.owned)||!io.readable(fia.laneSelect)){p.status=2;continue;}
     p.buffer=io.read(tc.ddiBuffer);p.live=fia.instance?io.read(fia.live):first;
     p.ready=io.read(fia.ready);p.owned=io.read(fia.owned);p.lane=io.read(fia.laneSelect);
     p.status=(p.buffer==UINT32_MAX || p.live==UINT32_MAX || p.ready==UINT32_MAX ||
               p.owned==UINT32_MAX || p.lane==UINT32_MAX)?3:4; // 4 = values readable only
    }
   }
  }
  if(r.status==Status::Success)consume(r);
  uint32_t beforePut[4];
  for(unsigned i=0;i<4;++i){beforePut[i]=io.refs(i);if(!beforePut[i]){r.status=Status::CountsChanged;return r;}}
  io.nativePutPG4();r.putCalls=1;r.pgHeld=0;
  for(unsigned i=0;i<4;++i){r.after[i]=io.refs(i);if(r.after[i]!=beforePut[i]-1)r.status=Status::CountsChanged;}
  r.powerAfter=io.read(0x45404);return r;
 }
};
#endif
