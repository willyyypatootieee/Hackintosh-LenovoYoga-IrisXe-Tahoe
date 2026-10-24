#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <sys/mman.h>
#include "kern_tgl_pcode_edram.hpp"
using Status=TGLPcodeEDRAM::Status;
TGLPcodeGate tglPcodeGate;
uintptr_t current_thread(){return 71;}
volatile uint8_t *mmio;
unsigned nativeSleeps,waits,forceGets,forcePuts,captures;
uint32_t firmwareStatus;
bool simulateCompletion=true;
uint32_t &reg(unsigned a){return *reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(mmio)+a);}
void IOSleep(unsigned n){assert(n==1);++waits;if(simulateCompletion)reg(0x138124)=firmwareStatus;}
void IODelay(unsigned){assert(false);}
struct XE {
 void *tglAcceleratorOwner {};uint64_t tglAcceleratorBARSize {};
 bool tglPcodeAcceleratorReady {true};
 static void tglEDRAMPCODE(void *);
};
XE object,*callbackXE=&object;
// EDRAM_HELPER
struct IO {
 unsigned sleeps {},reads {},writes {},initialBusy {},completionBusy {};
 bool submitted {},missingBefore {},missingAfter {};
 uint32_t error {};
 uint32_t read(uint32_t r) {
  assert(r==0x138124);++reads;
  if(submitted?missingAfter:missingBefore)return ~0u;
  auto &busy=submitted?completionBusy:initialBusy;
  if(busy){--busy;return 0x80000000;}
  return submitted?error:0;
 }
 void write(uint32_t r,uint32_t v){
  assert((writes==0 && r==0x138128 && v==0xb0000000)||(writes==1 && r==0x138124 && v==0x80591012));
  if(++writes==2)submitted=true;
 }
 void sleepMs(unsigned n){assert(n==1);++sleeps;}
 void delayUs(unsigned){assert(false);}
};
void protocolTests(){
 for(unsigned which=0;which<7;++which){
  TGLPcodeGate gate;IO io;
  if(which==1)io.initialBusy=501;
  if(which==2)io.completionBusy=501;
  if(which==3)io.missingBefore=true;
  if(which==4)io.missingAfter=true;
  if(which==5)io.error=3;
  if(which==6){io.initialBusy=2;io.completionBusy=3;}
  const Status statuses[]={Status::Success,Status::InitialTimeout,Status::CompletionTimeout,Status::Unavailable,Status::Unavailable,Status::FirmwareError,Status::Success};
  auto r=TGLPcodeEDRAM::write(io,gate,71);assert(r.status==statuses[which] && !gate.owner);
  assert(io.writes==((which==1||which==3)?0u:2u));
  assert(r.submitted==(io.writes!=0));
  if(which==1||which==2)assert(io.sleeps==500);
  if(which==6)assert(io.sleeps==5);
 }
 TGLPcodeGate gate;IO io;assert(gate.tryAcquire(99)==TGLPcodeGate::Status::Acquired);
 assert(TGLPcodeEDRAM::write(io,gate,71).status==Status::GateBusy && io.sleeps==500 && !io.reads && !io.writes);
 assert(gate.owner==99 && gate.release(99));assert(gate.tryAcquire(71)==TGLPcodeGate::Status::Acquired);
 io={};assert(TGLPcodeEDRAM::write(io,gate,71).status==Status::GateBusy && !io.sleeps && !io.reads && !io.writes);assert(gate.release(71));
}
alignas(8) uint8_t owner[0x1250],manager[0x30];
void forceWake(void *p,bool enable,unsigned domain){
 assert(p==owner && domain==4 && tglPcodeGate.owner==0);
 if(enable)++forceGets;else ++forcePuts;
}
void originalSleep(unsigned n){
 assert(n==1000 && tglPcodeGate.owner==0);++nativeSleeps;
 // The native unchanged sleep is deliberately not spent holding the gate.
 reg(0x138124)=0;reg(0x145910)=0xb0000000;
}
void capture(uintptr_t rbx,void *r14){assert(rbx==0xb0000000 && r14==manager);++captures;}
__attribute__((noinline)) void callbackWithClobbers(void *p){
 XE::tglEDRAMPCODE(p);
 // Force caller-save registers and flags to differ; the original following
 // IOSleep call makes them dead at the patch boundary.
 asm volatile("mov $0xdead, %%eax; mov %%rax, %%rdi; mov %%rax, %%rsi; mov %%rax, %%rdx; mov %%rax, %%rcx; mov %%rax, %%r8; mov %%rax, %%r9; mov %%rax, %%r10; mov %%rax, %%r11; xor %%eax, %%eax"
  :::"rax","rdi","rsi","rdx","rcx","r8","r9","r10","r11","cc");
}
void jump(uint8_t*p,uintptr_t t){p[0]=0x48;p[1]=0xb8;std::memcpy(p+2,&t,8);p[10]=0xff;p[11]=0xe0;}
int main(int argc,char**argv){
 assert(argc==3);protocolTests();
 std::ifstream f(argv[1],std::ios::binary);std::vector<char> donor((std::istreambuf_iterator<char>(f)),{});
 size_t size=4*1024*1024;assert(!donor.empty() && donor.size()<size);
 auto m=static_cast<uint8_t*>(mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0));
 assert(m!=MAP_FAILED);std::memcpy(m,donor.data(),donor.size());
 mmio=static_cast<uint8_t*>(mmap(nullptr,0x150000,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0));assert(mmio!=MAP_FAILED);
 *reinterpret_cast<void**>(manager+0x10)=owner;
 *reinterpret_cast<volatile uint8_t**>(manager+0x18)=mmio;
 *reinterpret_cast<volatile uint8_t**>(owner+0x1240)=mmio;
 object.tglAcceleratorOwner=owner;object.tglAcceleratorBARSize=0x150000;
 jump(m+0x28c2a,(uintptr_t)forceWake);jump(m+0x300000,(uintptr_t)originalSleep);
 assert(m[0x4bcae]==0xe8);int32_t rel=0x300000 - 0x4bcae - 5;std::memcpy(m+0x4bcaf,&rel,4);
 // ABI-aligned entry to the selected original interval [4BC78,4BCD6).
 // Save its callee-saved RBX/R14 and retain all original instructions inside.
 const uint8_t enter[]={0x55,0x48,0x89,0xe5,0x53,0x41,0x56,0x49,0x89,0xfe};
 std::memcpy(m+0x300100,enter,sizeof(enter));jump(m+0x300100+sizeof(enter),(uintptr_t)(m+0x4bc78));
 const uint8_t leave[]={0x48,0x89,0xdf,0x4c,0x89,0xf6,0x48,0xb8};
 std::memcpy(m+0x300200,leave,sizeof(leave));uintptr_t cap=(uintptr_t)capture;std::memcpy(m+0x300208,&cap,8);
 const uint8_t tail[]={0xff,0xd0,0x41,0x5e,0x5b,0x5d,0xc3};std::memcpy(m+0x300210,tail,sizeof(tail));jump(m+0x4bcd6,(uintptr_t)(m+0x300200));
 auto execute=(void(*)(void*))(m+0x300100);
 reg(0x13812c)=0xabcdef01;execute(manager);
 assert(forceGets==1 && forcePuts==1 && nativeSleeps==1 && captures==1 && !tglPcodeEDRAMCalls);
 uint8_t patched[TGLPcodeEDRAMPatch::Size];assert(!TGLPcodeEDRAMPatch::make(0,patched));
 assert(TGLPcodeEDRAMPatch::make((uintptr_t)callbackWithClobbers,patched));
 std::memcpy(m+0x4bc90,patched,sizeof(patched));
 // Audit copy includes only the intended 25-byte replacement, not replay stubs.
 std::memcpy(donor.data()+0x4bc90,patched,sizeof(patched));
 std::ofstream out(argv[2],std::ios::binary);out.write(donor.data(),donor.size());out.close();
 execute(manager);
 assert(forceGets==2 && forcePuts==2 && nativeSleeps==2 && captures==2);
 assert(tglPcodeEDRAMCalls==1 && tglPcodeEDRAMResults[0]==1 && !tglPcodeEDRAMInvalid && waits==1);
 assert(reg(0x138128)==0xb0000000 && reg(0x13812c)==0xabcdef01 && !tglPcodeGate.owner);
 assert(object.tglPcodeAcceleratorReady);
 // A rejected owner must not touch MMIO; use an inaccessible aperture.
 auto inaccessible=mmap(nullptr,4096,PROT_NONE,MAP_ANON|MAP_PRIVATE,-1,0);assert(inaccessible!=MAP_FAILED);
 *reinterpret_cast<void**>(manager+0x18)=inaccessible;object.tglAcceleratorOwner=nullptr;
 XE::tglEDRAMPCODE(manager);assert(tglPcodeEDRAMInvalid==1 && !object.tglPcodeAcceleratorReady);
 munmap(inaccessible,4096);munmap(const_cast<uint8_t*>(mmio),0x150000);munmap(m,size);
 puts("PASS: native interval and 25-byte callback patch preserve RBX/R14/continuation, forcewake and original 1000ms sleep outside gate; DATA1 unchanged; real helper owner guard; busy/reentry/timeouts/all-ones/status errors without unprotected stores");
 puts("CPU replay only, no GPU/eDRAM hardware request. Audit copy is non-deployable (process-local callback address).");
}
