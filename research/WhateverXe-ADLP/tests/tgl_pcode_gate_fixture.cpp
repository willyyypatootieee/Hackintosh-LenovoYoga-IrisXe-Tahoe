#include <cassert>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>
#include "kern_tgl_pcode_gate.hpp"
#include "kern_tgl_tc_cold.hpp"
struct XE {
 uintptr_t orgTGLMailboxRead {},orgTGLMailboxReadData {},orgTGLMailboxWrite {};
 static uint32_t wrapTGLMailboxRead(void*,uint32_t);
 static uint32_t wrapTGLMailboxReadData(void*,uint32_t,uint32_t);
 static void wrapTGLMailboxWrite(void*,uint32_t,uint32_t,bool);
};
XE object,*callbackXE=&object;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
thread_local uintptr_t token=1;
uintptr_t current_thread() { return token; }
unsigned delays,sleeps,nativeCalls,releaseAt;
void IODelay(unsigned);void IOSleep(unsigned);
// NATIVE_WRAPPERS
void wait(bool micro,unsigned n) {
 assert(n==1);
 auto count=micro?++delays:++sleeps;
 if(releaseAt && count==releaseAt)assert(tglPcodeGate.release(99));
}
void IODelay(unsigned n) { wait(true,n); }
void IOSleep(unsigned n) { wait(false,n); }
bool recurse,expectedMicro;
void *controller=reinterpret_cast<void*>(uintptr_t(0x1000));
void nativeEnter(void *p) {
 assert(p==controller);++nativeCalls;
 assert(__atomic_load_n(&tglPcodeGate.owner,__ATOMIC_ACQUIRE)==token);
 if(recurse) {
  recurse=false;auto before=nativeCalls;
  assert(XE::wrapTGLMailboxRead(p,7)==0 && nativeCalls==before);
 }
}
uint32_t nativeRead(void *p,uint32_t cmd) {nativeEnter(p);assert(cmd==7);return 0x12345678;}
uint32_t nativeReadData(void *p,uint32_t cmd,uint32_t data) {nativeEnter(p);assert(cmd==6 && data==1);return 0xaabbccdd;}
void nativeWrite(void *p,uint32_t cmd,uint32_t data,bool micro) {nativeEnter(p);assert(cmd==0x22 && data==1 && micro==expectedMicro);}
void resetCounts() {delays=sleeps=nativeCalls=releaseAt=0;recurse=false;}
struct ColdIO {
 unsigned accesses {};uint64_t clock {};
 bool pcodeTryLock() {return tglPcodeGate.tryAcquire(token)==TGLPcodeGate::Status::Acquired;}
 void pcodeUnlock() {assert(tglPcodeGate.release(token));}
 uint32_t read(uint32_t) {assert(__atomic_load_n(&tglPcodeGate.owner,__ATOMIC_ACQUIRE)==token);++accesses;return 0;}
 void write(uint32_t,uint32_t) {assert(__atomic_load_n(&tglPcodeGate.owner,__ATOMIC_ACQUIRE)==token);++accesses;}
 uint64_t nowUs() {return clock;}void delayUs(unsigned n) {clock+=n;}void sleepMs(unsigned n) {clock+=n*1000;}
};
int main() {
 object.orgTGLMailboxRead=(uintptr_t)nativeRead;object.orgTGLMailboxReadData=(uintptr_t)nativeReadData;
 object.orgTGLMailboxWrite=(uintptr_t)nativeWrite;
 resetCounts();
 assert(XE::wrapTGLMailboxRead(controller,7)==0x12345678);
 assert(XE::wrapTGLMailboxReadData(controller,6,1)==0xaabbccdd);
 for(bool m:{false,true}){expectedMicro=m;XE::wrapTGLMailboxWrite(controller,0x22,1,m);}
 assert(nativeCalls==4 && !sleeps && !delays && tglPcodeGate.owner==0);
 assert(tglPcodeGate.tryAcquire(0)==TGLPcodeGate::Status::Invalid);
 assert(tglPcodeGate.tryAcquire(99)==TGLPcodeGate::Status::Acquired);
 assert(!tglPcodeGate.release(0) && !tglPcodeGate.release(token));
 resetCounts();assert(XE::wrapTGLMailboxRead(controller,7)==0);assert(sleeps==500 && !delays && !nativeCalls);
 resetCounts();assert(XE::wrapTGLMailboxReadData(controller,6,1)==0);assert(sleeps==500 && !delays && !nativeCalls);
 resetCounts();XE::wrapTGLMailboxWrite(controller,0x22,1,false);assert(sleeps==500 && !delays && !nativeCalls);
 resetCounts();XE::wrapTGLMailboxWrite(controller,0x22,1,true);assert(delays==500 && !sleeps && !nativeCalls);
 {ColdIO io;auto r=TGLPcode::transfer(io,0x26,0,0);assert(r.status==TGLPcode::Status::Busy && !r.submitted && io.accesses==0);}
 assert(tglPcodeGate.owner==99 && tglPcodeGate.release(99));
 resetCounts();recurse=true;assert(XE::wrapTGLMailboxRead(controller,7)==0x12345678);
 assert(nativeCalls==1 && !sleeps && !delays && !tglPcodeGate.owner);
 assert(tglPcodeGateFailures[0]==2 && tglPcodeGateFailures[1]==1 && tglPcodeGateFailures[2]==2);
 for(bool m:{false,true}) {
  resetCounts();releaseAt=3;assert(tglPcodeGate.tryAcquire(99)==TGLPcodeGate::Status::Acquired);
  expectedMicro=m;XE::wrapTGLMailboxWrite(controller,0x22,1,m);
  assert(nativeCalls==1 && (m?delays:sleeps)==3 && (m?sleeps:delays)==0 && !tglPcodeGate.owner);
 }
 {ColdIO io;auto r=TGLPcode::transfer(io,0x26,0,0);assert(r.ok() && r.submitted && io.accesses==7 && !tglPcodeGate.owner);}
 // Real atomic gate, four simultaneous owners. Unlike the deterministic
 // protocol fixture, contention here is genuinely nonblocking.
 TGLPcodeGate gate;unsigned protectedCount=0;std::vector<std::thread> threads;
 for(unsigned t=0;t<4;++t)threads.emplace_back([&,t] {
  auto who=uintptr_t(t+1);
  for(unsigned i=0;i<1000;++i) {
   while(gate.tryAcquire(who)!=TGLPcodeGate::Status::Acquired)std::this_thread::yield();
   auto before=protectedCount;std::this_thread::yield();protectedCount=before+1;
   assert(gate.release(who));
  }
 });
 for(auto &t:threads)t.join();assert(protectedCount==4000 && !gate.owner);
 puts("PASS: actual native wrappers preserve args/returns; 500x1us/1ms bounded contention, no unprotected fallback, same-thread recursion rejection, foreign release rejection, TC-cold Busy with zero MMIO, delayed acquisition and 4-thread atomic ownership");
}
