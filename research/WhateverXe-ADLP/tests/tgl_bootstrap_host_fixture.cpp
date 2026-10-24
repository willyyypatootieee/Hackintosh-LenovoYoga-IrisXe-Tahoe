#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "kern_tgl_tc_cold.hpp"
#include "kern_tgl_pcode_gate.hpp"
struct OSObject {uint32_t kind=0,refs=1;void retain(){++refs;}};
struct IOService:OSObject {alignas(8) uint8_t storage[0xD00] {};};
struct IOMemoryMap:OSObject {
 uintptr_t address {};uint64_t length {};
 uint64_t getLength(){return length;}uintptr_t getVirtualAddress(){return address;}
};
#define OSDynamicCast(T,p) ((p) && (p)->kind==1 ? static_cast<T *>(p) : nullptr)
struct IOLock {bool held=false;};
static unsigned allocations;static bool allocFail;
static IOLock *IOLockAlloc(){if(allocFail)return nullptr;++allocations;return new IOLock;}
static void IOLockLock(IOLock *p){assert(!p->held);p->held=true;}
static void IOLockUnlock(IOLock *p){assert(p->held);p->held=false;}
static TGLPcodeGate tglPcodeGate;
static uint64_t timeUs;static uint32_t *mmio;static unsigned commands;static int response;
static void clock_get_uptime(uint64_t *t){*t=timeUs*1000;}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t *n){*n=t;}
static void *current_thread(){return reinterpret_cast<void *>(0x1234);}
static void tick(unsigned n){
 timeUs+=n;
 if(response>=0 && (mmio[0x138124/4]&0x80000000)){
  assert(tglPcodeGate.owner==0x1234);assert(mmio[0x138124/4]==0x80000026);
  assert(mmio[0x138128/4]==0 && mmio[0x13812C/4]==0);++commands;
  mmio[0x138124/4]=response==1?1:0;
  mmio[0x138128/4]=response==2?1:0;
 }
}
static void IODelay(unsigned n){tick(n);}static void IOSleep(unsigned n){tick(n*1000);}
#include "adapter.inc"
struct Fixture {
 IOService c;IOMemoryMap map;alignas(8) uint8_t manager[0x80] {};
 std::vector<uint32_t> memory=std::vector<uint32_t>(0x140000/4);
 Fixture(){
  tglBootstrapTrial={};tglPcodeGate={};allocations=commands=0;allocFail=false;
  timeUs=100;response=0;mmio=memory.data();map.kind=1;
  map.address=reinterpret_cast<uintptr_t>(mmio);map.length=memory.size()*4;
  *reinterpret_cast<uint8_t **>(reinterpret_cast<uint8_t *>(&c)+0xC40)=manager;
  *reinterpret_cast<OSObject **>(reinterpret_cast<uint8_t *>(&c)+0xC70)=&map;
  *reinterpret_cast<uint32_t **>(manager+0x50)=mmio;
  *reinterpret_cast<uint32_t *>(manager+0x60)=map.length;
 }
 ~Fixture(){delete tglBootstrapTrial.lock;}
 void run(bool ready=true,void *controller=nullptr){tglBootstrapTrialRun(controller?controller:&c,ready);assert(tglBootstrapTrial.stage==2);}
 void rejected(unsigned reason){run();assert(tglBootstrapTrial.record[1]==reason);assert(!commands && !allocations && c.refs==1 && map.refs==1);}
 void held(bool success){
  auto &s=tglBootstrapTrial;assert(allocations==1 && c.refs==2 && map.refs==2);
  assert(s.cold.total==1 && s.cold.bootstrapHeld && s.cold.owner==reinterpret_cast<uintptr_t>(&c));
  assert(s.record[1]==0 && s.record[10]==1 && s.record[12]==1 && s.record[13]==1);
  assert(s.record[4]==(success?0:6));assert(s.cold.phase==(success?TGLTCCold::Phase::Blocked:TGLTCCold::Phase::Unknown));
  uint64_t before[16];memcpy(before,s.record,sizeof(before));unsigned count=commands;
  run();assert(commands==count && !memcmp(before,s.record,sizeof(before)) && c.refs==2 && map.refs==2);
 }
};
int main(){
 {Fixture f;f.run(false);assert(tglBootstrapTrial.record[1]==10 && !allocations);}
 {Fixture f;tglBootstrapTrialRun(nullptr,true);assert(tglBootstrapTrial.stage==2 && tglBootstrapTrial.record[1]==10);}
 {Fixture f;reinterpret_cast<uint8_t *>(&f.c)[0x918]=1;f.rejected(11);}
 {Fixture f;*reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(&f.c)+0xC40)=nullptr;f.rejected(11);}
 {Fixture f;*reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(&f.c)+0xC70)=nullptr;f.rejected(11);}
 {Fixture f;f.map.kind=2;f.rejected(11);}
 {Fixture f;*reinterpret_cast<void **>(f.manager+0x50)=nullptr;f.rejected(12);}
 {Fixture f;*reinterpret_cast<uint32_t *>(f.manager+0x60)=0x13812F;f.rejected(12);}
 {Fixture f;f.map.length=0x13812F;f.rejected(12);}
 {Fixture f;f.map.address=1;f.rejected(12);}
 {Fixture f;allocFail=true;f.rejected(13);}
 {Fixture f;f.run();f.held(true);assert(commands==1 && tglPcodeGate.owner==0 && tglBootstrapTrial.record[9]==1);}
 {Fixture f;tglPcodeGate.owner=0x5678;f.run();f.held(false);assert(!commands && tglPcodeGate.owner==0x5678 && tglBootstrapTrial.record[5]==1);}
 {Fixture f;response=-1;f.run();f.held(false);assert(!commands && tglPcodeGate.owner==0 && tglBootstrapTrial.record[5]==1); /* timeout then busy retries */}
 {Fixture f;response=1;f.run();f.held(false);assert(commands==3 && tglBootstrapTrial.record[5]==3);}
 {Fixture f;response=2;f.run();f.held(false);assert(commands==3 && tglBootstrapTrial.record[5]==5);}
 {Fixture f;mmio[0x138124/4]=0xFFFFFFFF;f.run();f.held(false);assert(!commands && tglBootstrapTrial.record[5]==4);}
 puts("PASS actual opt-in bootstrap adapter: 11 admission guards, block/retain once, shared-gate contention, timeout, firmware error, exit failure, inaccessible MMIO; no automatic unblock");
}
