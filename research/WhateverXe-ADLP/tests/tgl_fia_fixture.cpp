#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include "kern_tgl_fia.hpp"
using S=TGLFia::Status;
struct Bus {
 std::map<uint32_t,uint32_t> regs;
 std::mutex locks[3];
 unsigned writes {},reads {},slept {};
};
struct IO {
 Bus &bus;bool powered=true,locked=false,dropWrite=false,missingReadback=false;
 unsigned index {},delayedReady {},readyReads {};
 uint32_t size=0x170000,selectedReady=0,selectedMask=0;
 std::vector<uint32_t> readAddresses,writeAddresses;
 bool hasColdReference(uint32_t ddi) {return powered && ddi>=3 && ddi<=8;}
 bool readable(uint32_t r) {return r<=size && size-r>=4;}
 uint32_t read(uint32_t r) {
  assert(powered && readable(r));++bus.reads;readAddresses.push_back(r);
  if(r==selectedReady && readyReads++<delayedReady)return 0;
  if(missingReadback && !writeAddresses.empty() && r==writeAddresses.back())return ~0u;
  return bus.regs.at(r);
 }
 void write(uint32_t r,uint32_t v){assert(powered && locked && r==0x163894+0x0u+(index==1?0xb000:index==2?0xc000:0));++bus.writes;writeAddresses.push_back(r);if(!dropWrite)bus.regs[r]=v;}
 void sleepMs(unsigned n){assert(!locked && n==1);bus.slept+=n;}
 void fiaLock(unsigned i){assert(!locked && i<3);bus.locks[i].lock();index=i;locked=true;}
 void fiaUnlock(unsigned i){assert(locked && i==index);locked=false;bus.locks[i].unlock();}
};
void populate(Bus &b,bool modular){
 for(unsigned i=0;i<3;++i){const uint32_t base[]={0x163000,0x16e000,0x16f000};b.regs[base[i]+0x8a0]=i?0:modular?0x10:0;b.regs[base[i]+0x890]=0;b.regs[base[i]+0x894]=0x40000000;}
}
int main(){
 unsigned cases=0;
 for(bool modular:{false,true})for(unsigned ddi=3;ddi<=8;++ddi){
  Bus b;populate(b,modular);IO io{b};TGLTypeCPort tc;TGLFiaPort f;assert(TGLTypeCPort::resolve(ddi,tc));
  if(!tc.resolveFia(modular?0x10:0,f)){assert(TGLFia::resolve(io,ddi).status==S::Invalid && b.writes==0);continue;}
  b.regs[f.ready]=f.readyMask;
  const auto adjacent=1u<<((f.index+1)%4);b.regs[f.owned]|=adjacent;
  assert(TGLFia::waitLegacyReady(io,ddi).ok());auto take=TGLFia::setOwned(io,ddi,true);
  assert(take.ok() && take.wrote && take.port.instance==f.instance && take.port.index==f.index);
  assert(take.after==(0x40000000|adjacent|f.readyMask));
  const auto count=b.writes;assert(TGLFia::setOwned(io,ddi,true).ok() && b.writes==count);
  assert(TGLFia::setOwned(io,ddi,false).ok() && b.regs[f.owned]==(0x40000000|adjacent));
  for(auto a:io.writeAddresses)assert(a==f.owned);
  ++cases;
 }
 // Refusal paths must not touch an unpowered aperture, or write based on
 // an invalid sample. Exercise FIA2 as well as the TC2 development path.
 for(unsigned ddi:{4u,5u,8u}){
  for(unsigned fault=0;fault<9;++fault){
   Bus b;populate(b,true);IO io{b};TGLTypeCPort tc;TGLFiaPort f;assert(TGLTypeCPort::resolve(ddi,tc)&&tc.resolveFia(0x10,f));b.regs[f.ready]=f.readyMask;
   if(fault==0)io.powered=false;
   if(fault==1)io.size=f.owned+3;
   if(fault==2)b.regs[0x1638a0]=~0u;
   if(fault==3)b.regs[f.live]=~0u;
   if(fault==4)b.regs[f.ready]=~0u;
   if(fault==5)b.regs[f.owned]=~0u;
   if(fault==6)b.regs[f.live]|=f.altMask;
   if(fault==7)b.regs[f.ready]=0;
   if(fault==8)io.dropWrite=true;
   auto r=TGLFia::setOwned(io,ddi,true);assert(!r.ok());
   assert(b.writes==(fault==8?1u:0u));assert(!io.locked);
   if(fault==0)assert(b.reads==0 && r.status==S::NoPower);
   if(fault==7)assert(r.status==S::NotReady);
   if(fault==8)assert(r.wrote && r.status==S::ReadbackFailed);
   ++cases;
  }
  Bus b;populate(b,true);IO io{b};TGLTypeCPort tc;TGLFiaPort f;TGLTypeCPort::resolve(ddi,tc);tc.resolveFia(0x10,f);
  b.regs[f.ready]=f.readyMask;io.missingReadback=true;
  auto r=TGLFia::setOwned(io,ddi,true);assert(r.wrote && r.status==S::Unavailable && b.regs[f.owned]&f.readyMask);
  // No speculative rollback after an uncertain write.
  assert(b.writes==1);++cases;
 }
 for(unsigned count:{0u,2u,100u}){
  Bus b;populate(b,true);b.regs[0x16e890]=1;IO io{b};io.selectedReady=0x16e890;io.delayedReady=count;
  auto r=TGLFia::waitLegacyReady(io,5);assert(r.ok()==(count<100));assert(b.slept==count && b.writes==0);++cases;
 }
 // Two ports sharing FIA2 may update concurrently. Keep this fixture's
 // registers atomic separately from the actual ownership RMW lock.
 struct ConcurrentIO {
  uint32_t &owned;std::mutex &lock;bool held=false;
  bool hasColdReference(uint32_t){return true;}bool readable(uint32_t){return true;}
  uint32_t read(uint32_t r){if(r==0x1638a0)return 0x10;if(r==0x16e8a0)return 0;if(r==0x16e890)return 3;assert(r==0x16e894 && held);return owned;}
  void write(uint32_t r,uint32_t v){assert(r==0x16e894 && held);owned=v;}
  void fiaLock(unsigned i){assert(i==1);lock.lock();held=true;}
  void fiaUnlock(unsigned i){assert(i==1 && held);held=false;lock.unlock();}
  void sleepMs(unsigned){assert(false);}
 };
 uint32_t owned=0x40000000;std::mutex lock;
 for(unsigned n=0;n<100;++n){
  ConcurrentIO a{owned,lock},b{owned,lock};
  std::thread x([&]{assert(TGLFia::setOwned(a,5,true).ok());});std::thread y([&]{assert(TGLFia::setOwned(b,6,true).ok());});x.join();y.join();assert(owned==0x40000003);
  assert(TGLFia::setOwned(a,5,false).ok() && owned==0x40000002);assert(TGLFia::setOwned(b,6,false).ok() && owned==0x40000000);
 }
 printf("PASS: %u FIA geometry/failure/wait cases plus 100 paired concurrent claims; per-instance ownership, adjacent-bit preservation, no-power/all-ones refusal, uncertain-write retention\n",cases);
 puts("FIA primitive only: caller must own TC-cold reference and quiesce before release. No kernel/native-mode wiring or physical port qualification.");
}
