#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include "kern_tgl_port_use.hpp"
#include <vector>
#include <sys/mman.h>
#include "kern_tgl_tc_cold.hpp"
using Use = TGLTCCold::Use;
TGLTCCold state;
TGLPortUse gate;TGLPortUse::Token cleanupToken;
bool useGate,stopAtNested;unsigned stopRefusals;
unsigned powerRefs, dcRefs, nativeCalls, unsafePower, sleeps, delays;
uint32_t auxResult, ddi;
std::vector<unsigned> events;
struct IO {
 bool use {},cold {}, pcode {}, submitted {}, failBlock {}, failUnblock {};
 void useLock(){assert(!use);use=true;}void useUnlock(){assert(use);use=false;}
 uint32_t low {}; uint64_t time {};
 void coldLock() { assert(!cold && !use);cold=true; }
 void coldUnlock() { assert(cold);cold=false; }
 bool pcodeTryLock() { assert(!pcode);pcode=true;submitted=false;return true; }
 void pcodeUnlock() { assert(pcode);pcode=false; }
 uint32_t read(uint32_t reg) {
  assert(pcode);
  if(reg==0x138124) return submitted && ((low==0 && failBlock)||(low==1 && failUnblock)) ? 1:0;
  assert(reg==0x138128 || reg==0x13812c);return 0;
 }
 void write(uint32_t reg,uint32_t value) {
  assert(pcode);
  if(reg==0x138128)low=value;
  else if(reg==0x13812c)assert(value==0);
  else { assert(reg==0x138124 && value==0x80000026);submitted=true;events.push_back(low?8:1); }
 }
 uint64_t nowUs() { return time; }
 void delayUs(unsigned n) { time+=n; }
 void sleepMs(unsigned n) { time+=1000*n; }
} io;
void dcOff(void*) { assert(!io.use);events.push_back(2);++dcRefs; }
void dcOn(void*) { assert(dcRefs);events.push_back(7);--dcRefs; }
void powerGet(void*,uint32_t d) {
 assert(d==ddi);events.push_back(3);++powerRefs;
 if(state.phase!=TGLTCCold::Phase::Blocked || !state.refs[d][1])++unsafePower;
}
void powerPut(void*,uint32_t d) { assert(d==ddi && powerRefs);events.push_back(6);--powerRefs; }
uint32_t lowAUX(void*,uint32_t command,uint32_t *packet,uint32_t length,bool) {
 assert(dcRefs && powerRefs && length<=20);events.push_back(4);++nativeCalls;
 // Preserve request payload on write; supply a deterministic read response.
 if(command==0 && auxResult==0) std::memset(reinterpret_cast<uint8_t*>(packet)+1,0x5a,16);
 return auxResult;
}
void sleepMock(unsigned n) { assert(n);++sleeps; }
void delayMock(unsigned n) { assert(n);++delays; }
void *copyMock(void *d,const void *s,size_t n) { return std::memcpy(d,s,n); }
void *checkedCopy(void *d,const void *s,size_t n,size_t cap) { assert(n<=cap);return std::memcpy(d,s,n); }
void jump(uint8_t *p,uintptr_t target) { p[0]=0x48;p[1]=0xb8;std::memcpy(p+2,&target,8);p[10]=0xff;p[11]=0xe0; }
void callTo(uint8_t *p,unsigned site,unsigned stub) { assert(p[site]==0xe8);int32_t rel=stub-site-5;std::memcpy(p+site+1,&rel,4); }
alignas(8) uint8_t controller[0x1c00], port[0x600], power[0x100];
uintptr_t vtable[64];uint32_t config[8];uint64_t canary=0x829347ab;
using AuxFn=uint32_t(*)(void*,uint32_t,void*,uint32_t);
using I2CFn=uint32_t(*)(void*,uint32_t,uint32_t*,uint32_t,uint8_t);
AuxFn readAUX,writeAUX;I2CFn i2cAUX;
uint32_t operation(unsigned kind,uint8_t *data) {
 if(kind==0)return readAUX(port,0x100,data,4);
 if(kind==1)return writeAUX(port,0x100,data,4);
 uint32_t packet[5]{};return i2cAUX(port,1,packet,8,0);
}
TGLTCCold::AUXCall scoped(unsigned kind,uint8_t *data) {
 return state.invokeAUX(io,1,ddi,[&] { return operation(kind,data); });
}
// The actual upper native call remains outside the short admission lock.
// Failed cold cleanup converts the existing admission token to a durable
// reservation, rather than dropping evidence that destruction must wait.
template<class Operation> TGLTCCold::AUXCall admitted(Operation operation) {
 auto r=gate.invokeAUX(io,state,1,reinterpret_cast<uintptr_t>(port),ddi,10,operation);
 assert(r.admission.ok() && r.retirement==TGLPortUse::Status::Success);
 if(r.retained.valid()){assert(!cleanupToken.valid());cleanupToken=r.retained;}
 return r.call;
}
unsigned nestedCalls,maxRefs;
uint32_t nestedWrite(void *p,uint32_t address,void *data,uint32_t length) {
 ++nestedCalls;
 if(useGate && stopAtNested){
  assert(gate.stop(io,1)==TGLPortUse::Status::Success);
  assert(gate.enter(io,1,reinterpret_cast<uintptr_t>(port),ddi,11).status==TGLPortUse::Status::Closed);
  assert(gate.claim(io,1,reinterpret_cast<uintptr_t>(port),ddi,20).status==TGLPortUse::Status::Busy);
  auto denied=gate.invokeAUX(io,state,1,reinterpret_cast<uintptr_t>(port),ddi,11,[]()->uint32_t{assert(false);return 0;});
  assert(denied.admission.status==TGLPortUse::Status::Closed && !denied.call.executed && !denied.retained.valid());
  ++stopRefusals;
 }
 auto native=[&] {if(state.total>maxRefs)maxRefs=state.total;return writeAUX(p,address,data,length);};
 auto r=useGate?admitted(native):state.invokeAUX(io,1,ddi,native);
 assert(r.executed && r.acquire.ok() && r.release.ok() && !r.retainedReference());
 return r.operationResult;
}
void reset(unsigned d,uint32_t result) {
 state={};io={};powerRefs=dcRefs=nativeCalls=unsafePower=sleeps=delays=0;
 events.clear();ddi=d;config[0]=d;auxResult=result;port[0x18]=0;
 gate.~TGLPortUse();new(&gate) TGLPortUse;cleanupToken={};
 useGate=stopAtNested=false;stopRefusals=nestedCalls=maxRefs=0;
 assert(gate.bind(io,1,reinterpret_cast<uintptr_t>(port),d)==TGLPortUse::Status::Success);
}
int main(int argc,char **argv) {
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);
 std::vector<char> donor((std::istreambuf_iterator<char>(f)),{});
 const size_t size=4*1024*1024;assert(!donor.empty() && donor.size()<size);
 auto m=static_cast<uint8_t*>(mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0));
 assert(m!=MAP_FAILED);std::memcpy(m,donor.data(),donor.size());
 jump(m+0x48054,(uintptr_t)dcOff);jump(m+0x47d02,(uintptr_t)dcOn);
 jump(m+0x5c4ba,(uintptr_t)powerGet);jump(m+0x5ae0a,(uintptr_t)powerPut);
 jump(m+0x300000,(uintptr_t)sleepMock);jump(m+0x300020,(uintptr_t)delayMock);
 jump(m+0x300040,(uintptr_t)copyMock);jump(m+0x300060,(uintptr_t)checkedCopy);
 for(auto site:{0x1cdf6u,0x1d0f7u,0x1d46du,0x1d52au})callTo(m,site,0x300000);
 callTo(m,0x1d3b2,0x300020);callTo(m,0x1ce6c,0x300040);callTo(m,0x1d0af,0x300060);
 *reinterpret_cast<void**>(m+0x138ba8)=controller;
 *reinterpret_cast<void**>(m+0xc80a8)=&canary;
 *reinterpret_cast<void**>(controller+0xc8)=power;
 *reinterpret_cast<void**>(port+0x548)=config;
 vtable[0x170/8]=(uintptr_t)lowAUX;*reinterpret_cast<void**>(port)=vtable;
 readAUX=(AuxFn)(m+0x1cc72);writeAUX=(AuxFn)(m+0x1ceb0);i2cAUX=(I2CFn)(m+0x1d2b6);
 unsigned cases=0;
 for(unsigned d=3;d<9;++d)for(unsigned k=0;k<3;++k) {
  // The unprotected donor reaches AUX power before the low-level function.
  uint8_t output[8];std::memset(output,0xa5,sizeof(output));reset(d,0);
  assert(operation(k,output)==0 && unsafePower==1 && powerRefs==0 && dcRefs==0);
  for(uint32_t result:{0u,0xe00002bcu,0xe00002d8u,0xe00002d6u}) {
   reset(d,result);std::memset(output,0xa5,sizeof(output));auto r=scoped(k,output);
   assert(r.executed && r.acquire.ok() && r.release.ok() && r.operationResult==result);
   assert(!state.total && !powerRefs && !dcRefs && unsafePower==0);
   assert(events.front()==1 && events[1]==2 && events[2]==3 && events[events.size()-2]==7 && events.back()==8);
   assert(nativeCalls==(result?7u:1u));
   if(k==0) for(unsigned i=0;i<4;++i) assert(output[i]==(result?0xa5:0x5a));
   for(unsigned i=4;i<8;++i)assert(output[i]==0xa5);
   ++cases;
  }
  reset(d,0);io.failBlock=true;auto failed=scoped(k,output);
  assert(!failed.executed && !failed.acquire.ok() && nativeCalls==0 && !powerRefs && !dcRefs && state.total==0 && failed.operationResult==0xFFFFFFFFU);
  io.failBlock=false;assert(state.reconcileIdle(io,1).ok());++cases;
  reset(d,0);io.failUnblock=true;auto cleanup=scoped(k,output);
  assert(cleanup.executed && cleanup.operationResult==0 && cleanup.retainedReference());
  assert(state.total==1 && state.refs[d][1]==1 && state.phase==TGLTCCold::Phase::Unknown);
  assert(!powerRefs && !dcRefs);io.failUnblock=false;
  assert(state.release(io,1,d,Use::AUX).ok() && !state.total);++cases;
 }
 // Actual runI2COverAUX flag bit0 executes writeAUX recursively. Keep the
 // outer lease across both native power scopes and the low-level command.
 // Route the nested native write entry through the same real core helper.
 // Relocate only its 13-byte, RIP-independent prologue into a trampoline.
 const uint8_t prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53};
 assert(!std::memcmp(m+0x1ceb0,prologue,sizeof(prologue)));
 std::memcpy(m+0x300100,prologue,sizeof(prologue));
 jump(m+0x300100+sizeof(prologue),(uintptr_t)(m+0x1cebd));
 writeAUX=(AuxFn)(m+0x300100);jump(m+0x1ceb0,(uintptr_t)nestedWrite);
 reset(4,0);port[0x18]=1;uint8_t output[8]{};auto nested=scoped(2,output);
 assert(nested.executed && nested.operationResult==0 && nested.release.ok());
 assert(nestedCalls==1 && maxRefs==2 && nativeCalls==2 && unsafePower==0 && !powerRefs && !dcRefs && !state.total);
 // Stop arrives after I2C has begun but before its recursive native write.
 // The admitted ancestor lets that same-port descendant finish. New threads
 // are denied and destructive drain cannot claim ownership until both return.
 unsigned stopped=0;
 for(unsigned d=3;d<9;++d)for(uint32_t result:{0u,0xe00002bcu}) {
  reset(d,result);port[0x18]=1;useGate=stopAtNested=true;
  auto r=admitted([&]{return operation(2,output);});
  assert(r.executed && r.operationResult==result && r.acquire.ok() && r.release.ok());
  assert(stopRefusals && nestedCalls==stopRefusals && maxRefs==2);
  assert(!state.total && !powerRefs && !dcRefs && unsafePower==0);
  auto drain=gate.claim(io,1,reinterpret_cast<uintptr_t>(port),ddi,20);assert(drain.ok());
  assert(gate.finish(io,1,20,drain.token,false)==TGLPortUse::Status::Success);
  assert(gate.emptyClosed(io,1,reinterpret_cast<uintptr_t>(port),ddi));++stopped;
 }
 // Transfer completed, final mailbox release failed. The native result is
 // preserved and a resident reservation blocks free until cleanup succeeds.
 reset(4,0);io.failUnblock=true;
 auto failed=admitted([&]{return operation(0,output);});
 assert(failed.executed && !failed.operationResult && failed.retainedReference() && cleanupToken.valid());
 assert(gate.stop(io,1)==TGLPortUse::Status::Success);
 auto drain=gate.claim(io,1,reinterpret_cast<uintptr_t>(port),ddi,20);assert(drain.ok());
 assert(gate.finish(io,1,20,drain.token,false)==TGLPortUse::Status::Busy);
 io.failUnblock=false;assert(state.release(io,1,ddi,Use::AUX).ok());
 assert(gate.drop(io,1,20,drain.token,cleanupToken)==TGLPortUse::Status::Success);
 assert(gate.finish(io,1,20,drain.token,false)==TGLPortUse::Status::Success);
 assert(!powerRefs && !dcRefs && !state.total);
 // Initial block can fail without producing a cold reference. Retain a
 // gate reservation for uncertain hardware state, then reconcile while idle.
 reset(4,0);io.failBlock=true;
 auto initial=admitted([&]{return operation(0,output);});
 assert(!initial.executed && !state.total && cleanupToken.valid() && !nativeCalls);
 assert(gate.stop(io,1)==TGLPortUse::Status::Success);
 drain=gate.claim(io,1,reinterpret_cast<uintptr_t>(port),ddi,20);assert(drain.ok());
 assert(gate.finish(io,1,20,drain.token,false)==TGLPortUse::Status::Busy);
 io.failBlock=false;assert(state.reconcileIdle(io,1).ok());
 assert(gate.drop(io,1,20,drain.token,cleanupToken)==TGLPortUse::Status::Success);
 assert(gate.finish(io,1,20,drain.token,false)==TGLPortUse::Status::Success);
 std::printf("PASS: %u real nested I2C stop races across TC1..6; new caller refused, in-flight descendant completed, cleanup failure pins port until explicit retry\n",stopped);
 munmap(m,size);
 std::printf("PASS: %u scoped cases over 6 TC DDIs x 3 real donor functions; native success/error/retry, early block failure, retained cleanup failure and nested I2C write\n",cases);
 puts("Unprotected donor control observed AUX power before low-level invocation. CPU replay only; MMIO/power/sleep dependencies are mocked, no GPU or native mailbox concurrency qualification.");
}
