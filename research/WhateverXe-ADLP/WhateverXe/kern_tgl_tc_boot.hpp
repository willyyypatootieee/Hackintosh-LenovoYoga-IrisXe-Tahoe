// Cold bootstrap -> per-port legacy Mode handoff. Wiring intent, backend
// qualification, and current hardware evidence are independent inputs.
// Kernel caller must run this inside a powered readout's consume callback,
// with native lifecycle entry excluded and live objects/MMIO retained.
#ifndef kern_tgl_tc_boot_hpp
#define kern_tgl_tc_boot_hpp
#include "kern_tgl_tc_mode.hpp"
#include "kern_tgl_boot_readout.hpp"
struct TGLTCWiring {
 static constexpr uint32_t Mask=0x1F8; // physical DDI3..8, not framebuffer IDs
 uint32_t legacy {},typeC {},excluded {};bool valid {};
 static TGLTCWiring fromMasks(uint32_t l,uint32_t t,uint32_t x) {
  TGLTCWiring r;if(((l|t|x)&~Mask)||(l&t)||(l&x)||(t&x))return r;
  r.legacy=l;r.typeC=t;r.excluded=x;r.valid=true;return r;
 }
 // Explicit IGPU property tgl-tc-wiring: 4 little-endian uint32 words,
 // version(1), legacy-DDI mask, Type-C-DDI mask, excluded-DDI mask. Omitted
 // ports remain Unknown. A mask describes wiring intent, never backend
 // qualification or proof that inherited hardware has been drained.
 static TGLTCWiring fromProperty(const void *bytes,uint32_t length) {
  if(!bytes || length!=16)return {};
  auto p=static_cast<const uint8_t *>(bytes);uint32_t q[4] {};
  for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)q[i]|=uint32_t(p[4*i+j])<<(8*j);
  return q[0]==1?fromMasks(q[1],q[2],q[3]):TGLTCWiring{};
 }
 bool legacyEligible(uint32_t d,uint32_t backend) const {
  return valid && d>=3 && d<=8 && (legacy&backend&(1u<<d));
 }
};
struct TGLTCBoot {
 TGLTCMode modes[6];
 enum class PortState:uint32_t {Unresolved,Excluded,Quiescent,Referenced,Rejected};
 struct Result {
  bool attempted {},finished {};
  PortState ports[6] {};
  TGLTCMode::Result adoption[6] {};
  TGLTCCold::Result finish {};
 };
 // Host: coldIO/portIO(ddi), owner(), identity(ddi), exclusiveBootReadout(),
 // absenceVerified(ddi). portIO implements the production mode IO contract.
 // absenceVerified is independent platform proof that no inherited/display
 // use exists, NOT "not in a mask", HPD-low or an allones register response.
 template<class Host> Result handoff(Host &host,TGLTCCold &cold,const TGLTCWiring &wiring,
                                     uint32_t backend,const TGLBootReadout::Result &view) {
  Result r;
  if(!wiring.valid || !host.exclusiveBootReadout() || !view.pgHeld ||
     view.status!=TGLBootReadout::Status::Success || view.powerHeld==UINT32_MAX || view.fuseHeld==UINT32_MAX ||
     (view.powerHeld&0x55)!=0x55 || (view.fuseHeld&0x07800000)!=0x07800000 ||
     !cold.bootstrapReady(host.coldIO(),host.owner()))return r;
  r.attempted=true;TGLTCCold::BootInventory inventory;
  for(uint32_t i=0;i<6;++i){
   const auto d=i+3,bit=1u<<d;const auto &p=view.ports[i];
   bool routed=false,unresolvedPipe=false;
   for(auto v:view.func){
    if(v==UINT32_MAX){unresolvedPipe=true;continue;}
    if(v&0x80000000){const auto selected=(v>>27)&15;
     if(!selected || selected>9)unresolvedPipe=true;
     else if(selected==d+1)routed=true;
    }
   }
   if(unresolvedPipe || p.ddi!=d)continue;
   const bool readable=p.status==4 && p.index<4 && p.buffer!=UINT32_MAX &&
    p.live!=UINT32_MAX && p.ready!=UINT32_MAX && p.owned!=UINT32_MAX;
   const bool enabled=readable&&(p.buffer&0x80000000);
   const bool owned=readable&&(p.owned&(1u<<p.index));
   if(wiring.excluded&bit){
    if(!routed && !enabled && !owned && !modes[i].owner && host.absenceVerified(d)) {
     inventory.ports[i]=TGLTCCold::BootDisposition::Quiescent;r.ports[i]=PortState::Excluded;
    }
    continue;
   }
   if(!wiring.legacyEligible(d,backend)||!readable)continue;
   if(p.live&(0x60u<<(8*p.index)))continue; // observed DP-alt/TBT contradicts legacy intent
   auto &io=host.portIO(d);auto &mode=modes[i];
   // A completed partial handoff must not re-acquire its native AUX debt.
   if(mode.owner){
    if(mode.owner==host.owner() && mode.port==host.identity(d) &&
       mode.phase==TGLTCMode::Phase::Active && io.nativeMode()==4 && mode.coldHeld && mode.laneHeld && io.lanesReady() &&
       cold.refs[d][static_cast<uint32_t>(TGLTCCold::Use::Mode)]) {
     inventory.ports[i]=TGLTCCold::BootDisposition::Referenced;r.ports[i]=PortState::Referenced;
    } else r.ports[i]=PortState::Rejected;
    continue;
   }
   if(routed||enabled||owned){
    // Includes owned-but-buffer-disabled firmware state observed on TC2.
    r.adoption[i]=mode.adoptFirmware(io,cold,host.owner(),host.identity(d),d);
    if(r.adoption[i].ok()){
     inventory.ports[i]=TGLTCCold::BootDisposition::Referenced;r.ports[i]=PortState::Referenced;
    }else r.ports[i]=PortState::Rejected;
   }else if(io.nativeMode()==0 && io.quiescent()) {
    inventory.ports[i]=TGLTCCold::BootDisposition::Quiescent;r.ports[i]=PortState::Quiescent;
   }
  }
  r.finish=cold.finishBootstrap(host.coldIO(),host.owner(),inventory);
  r.finished=r.finish.ok();return r;
 }
 // Every later setter for an owned/eligible legacy port must use this same
 // slot. Calling native HAL again would acquire another AUX ref even for 4->4.
 template<class IO> TGLTCMode::Result request(IO &io,TGLTCCold &cold,uintptr_t owner,
                                           uintptr_t identity,uint32_t ddi,uint32_t mode) {
  if(ddi<3||ddi>8)return {};
  if(mode==4)return modes[ddi-3].connect(io,cold,owner,identity,ddi);
  if(mode==0)return modes[ddi-3].disconnect(io,cold,owner,identity,ddi);
  TGLTCMode::Result r;r.status=TGLTCMode::Status::NotAdmitted;return r;
 }
};
#endif
