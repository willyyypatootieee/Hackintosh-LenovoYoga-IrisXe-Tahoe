#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <thread>
#include <atomic>
#include <sys/mman.h>
#include "kern_tgl_tc_cold.hpp"
#include "kern_tgl_pcode_gate.hpp"
struct IOService {};struct IOMemoryMap {};using IOLock=std::mutex;
thread_local unsigned held;thread_local char threadIdentity;
void IOLockLock(IOLock *p){assert(!held);p->lock();++held;}
void IOLockUnlock(IOLock *p){assert(held==1);--held;p->unlock();}
void *current_thread(){return &threadIdentity;}
void clock_get_uptime(uint64_t *v){*v=0;}void absolutetime_to_nanoseconds(uint64_t v,uint64_t *n){*n=v;}
void IODelay(unsigned){assert(false);}void IOSleep(unsigned){assert(false);}
static TGLPcodeGate tglPcodeGate;
constexpr uint32_t kIOReturnNotReady=0xe00002d8;
#include "adapter.inc"
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
template<class... T> void tglDPCDRecord(T...){}
struct XE {
 bool tglBootstrapAUXReady {};uintptr_t orgTGLReadAUX {},orgTGLWriteAUX {},orgTGLI2CAUX {};
 static uint32_t wrapTGLReadAUX(void*,uint32_t,void*,uint32_t);
 static uint32_t wrapTGLWriteAUX(void*,uint32_t,void*,uint32_t);
 static uint32_t wrapTGLI2CAUX(void*,uint32_t,uint32_t*,uint32_t,uint8_t);
};
XE driver,*callbackXE=&driver;
#include "wrappers.inc"
alignas(8) uint8_t controller[0xe00],ports[6][0x600],manager[0x100];uint32_t config[6][8];
IOLock coldLock;void *noAccess;std::atomic<unsigned> nativeCalls{0};
thread_local bool expectScope=true;thread_local uint32_t nativeResult=0;
unsigned portIndex(void *p){for(unsigned i=0;i<6;++i)if(p==ports[i])return i;assert(false);return 0;}
void nativeCheck(void *p){
 assert(!held);++nativeCalls;auto i=portIndex(p);
 IOLockLock(&coldLock);if(expectScope)assert(tglBootstrapTrial.cold.bootstrapHeld && tglBootstrapTrial.cold.refs[i+3][1]);IOLockUnlock(&coldLock);
}
uint32_t readNative(void *p,uint32_t address,void *data,uint32_t length){nativeCheck(p);assert(address==0x100 && length==4);if(!nativeResult)memset(data,0x5a,4);return nativeResult;}
uint32_t writeNative(void *p,uint32_t address,void *data,uint32_t length){nativeCheck(p);assert(address==0x100 && length==4 && data);return nativeResult;}
uint32_t i2cNative(void *p,uint32_t command,uint32_t *packet,uint32_t length,uint8_t flag){
 nativeCheck(p);assert(command==1 && length==8 && flag==7);
 return XE::wrapTGLWriteAUX(p,0x100,packet,4);
}
void reset(){
 tglBootstrapTrial={};tglBootstrapAUX={};tglPcodeGate={};nativeCalls=0;expectScope=true;nativeResult=0;
 auto &s=tglBootstrapTrial;s.lock=&coldLock;s.controller=reinterpret_cast<IOService *>(controller);s.base=static_cast<volatile uint8_t *>(noAccess);s.stage=2;
 s.cold.owner=reinterpret_cast<uintptr_t>(controller);s.cold.bootstrapHeld=true;s.cold.total=1;s.cold.phase=TGLTCCold::Phase::Blocked;
 *reinterpret_cast<void**>(controller+0xc40)=manager;
 for(unsigned i=0;i<6;++i){config[i][0]=i+3;*reinterpret_cast<void**>(ports[i]+0x548)=config[i];*reinterpret_cast<void**>(ports[i]+0x578)=manager;}
 driver.tglBootstrapAUXReady=true;driver.orgTGLReadAUX=(uintptr_t)readNative;driver.orgTGLWriteAUX=(uintptr_t)writeNative;driver.orgTGLI2CAUX=(uintptr_t)i2cNative;
 tglBootstrapAUX.enabled=1;
}
void run(unsigned kind,unsigned i){uint32_t data[5]{};uint32_t result;
 if(!kind)result=XE::wrapTGLReadAUX(ports[i],0x100,data,4);
 else if(kind==1)result=XE::wrapTGLWriteAUX(ports[i],0x100,data,4);
 else result=XE::wrapTGLI2CAUX(ports[i],1,data,8,7);
 assert(result==nativeResult);if(kind==0 && !nativeResult)assert(data[0]==0x5a5a5a5a);
}
void balanced(){
 assert(tglBootstrapTrial.cold.total==1 && tglBootstrapTrial.cold.bootstrapHeld && tglPcodeGate.owner==0);
 for(auto &p:tglBootstrapTrial.cold.refs)for(auto n:p)assert(!n);
 for(auto &p:tglBootstrapAUX.i2cThreads)for(auto n:p)assert(!n);
 for(unsigned k=0;k<3;++k)assert(!tglBootstrapAUX.acquireErrors[k] && !tglBootstrapAUX.releaseErrors[k]);
}
int main(){
 noAccess=mmap(nullptr,0x140000,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(noAccess!=MAP_FAILED);
 reset();driver.tglBootstrapAUXReady=false;expectScope=false;run(0,0);assert(!tglBootstrapAUX.calls[0]);balanced();
 reset();tglBootstrapAUX.enabled=0;expectScope=false;run(1,0);assert(tglBootstrapAUX.bypass[1]==1);balanced();
 reset();config[0][0]=1;expectScope=false;run(0,0);assert(tglBootstrapAUX.bypass[0]==1);balanced();
 reset();*reinterpret_cast<void**>(ports[0]+0x578)=nullptr;expectScope=false;run(0,0);assert(tglBootstrapAUX.bypass[0]==1);balanced();
 reset();for(unsigned i=0;i<6;++i)for(unsigned k=0;k<3;++k)for(auto error:{0u,0xe00002bcu}){nativeResult=error;run(k,i);balanced();}
 for(unsigned i=0;i<6;++i)assert(tglBootstrapAUX.peak[i]==2 && tglBootstrapAUX.nestedWrites[i]==2);
 reset();std::thread workers[3];
 for(unsigned i=0;i<3;++i)workers[i]=std::thread([i]{for(unsigned n=0;n<50;++n)run(2,i);});
 for(auto &w:workers)w.join();balanced();
 for(unsigned i=0;i<3;++i)assert(tglBootstrapAUX.nestedWrites[i]==50 && tglBootstrapAUX.peak[i]==2);
 assert(nativeCalls==300 && !tglBootstrapAUX.scopeOverflow);
 reset();tglBootstrapTrial.cold.phase=TGLTCCold::Phase::Unknown;uint32_t data=0;
 assert(XE::wrapTGLReadAUX(ports[0],0x100,&data,4)==kIOReturnNotReady && !nativeCalls && tglBootstrapAUX.acquireErrors[0]==1);
 assert(!data && tglBootstrapTrial.cold.total==1);
 assert(munmap(noAccess,0x140000)==0);
 puts("PASS actual bootstrap AUX adapter + read/write/I2C wrappers: 36 calls across 6 ports, preserved native errors/data, same-thread same-port nesting, 3-thread 150 I2C operations, bypass/readiness guards; all AUX refs return to bootstrap-only. MMIO PROT_NONE proves no new PCODE request under immutable bootstrap (ASan/UBSan).");
}
