#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>
#include <algorithm>
#include <sys/mman.h>
#include "kern_tgl_tc_mode.hpp"
#include "kern_tgl_tc_boot.hpp"
using S=TGLTCMode::Status;using Use=TGLTCCold::Use;
using BaseFn=uint32_t(*)(void*,uint32_t);BaseFn nativeBase;uintptr_t nativeVtable[64] {};unsigned zeroCalls {};
void nativeBzero(void *p,size_t n){++zeroCalls;std::memset(p,0,n);}
void jump(uint8_t *p,uintptr_t t){p[0]=0x48;p[1]=0xb8;std::memcpy(p+2,&t,8);p[10]=0xff;p[11]=0xe0;}
void relocate(uint8_t *p,unsigned site,unsigned target,uint8_t opcode){assert(p[site]==opcode);int32_t displacement=target-site-5;std::memcpy(p+site+1,&displacement,4);}
struct Shared {
 TGLTCCold cold;
 bool coldLockHeld=false,pcode=false,submitted=false,failBlock=false,failUnblock=false;
 bool fiaLocked=false,dropWrite=false,badReadback=false;
 uint32_t low {},blocks {},unblocks {},fiaReads {},fiaWrites {};uint64_t clock {};
 std::map<uint32_t,uint32_t> regs;
 Shared(){for(auto b:{0x163000u,0x16e000u,0x16f000u}){regs[b+0x8a0]=b==0x163000?0x10:0;regs[b+0x890]=3;regs[b+0x894]=0x40000000;}}
};
struct IO {
 Shared &s;unsigned ddi;bool admitted=true,inherited=false,firmware=false,idle=true,needAux=true;
 unsigned aux {},gets {},puts {},baseCalls {},lanes {},laneGets {},lanePuts {};bool laneReady=true;uint32_t baseError {};
 alignas(8) uint8_t port[0x600] {};uint32_t config[8] {};
 IO(Shared &bus,unsigned d):s(bus),ddi(d){config[0]=ddi;config[5]=0x201;*reinterpret_cast<void**>(port+0x548)=config;*reinterpret_cast<void**>(port)=nativeVtable;}
 uintptr_t tag(){return (uintptr_t)port;}
 bool firmwareVerified(){return firmware;}
 bool admissionReady(){return admitted;}bool inheritedVerified(){return inherited;}
 uint32_t nativeMode(){return *reinterpret_cast<uint32_t*>(port+0x550);}
 uint32_t nativeSetMode(uint32_t v){assert(s.cold.refs[ddi][unsigned(Use::Mode)] && s.cold.phase==TGLTCCold::Phase::Blocked);++baseCalls;auto r=nativeBase(port,v);assert(r==0);return baseError;}
 void nativeGetLanes(){assert(s.cold.refs[ddi][unsigned(Use::Mode)]);++lanes;++laneGets;}
 void nativePutLanes(){assert(lanes && s.cold.refs[ddi][unsigned(Use::Mode)]);--lanes;++lanePuts;}
 bool lanesReady(){return lanes && laneReady;}
 bool nativeNeedsAux(){return needAux;}
 void nativeGetAux(){assert(nativeMode()==4);++aux;++gets;}
 void nativePutAux(){assert(aux && nativeMode()==0);--aux;++puts;}
 bool quiescent(){return idle;}
 void coldLock(){assert(!s.coldLockHeld);s.coldLockHeld=true;}void coldUnlock(){assert(s.coldLockHeld);s.coldLockHeld=false;}
 bool pcodeTryLock(){assert(s.coldLockHeld && !s.pcode);s.pcode=true;s.submitted=false;return true;}
 void pcodeUnlock(){assert(s.pcode);s.pcode=false;}
 bool readable(uint32_t r){return r<0x170000;}
 uint32_t read(uint32_t r){
  if(r==0x138124){assert(s.pcode);return s.submitted && ((s.low==0 && s.failBlock)||(s.low==1 && s.failUnblock))?1:0;}
  if(r==0x138128 || r==0x13812c){assert(s.pcode);return 0;}
  assert(!s.pcode && s.cold.phase==TGLTCCold::Phase::Blocked && s.cold.refs[ddi][unsigned(Use::Mode)]);
  assert(lanesReady());++s.fiaReads;
  if(s.badReadback && s.fiaWrites && (r&0xfff)==0x894)return ~0u;
  return s.regs.at(r);
 }
 void write(uint32_t r,uint32_t v){
  if(r==0x138128){assert(s.pcode);s.low=v;return;}
  if(r==0x13812c){assert(s.pcode && v==0);return;}
  if(r==0x138124){assert(s.pcode && v==0x80000026);s.submitted=true;if(s.low)++s.unblocks;else ++s.blocks;return;}
  assert(lanesReady() && s.fiaLocked && s.cold.refs[ddi][unsigned(Use::Mode)]);++s.fiaWrites;if(!s.dropWrite)s.regs[r]=v;
 }
 uint64_t nowUs(){return s.clock;}void delayUs(unsigned n){s.clock+=n;}void sleepMs(unsigned n){assert(!s.fiaLocked);s.clock+=n*1000;}
 void fiaLock(unsigned i){assert(i==(ddi-3)/2 && !s.fiaLocked);s.fiaLocked=true;}
 void fiaUnlock(unsigned i){assert(i==(ddi-3)/2 && s.fiaLocked);s.fiaLocked=false;}
};
struct BootHost {
 Shared &s;IO p3,p4,p5,p6,p7,p8;bool exclusive=true;uint32_t absent=0;
 BootHost(Shared &bus):s(bus),p3(bus,3),p4(bus,4),p5(bus,5),p6(bus,6),p7(bus,7),p8(bus,8){
  for(unsigned d=3;d<=8;++d){portIO(d).firmware=true;portIO(d).aux=1;}
 }
 IO &coldIO(){return p3;}uintptr_t owner(){return 1;}
 bool exclusiveBootReadout(){return exclusive;}bool absenceVerified(unsigned d){return absent&(1u<<d);}
 IO &portIO(unsigned d){switch(d){case 3:return p3;case 4:return p4;case 5:return p5;case 6:return p6;case 7:return p7;default:assert(d==8);return p8;}}
 uintptr_t identity(unsigned d){return portIO(d).tag();}
 TGLBootReadout::Result view(uint32_t active,uint32_t owned){
  TGLBootReadout::Result v;v.status=TGLBootReadout::Status::Success;v.pgHeld=1;v.powerHeld=0xff;v.fuseHeld=0x07800000;
  unsigned pipe=0;
  for(unsigned i=0;i<6;++i){auto d=i+3;TGLTypeCPort tc;TGLFiaPort fia;
   assert(TGLTypeCPort::resolve(d,tc)&&tc.resolveFia(0x10,fia));auto &p=v.ports[i];
   p.ddi=d;p.status=4;p.index=fia.index;p.instance=fia.instance;p.live=s.regs[fia.live];p.ready=s.regs[fia.ready];
   if(owned&(1u<<d))s.regs[fia.owned]|=fia.readyMask;p.owned=s.regs[fia.owned];
   if(active&(1u<<d)){assert(pipe<3);p.buffer=0x80000000;v.func[pipe++]=0x80000000|((d+1)<<27);}
  }
  return v;
 }
};
void bootstrapHandoffTests(){
 using P=TGLTCBoot::PortState;const auto all=TGLTCWiring::Mask,three=(1u<<4)|(1u<<5)|(1u<<6);
 assert(!TGLTCWiring::fromMasks(1,0,0).valid);
 assert(!TGLTCWiring::fromMasks(three,1u<<4,0).valid);
 assert(!TGLTCWiring::fromMasks(three,0,three).valid);
 const auto compatibility=TGLTCWiring::fromMasks(1u<<4,0,0);
 assert(compatibility.legacyEligible(4,all)&&!compatibility.legacyEligible(5,all));
 assert(!compatibility.legacyEligible(4,0)&&!compatibility.excluded);
 // Complete three-port handoff, then repeated mode4 requests and every drain
 // order. Uses original native base setter/reset, seeded native AUX debt.
 unsigned order[]={4,5,6},cases=0;
 do {
  Shared s;BootHost host(s);TGLTCBoot boot;host.absent=all^three;
  assert(s.cold.beginBootstrap(host.coldIO(),1).ok());
  const auto wiring=TGLTCWiring::fromMasks(three,0,all^three);auto v=host.view(three,three);
  auto r=boot.handoff(host,s.cold,wiring,three,v);
  assert(r.finished&&!s.cold.bootstrapHeld&&s.cold.total==3&&!s.unblocks);
  for(auto d:{4u,5u,6u}){auto &io=host.portIO(d);assert(io.aux==2&&io.gets==1&&r.ports[d-3]==P::Referenced);
   for(unsigned n=0;n<3;++n)assert(boot.request(io,s.cold,1,io.tag(),d,4).ok());
   assert(io.gets==1&&io.aux==2);io.idle=false;
   assert(boot.request(io,s.cold,1,io.tag(),d,0).status==S::InUse);
   assert(boot.request(io,s.cold,1,io.tag(),d,3).status==S::NotAdmitted);
  }
  for(auto d:order){auto &io=host.portIO(d);io.idle=true;assert(boot.request(io,s.cold,1,io.tag(),d,0).ok());assert(io.aux==1&&io.puts==1);}
  assert(s.unblocks==1&&!s.cold.total);++cases;
 }while(std::next_permutation(order,order+3));assert(cases==6);
 // Reproduce r349 shape: no TC scanout, TC2 PHY owned, TC5/6 unreadable.
 // The incomplete profile may adopt TC2 but must retain bootstrap; retry
 // must not add an AUX ref. A later independently proved topology can finish.
 {Shared s;BootHost host(s);TGLTCBoot boot;assert(s.cold.beginBootstrap(host.coldIO(),1).ok());
  auto v=host.view(0,1u<<4);for(unsigned i=4;i<6;++i){v.ports[i].status=3;v.ports[i].live=UINT32_MAX;v.ports[i].owned=UINT32_MAX;}
  auto r=boot.handoff(host,s.cold,compatibility,all,v);assert(r.attempted&&!r.finished&&r.ports[1]==P::Referenced);
  assert(s.cold.total==2&&s.cold.bootstrapHeld&&host.p4.gets==1&&host.p4.nativeMode()==4);
  r=boot.handoff(host,s.cold,compatibility,all,v);assert(!r.finished&&s.cold.total==2&&host.p4.gets==1&&!s.unblocks);
  const auto declared=TGLTCWiring::fromMasks(1u<<4,0,all^(1u<<4));
  assert(!boot.handoff(host,s.cold,declared,all,v).finished); // declaration alone is insufficient
  host.absent=all^(1u<<4);r=boot.handoff(host,s.cold,declared,all,v);assert(r.finished&&s.cold.total==1&&!s.unblocks);
 }
 // Reported absent wiring may not override an enabled or owned resource.
 for(bool owned:{false,true}){Shared s;BootHost host(s);TGLTCBoot boot;host.absent=all;
  assert(s.cold.beginBootstrap(host.coldIO(),1).ok());auto v=host.view(owned?0:1u<<4,owned?1u<<4:0);
  auto r=boot.handoff(host,s.cold,TGLTCWiring::fromMasks(0,0,all),all,v);assert(!r.finished&&r.ports[1]==P::Unresolved&&!s.unblocks);
 }
 // A legacy declaration does not override a contradictory live alt mode,
 // even if buffer/ownership are zero and the native idle predicate is true.
 {Shared s;BootHost host(s);TGLTCBoot boot;assert(s.cold.beginBootstrap(host.coldIO(),1).ok());
  auto v=host.view(0,0);v.ports[1].live|=0x2000;
  auto r=boot.handoff(host,s.cold,compatibility,all,v);assert(r.ports[1]==P::Unresolved&&!r.finished&&!host.p4.gets);
 }
 // Wiring intent cannot bypass backend qualification, power or exclusivity.
 for(unsigned fault=0;fault<4;++fault){Shared s;BootHost host(s);TGLTCBoot boot;assert(s.cold.beginBootstrap(host.coldIO(),1).ok());auto v=host.view(0,1u<<4);
  if(fault==0)host.exclusive=false;if(fault==1)v.pgHeld=0;if(fault==2)v.fuseHeld=0;
  auto r=boot.handoff(host,s.cold,compatibility,fault==3?0:all,v);assert(!r.finished&&!host.p4.gets&&s.cold.total==1);
 }
 puts("PASS: powered bootstrap handoff -> original native modes -> repeated mode4 without extra AUX -> all 6 three-port drain orders; r349 owned-but-disabled/unreadable partial handoff retained, profile alone never proves absence");
}
int main(int argc,char **argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(f)),{});
 const size_t size=4*1024*1024;assert(b.size()>0x138bb0 && b.size()<size);
 auto m=(uint8_t*)mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0);assert(m!=MAP_FAILED);std::memcpy(m,b.data(),b.size());nativeBase=(BaseFn)(m+0x1b4c8);
 nativeVtable[0x120/8]=(uintptr_t)(m+0x1b11c);
 jump(m+0x300000,(uintptr_t)nativeBzero);
 relocate(m,0x1b16e,0x300000,0xe8);relocate(m,0x1b17c,0x300000,0xe8);relocate(m,0x1b196,0x300000,0xe9);
 bootstrapHandoffTests();
 {
  Shared s;IO a{s,4},b{s,5},c{s,6};IO *ios[]={&a,&b,&c};TGLTCMode modes[3];
  assert(s.cold.beginBootstrap(a,1).ok());TGLTCCold::BootInventory inventory;
  for(auto &p:inventory.ports)p=TGLTCCold::BootDisposition::Quiescent;
  for(auto d:{4u,5u,6u})inventory.ports[d-3]=TGLTCCold::BootDisposition::Referenced;
  for(unsigned i=0;i<3;++i){
   assert(s.cold.finishBootstrap(a,1,inventory).status==TGLTCCold::Status::InUse);
   assert(modes[i].connect(*ios[i],s.cold,1,ios[i]->tag(),ios[i]->ddi).ok());
  }
  assert(s.cold.total==4 && s.blocks==1 && !s.unblocks);
  assert(s.cold.finishBootstrap(a,1,inventory).ok() && s.cold.total==3);
  for(unsigned i=0;i<3;++i)assert(modes[i].disconnect(*ios[i],s.cold,1,ios[i]->tag(),ios[i]->ddi).ok());
  assert(s.unblocks==1 && s.cold.total==0);
 }
 // Firmware mode0 handoff takes independent Mode/AUX ownership without
 // stealing a seeded native AUX reference or writing an already-owned FIA bit.
 for(unsigned d=3;d<=8;++d)for(bool owned:{false,true})for(bool needAux:{false,true}){
  Shared s;IO io{s,d};TGLTCMode mode;io.firmware=true;io.idle=false;io.needAux=needAux;io.aux=1;
  TGLTypeCPort tc;TGLFiaPort fia;assert(TGLTypeCPort::resolve(d,tc)&&tc.resolveFia(0x10,fia));
  if(owned)s.regs[fia.owned]|=fia.readyMask;
  assert(s.cold.beginBootstrap(io,1).ok());
  assert(mode.adoptFirmware(io,s.cold,1,io.tag(),d).ok());
  assert(s.fiaWrites==(owned?0u:1u) && io.gets==unsigned(needAux));
  assert(io.aux==1+unsigned(needAux) && io.nativeMode()==4 && s.cold.total==2);
  assert(io.lanes==1 && io.laneGets==1);
  const auto writes=s.fiaWrites;
  assert(mode.adoptFirmware(io,s.cold,1,io.tag(),d).status==S::InUse && io.gets==unsigned(needAux));
  assert(mode.disconnect(io,s.cold,1,io.tag(),d).status==S::InUse && s.fiaWrites==writes);
  TGLTCCold::BootInventory inventory;for(auto &p:inventory.ports)p=TGLTCCold::BootDisposition::Quiescent;
  inventory.ports[d-3]=TGLTCCold::BootDisposition::Referenced;
  assert(s.cold.finishBootstrap(io,1,inventory).ok() && !s.unblocks);
  io.idle=true;assert(mode.disconnect(io,s.cold,1,io.tag(),d).ok());
  assert(io.aux==1 && io.puts==unsigned(needAux) && s.unblocks==1);
  assert(!io.lanes && io.lanePuts==1);
 }
 for(unsigned fault=0;fault<7;++fault){
  Shared s;IO io{s,5};TGLTCMode mode;io.firmware=true;io.idle=false;
  if(fault!=0)assert(s.cold.beginBootstrap(io,1).ok());
  if(fault==1)io.firmware=false;
  if(fault==2)s.regs[0x16e8a0]=0x20; // DP-alt, not legacy
  if(fault==3)s.regs[0x16e890]=0;
  if(fault==4)s.regs[0x16e894]=~0u;
  if(fault==5)io.baseError=0xe00002bc;
  if(fault==6)s.badReadback=true;
  assert(!mode.adoptFirmware(io,s.cold,1,io.tag(),5).ok());
  if(fault<2)assert(!mode.owner && !s.fiaReads && !io.baseCalls);
  else {
   assert(mode.phase==TGLTCMode::Phase::NeedsCleanup && mode.coldHeld && !s.unblocks);
   if(fault==2||fault==3)assert(!io.baseCalls && !io.gets);
   const auto writes=s.fiaWrites;
   assert(mode.disconnect(io,s.cold,1,io.tag(),5).status==S::InUse && s.fiaWrites==writes);
   io.idle=true;s.badReadback=false;io.baseError=0;s.regs[0x16e8a0]=0;s.regs[0x16e890]=3;s.regs[0x16e894]=0;
   assert(mode.disconnect(io,s.cold,1,io.tag(),5).ok() && s.cold.bootstrapHeld && !s.unblocks);
  }
 }
 {Shared s;IO io{s,5};TGLTCMode mode;io.firmware=true;assert(s.cold.beginBootstrap(io,1).ok());
  assert(mode.adoptFirmware(io,s.cold,2,io.tag(),5).status==S::NeedsAdoption && !s.fiaReads);
  assert(nativeBase(io.port,4)==0);
  assert(mode.adoptFirmware(io,s.cold,1,io.tag(),5).status==S::NeedsAdoption && !s.fiaReads);
 }
 {Shared s;IO io{s,4};TGLTCMode mode;io.laneReady=false;
  assert(mode.connect(io,s.cold,1,io.tag(),4).status==S::PowerError);
  assert(mode.coldHeld&&mode.laneHeld&&io.lanes==1&&!s.fiaReads&&!io.gets);
  io.laneReady=true;assert(mode.disconnect(io,s.cold,1,io.tag(),4).ok());
  assert(!io.lanes&&io.laneGets==1&&io.lanePuts==1&&!s.cold.total);
 }
 puts("PASS: 24 firmware mode0 handoffs across TC1..6; existing ownership preserved/unowned legacy repaired; AUX seed preserved; missing bootstrap/identity/legacy, hardware and native failures retained");
 unsigned order[]={0,1,2},permutations=0;
 do {
  Shared s;IO a{s,4},b{s,5},c{s,6};IO *ios[]={&a,&b,&c};TGLTCMode modes[3];
  for(unsigned i=0;i<3;++i)assert(modes[i].connect(*ios[i],s.cold,1,ios[i]->tag(),ios[i]->ddi).ok());
  assert(s.blocks==1 && s.cold.total==3 && s.regs[0x163894]==0x40000002 && s.regs[0x16e894]==0x40000003);
  for(unsigned i=0;i<3;++i){auto k=order[i];assert(modes[k].disconnect(*ios[k],s.cold,1,ios[k]->tag(),ios[k]->ddi).ok());assert(s.unblocks==(i==2?1u:0u));}
  assert(s.cold.total==0 && s.cold.owner==0 && s.regs[0x16e894]==0x40000000);++permutations;
 }while(std::next_permutation(order,order+3));
 {Shared s;IO io{s,5};TGLTCMode mode;io.admitted=false;assert(mode.connect(io,s.cold,1,io.tag(),5).status==S::NotAdmitted && !s.blocks && !s.fiaReads && !io.baseCalls);}
 {Shared s;IO io{s,5};TGLTCMode mode;assert(nativeBase(io.port,4)==0);assert(mode.connect(io,s.cold,1,io.tag(),5).status==S::NeedsAdoption && !s.blocks);assert(mode.adopt(io,s.cold,1,io.tag(),5,true).status==S::NeedsAdoption);}
 // An inherited output has an independent bootstrap reference. Adoption
 // transfers its known mode AUX debt, without acquiring a duplicate AUX ref.
 {Shared s;IO io{s,5};TGLTCMode mode;assert(s.cold.beginBootstrap(io,1).ok());
  assert(nativeBase(io.port,4)==0);io.aux=1;io.inherited=true;s.regs[0x16e894]|=1;
  assert(mode.adopt(io,s.cold,1,io.tag(),5,true).ok() && io.gets==0 && s.cold.total==2);
  assert(mode.disconnect(io,s.cold,1,io.tag(),5).ok() && io.puts==1 && !s.unblocks);
  TGLTCCold::BootInventory inventory;for(auto &p:inventory.ports)p=TGLTCCold::BootDisposition::Quiescent;
  assert(s.cold.finishBootstrap(io,1,inventory).ok() && s.unblocks==1);
 }
 for(auto value:{0u,0xffffffffu}){Shared s;IO io{s,5};TGLTCMode mode;
  assert(nativeBase(io.port,4)==0);io.inherited=true;s.regs[0x16e894]=value;
  const auto r=mode.adopt(io,s.cold,1,io.tag(),5,false);
  assert(r.status==S::HardwareError && !r.hardware.ok() && mode.coldHeld && !s.fiaWrites);
  s.regs[0x16e894]=0;assert(mode.disconnect(io,s.cold,1,io.tag(),5).ok());
 }
 for(unsigned fault=0;fault<6;++fault){
  Shared s;IO io{s,5};TGLTCMode mode;
  if(fault==0)s.failBlock=true;
  if(fault==1)s.regs[0x16e890]=0;
  if(fault==2)s.regs[0x16e890]=~0u;
  if(fault==3)s.dropWrite=true;
  if(fault==4)s.badReadback=true;
  if(fault==5)io.baseError=0xe00002bc;
  auto r=mode.connect(io,s.cold,1,io.tag(),5);assert(!r.ok() && mode.phase==TGLTCMode::Phase::NeedsCleanup);
  if(fault==0)assert(!mode.coldHeld && !io.baseCalls && !s.fiaReads);
  else assert(mode.coldHeld && s.cold.total==1 && !s.unblocks);
  io.idle=false;const auto reads=s.fiaReads,writes=s.fiaWrites;
  assert(mode.disconnect(io,s.cold,1,io.tag(),5).status==S::InUse && s.fiaReads==reads && s.fiaWrites==writes);
  s.failBlock=s.dropWrite=s.badReadback=false;s.regs[0x16e890]=3;io.baseError=0;io.idle=true;io.admitted=false;
  assert(mode.disconnect(io,s.cold,1,io.tag(),5).ok() && !s.cold.total && !io.aux && !mode.owner);
 }
 {Shared s;IO io{s,5};TGLTCMode mode;assert(mode.connect(io,s.cold,1,io.tag(),5).ok());
  auto writes=s.fiaWrites;assert(mode.connect(io,s.cold,1,io.tag(),5).ok() && io.gets==1 && s.fiaWrites==writes && s.cold.total==1);
  assert(mode.disconnect(io,s.cold,2,io.tag(),5).status==S::WrongOwner && s.fiaWrites==writes);
  s.failUnblock=true;assert(mode.disconnect(io,s.cold,1,io.tag(),5).status==S::PowerError);
  assert(mode.coldHeld && !mode.auxHeld && s.cold.total==1 && io.puts==1 && io.baseCalls==2 && io.nativeMode()==0);
  auto reads=s.fiaReads;s.failBlock=true;
  assert(mode.disconnect(io,s.cold,1,io.tag(),5).status==S::PowerError && s.fiaReads==reads);
  s.failBlock=s.failUnblock=false;assert(mode.disconnect(io,s.cold,1,io.tag(),5).ok());
  assert(io.puts==1 && io.baseCalls==2 && s.cold.total==0);
 }
 {Shared s;IO io{s,5};TGLTCMode mode;s.regs[0x16e894]|=1;
  assert(mode.connect(io,s.cold,1,io.tag(),5).status==S::NeedsAdoption && mode.coldHeld && !s.fiaWrites);
  assert(mode.disconnect(io,s.cold,1,io.tag(),5).ok());
 }
 {Shared s;IO io{s,5};TGLTCMode mode;assert(mode.connect(io,s.cold,1,io.tag(),5).ok());
  const auto aux=s.cold.invokeAUX(io,1,5,[&]{assert(s.cold.total==2 && s.cold.refs[5][unsigned(Use::Mode)]==1);return 0u;});
  assert(aux.executed && aux.release.ok() && s.cold.total==1 && s.blocks==1 && !s.unblocks);
  assert(s.cold.acquire(io,1,5,Use::Scanout).ok());const auto writes=s.fiaWrites;
  assert(mode.disconnect(io,s.cold,1,io.tag(),5).status==S::InUse && s.fiaWrites==writes);
  assert(s.cold.release(io,1,5,Use::Scanout).ok());assert(mode.disconnect(io,s.cold,1,io.tag(),5).ok());
 }
 {Shared s;IO io{s,8};TGLTCMode mode;io.needAux=false;assert(mode.connect(io,s.cold,1,io.tag(),8).ok() && !io.gets && s.cold.total==1);assert(mode.disconnect(io,s.cold,1,io.tag(),8).ok() && !io.puts);}
 assert(zeroCalls>0);
 munmap(m,size);
 printf("PASS: real native base setter/resetSoftwareState + TC-cold + FIA mode adapter; %u three-port release orders, explicit adoption, early refusal, retained failure, quiescent drain, uncertain PCODE refresh without replaying completed native cleanup\n",permutations);
 puts("Host lifecycle admission/quiescence/adoption facts remain mocked, not kernel-wired or hardware-qualified.");
}
