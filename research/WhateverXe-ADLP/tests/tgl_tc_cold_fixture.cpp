#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include "kern_tgl_tc_cold.hpp"
using PS=TGLPcode::Status;
using CS=TGLTCCold::Status;
using Use=TGLTCCold::Use;
struct Plan { bool busy {}, missing {}, timeout {}; uint32_t status {}, response {}; uint64_t latency {}; };
struct Tx { uint32_t command {}, low {}, high {}, writes {}; };
struct IO {
 std::mutex cold,pcode;
 std::thread::id holder;
 std::vector<Plan> plans;unsigned planIndex {};
 std::vector<Tx> history;
 std::atomic<uint64_t> clock {0};
 Plan plan {};Tx tx {};bool submitted {};uint64_t at {};
 unsigned locks {},unlocks {};
 void coldLock() {cold.lock();} void coldUnlock() {cold.unlock();}
 // Deterministic transport fixture; actual nonblocking contention is tested separately.
 bool pcodeTryLock() {
  pcode.lock();holder=std::this_thread::get_id();++locks;
  plan=planIndex<plans.size()?plans[planIndex++]:Plan{};tx={};submitted=false;return true;
 }
 void pcodeUnlock() {assert(holder==std::this_thread::get_id());++unlocks;history.push_back(tx);holder={};pcode.unlock();}
 uint32_t read(uint32_t reg) {
  assert(holder==std::this_thread::get_id());
  if(reg==0x138124) {
   if(plan.missing)return ~0u;
   if(!submitted)return plan.busy?0x80000000:0;
   if(plan.timeout||clock.load()-at<plan.latency)return 0x80000000;
   return plan.status;
  }
  assert(submitted && !plan.timeout && clock.load()-at>=plan.latency);
  assert(reg==0x138128||reg==0x13812c);return reg==0x138128?plan.response:0;
 }
 void write(uint32_t reg,uint32_t value) {
  assert(holder==std::this_thread::get_id());
  const uint32_t order[]={0x138128,0x13812c,0x138124};assert(tx.writes<3 && reg==order[tx.writes++]);
  if(reg==0x138128)tx.low=value;
  else if(reg==0x13812c)tx.high=value;
  else {assert(value&0x80000000);tx.command=value&0x7fffffff;submitted=true;at=clock;}
 }
 uint64_t nowUs() {return clock;}
 void delayUs(unsigned n) {assert(holder==std::this_thread::get_id() && n==10);clock+=n;}
 void sleepMs(unsigned n) {clock+=n*1000;}
 void balanced() {assert(locks==unlocks);}
};
int main() {
 // Bootstrap is global, not a synthetic Scanout reference on some DDI.
 using BD=TGLTCCold::BootDisposition;
 for(unsigned mask=0;mask<64;++mask){
  IO io;TGLTCCold s;TGLTCCold::BootInventory inventory;
  assert(s.beginBootstrap(io,1).ok() && s.bootstrapHeld && s.total==1);
  assert(s.beginBootstrap(io,1).ok() && s.total==1 && io.history.size()==1);
  assert(s.finishBootstrap(io,1,inventory).status==CS::InUse && io.history.size()==1);
  for(unsigned i=0;i<6;++i){
   inventory.ports[i]=mask&(1u<<i)?BD::Referenced:BD::Quiescent;
   if(mask&(1u<<i))assert(s.acquire(io,1,i+3,Use::Scanout).ok());
  }
  assert(s.finishBootstrap(io,2,inventory).status==CS::WrongOwner);
  assert(s.finishBootstrap(io,1,inventory).ok() && !s.bootstrapHeld);
  assert(io.history.size()==(mask?1u:2u));
  for(unsigned i=0;i<6;++i)if(mask&(1u<<i))assert(s.release(io,1,i+3,Use::Scanout).ok());
  assert(io.history.size()==2 && io.history.back().low==1 && !s.owner && !s.total);io.balanced();
 }
 {IO io;TGLTCCold s;TGLTCCold::BootInventory v;
  assert(s.beginBootstrap(io,1).ok());
  for(auto &x:v.ports)x=BD::Quiescent;v.ports[2]=BD::Referenced;
  // DDI5 cannot borrow DDI4 Scanout or its own temporary AUX reference.
  assert(s.acquire(io,1,4,Use::Scanout).ok());assert(s.acquire(io,1,5,Use::AUX).ok());
  assert(s.finishBootstrap(io,1,v).status==CS::InUse && s.bootstrapHeld);
  assert(s.release(io,1,4,Use::Scanout).ok());assert(s.release(io,1,5,Use::AUX).ok());
  assert(s.total==1 && io.history.size()==1);
  assert(s.reconcileIdle(io,1).status==CS::InUse);
  assert(s.release(io,1,5,Use::Scanout).status==CS::Unbalanced);
  assert(s.acquire(io,1,5,Use::Mode).ok());assert(s.finishBootstrap(io,1,v).ok());
  assert(s.release(io,1,5,Use::Mode).ok());
 }
 {IO io;TGLTCCold s;TGLTCCold::BootInventory v;for(auto &x:v.ports)x=BD::Quiescent;
  io.plans.assign(3,{false,false,true});
  assert(!s.beginBootstrap(io,1).ok() && s.bootstrapHeld && s.total==1 && s.owner==1);
  assert(s.phase==TGLTCCold::Phase::Unknown);
  assert(s.beginBootstrap(io,2).status==CS::WrongOwner);
  // A successful temporary operation still cannot unblock unknown EFI uses.
  assert(s.acquire(io,1,6,Use::AUX).ok());assert(s.release(io,1,6,Use::AUX).ok());
  assert(s.total==1 && io.history.back().low==0);
  assert(s.beginBootstrap(io,1).ok() && s.total==1);
  assert(s.finishBootstrap(io,1,v).ok() && !s.bootstrapHeld && s.total==0);
 }
 {IO io;TGLTCCold s;TGLTCCold::BootInventory v;for(auto &x:v.ports)x=BD::Quiescent;
  assert(s.beginBootstrap(io,1).ok());
  io.plans.assign(3,{false,false,true});
  assert(!s.finishBootstrap(io,1,v).ok() && s.bootstrapHeld && s.total==1);
  const auto n=io.history.size();assert(s.finishBootstrap(io,1,v).ok());
  assert(io.history.size()==n+2 && io.history[n].low==0 && io.history[n+1].low==1);
 }
 {IO io;TGLTCCold s;assert(!s.beginBootstrap(io,0).ok() && io.history.empty());
  assert(s.acquire(io,1,4,Use::AUX).ok());assert(s.beginBootstrap(io,1).status==CS::InUse);
  assert(!s.bootstrapHeld && s.total==1);assert(s.release(io,1,4,Use::AUX).ok());
 }
 puts("PASS: bootstrap 64 active-port masks; unresolved/foreign/temporary-reference refusals; retained failed block/unblock; retry and no premature last-unblock");

 {IO io;auto r=TGLPcode::transfer(io,0x26,0,0);assert(r.ok() && r.submitted && r.data==0);io.balanced();}
 {IO io;io.plans={{true}};auto r=TGLPcode::transfer(io,0x26,0,0);assert(r.status==PS::Busy && !r.submitted && io.history[0].writes==0);io.balanced();}
 {IO io;io.plans={{false,true}};auto r=TGLPcode::transfer(io,0x26,0,0);assert(r.status==PS::Unavailable && !r.submitted);io.balanced();}
 {IO io;io.plans={{false,false,true}};auto r=TGLPcode::transfer(io,0x26,0,0);assert(r.status==PS::Timeout && r.submitted && io.clock==20500);io.balanced();}
 for(auto delay:{10u,500u,1500u,20500u}) {IO io;io.plans={{false,false,false,0,0,delay}};assert(TGLPcode::transfer(io,0x26,0,0).ok());assert(io.clock==delay);}
 for(auto error:{1u,2u,3u,4u,0x7fu,0xffu}) {IO io;io.plans={{false,false,false,error}};auto r=TGLPcode::transfer(io,0x26,0,0);assert(r.status==PS::FirmwareError && (r.mailbox&0xff)==error);io.balanced();}
 {IO io;auto r=TGLPcode::transfer(io,0x80000026,0,0);assert(r.status==PS::Invalid && io.locks==0);}
 {IO io;io.plans={{true},{false,false,false,0,1},{}};auto r=TGLPcode::tcCold(io,true);assert(r.ok() && r.attempts==3 && io.clock==2000);assert(io.history[1].low==0 && io.history[2].command==0x26);io.balanced();}
 {IO io;io.plans.assign(3,{false,false,false,0,1});auto r=TGLPcode::tcCold(io,true);assert(r.status==PS::ExitFailed && r.attempts==3);}
 {IO io;io.plans={{false,false,false,0,1}};assert(TGLPcode::tcCold(io,false).ok());assert(io.history[0].low==1);}
 {IO io;io.plans={{false,false,true},{true},{true}};auto r=TGLPcode::tcCold(io,true);
  assert(r.status==PS::Busy && r.submitted && r.attempts==3); // first attempt did issue a command
  assert(io.history[1].writes==0 && io.history[2].writes==0);
 }
 // Three ports, each with four independent uses; all acquisition/release
 // orders keep the hardware block until the final token has been returned.
 unsigned scenarios=0;
 for(unsigned a=3;a<7;++a)for(unsigned b=a+1;b<8;++b)for(unsigned c=b+1;c<9;++c) {
  unsigned ins[]={a,b,c};do {unsigned outs[]={a,b,c};do {
   IO io;TGLTCCold state;
   for(auto d:ins)for(unsigned u=0;u<4;++u)assert(state.acquire(io,1,d,Use(u)).ok());
   assert(state.total==12 && io.history.size()==1 && state.phase==TGLTCCold::Phase::Blocked);
   for(auto d:outs)for(unsigned u=0;u<4;++u) {
    assert(state.release(io,1,d,Use(u)).ok());
    if(state.total)assert(io.history.size()==1);
   }
   assert(state.total==0 && state.owner==0 && io.history.size()==2 && io.history.back().low==1);
   assert(state.phase==TGLTCCold::Phase::Unblocked);io.balanced();++scenarios;
  }while(std::next_permutation(outs,outs+3));}while(std::next_permutation(ins,ins+3));
 }
 {IO io;TGLTCCold s;assert(s.acquire(io,1,4,Use::Training).ok());
  assert(s.acquire(io,1,4,Use::Scanout).ok());assert(s.release(io,1,4,Use::Training).ok());
  assert(s.total==1 && io.history.size()==1);assert(s.release(io,1,4,Use::Scanout).ok());assert(io.history.size()==2);
 }
 {IO io;TGLTCCold s;assert(s.acquire(io,1,4,Use::AUX).ok());
  assert(s.release(io,2,4,Use::AUX).status==CS::WrongOwner);
  assert(s.release(io,1,5,Use::AUX).status==CS::Unbalanced);
  assert(s.release(io,1,4,Use::Probe).status==CS::Unbalanced);
  assert(s.acquire(io,2,4,Use::AUX).status==CS::WrongOwner);
  for(unsigned d:{0u,2u,9u,~0u})assert(s.acquire(io,1,d,Use::AUX).status==CS::Invalid);
  assert(s.acquire(io,0,4,Use::AUX).status==CS::Invalid);
  assert(s.acquire(io,1,4,Use::Count).status==CS::Invalid);
  assert(s.total==1 && io.history.size()==1);
  s.total=UINT32_MAX;assert(s.acquire(io,1,4,Use::AUX).status==CS::Overflow);s.total=1;
  assert(s.release(io,1,4,Use::AUX).ok());
 }
 {IO io;TGLTCCold s;io.plans.assign(3,{false,false,true});
  assert(s.acquire(io,1,4,Use::Probe).status==CS::TransportFailed);
  assert(s.total==0 && s.phase==TGLTCCold::Phase::Unknown && s.owner==1);
  assert(s.acquire(io,2,4,Use::Probe).status==CS::WrongOwner);
  assert(s.acquire(io,1,4,Use::Probe).ok());assert(s.total==1);assert(s.release(io,1,4,Use::Probe).ok());
 }
 {IO io;TGLTCCold s;assert(s.acquire(io,1,4,Use::Scanout).ok());
  io.plans.assign(3,{false,false,true});io.planIndex=0;
  assert(s.release(io,1,4,Use::Scanout).status==CS::TransportFailed);
  assert(s.total==1 && s.refs[4][3]==1 && s.owner==1 && s.phase==TGLTCCold::Phase::Unknown);
  // A release timeout may have completed late: re-block before another use.
  const auto before=io.history.size();assert(s.acquire(io,1,5,Use::AUX).ok());
  assert(io.history.size()==before+1 && io.history.back().low==0 && s.total==2);
  assert(s.release(io,1,5,Use::AUX).ok());assert(s.release(io,1,4,Use::Scanout).ok());assert(s.total==0);
 }
 {IO io;TGLTCCold s;io.plans.assign(3,{true});
  assert(s.acquire(io,1,4,Use::Probe).status==CS::TransportFailed && s.total==0);
  assert(s.reconcileIdle(io,2).status==CS::WrongOwner);
  io.plans.assign(3,{true});io.planIndex=0;
  assert(s.reconcileIdle(io,1).status==CS::TransportFailed && s.owner==1);
  assert(s.reconcileIdle(io,1).ok() && s.owner==0 && s.phase==TGLTCCold::Phase::Unblocked);
  assert(s.acquire(io,2,4,Use::AUX).ok());assert(s.reconcileIdle(io,2).status==CS::InUse);
  assert(s.release(io,2,4,Use::AUX).ok());
 }
 {IO io;TGLTCCold s;assert(s.acquire(io,1,4,Use::AUX).ok());
  io.plans.assign(3,{true});io.planIndex=0;
  assert(s.release(io,1,4,Use::AUX).status==CS::TransportFailed && s.total==1);
  assert(s.release(io,1,4,Use::AUX).ok() && s.total==0 && s.owner==0);
 }
 // Concurrent callers use the same real core. An independent native-like
 // mailbox client shares the adapter's lock; actual native routing is pending.
 {IO io;TGLTCCold s;std::mutex m;std::condition_variable cv;unsigned acquired=0;bool release=false;
  std::vector<std::thread> threads;
  for(unsigned ddi:{3u,4u,8u})threads.emplace_back([&,ddi]{
   assert(s.acquire(io,1,ddi,Use::AUX).ok());
   {std::unique_lock<std::mutex> lock(m);++acquired;cv.notify_all();cv.wait(lock,[&]{return release;});}
   assert(s.release(io,1,ddi,Use::AUX).ok());
  });
  {std::unique_lock<std::mutex> lock(m);cv.wait(lock,[&]{return acquired==3;});}
  std::thread native([&]{for(unsigned i=0;i<50;++i)assert(TGLPcode::transfer(io,7,3,0).ok());});
  {std::lock_guard<std::mutex> lock(m);release=true;}cv.notify_all();
  for(auto &t:threads)t.join();native.join();
  assert(s.total==0 && s.owner==0);unsigned block=0,unblock=0,normal=0;
  for(auto t:io.history) {if(t.command==7)++normal;else if(t.command==0x26){if(t.low==0)++block;else ++unblock;}}
  assert(block==1 && unblock==1 && normal==50);io.balanced();
 }
 printf("PASS: transport errors/deadlines/retries; %u 3-port 4-use lifetime orders; handoff, failed acquire/release, wrong-owner/scope, overflow; concurrent callers and shared simulated native mailbox\n",scenarios);
}
