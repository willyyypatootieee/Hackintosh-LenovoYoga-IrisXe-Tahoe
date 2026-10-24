#include <cassert>
#include <initializer_list>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include "kern_tgl_tc_boot.hpp"
#include "kern_tgl_port_map.hpp"
struct IOService;
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 template<class... T>void setProperty(T...){}
};
bool quiet;bool tglQuietTraining(){return quiet;}
void IODelay(unsigned);
struct XE {
 // FIELDS
 bool tglPhyClockPreserveReady=true,tglDekelReady=true;
 uintptr_t orgTGLSignalLevels,orgTGLPhyClockGating,orgTGLSetDklDPMode,orgTGLPortWriteReg,orgTGLEnablePort;
 static uint32_t wrapTGLSignalLevels(void*,uint32_t,uint32_t,void*);
 static void wrapTGLPhyClockGating(void*,bool);
 static void wrapTGLSetDklDPMode(void*);
 static void wrapTGLPortWriteReg(void*,uint32_t,uint32_t);
 static uint32_t wrapTGLEnablePort(void*);
 static bool tglDekelAccess(void*,uint32_t,bool,uint32_t&);
 static void tglTrace(unsigned,unsigned,bool,unsigned,unsigned,unsigned,unsigned){}
 static void tglSnapshotTransmitter(void*,bool,uint32_t){}
};
XE object,*callbackXE=&object;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
// FUNCTIONS
constexpr size_t span=0x100000;
uint8_t *mmio;unsigned ddi,accesses,reads,writes,clockCalls,modeCalls,signalCalls,writeCalls,enableCalls,delays,readyAfter;
bool readable;uint32_t nativeResult,signal[3],seenReg,seenValue,regs[9][2];void *seenOverride;
alignas(8)uint8_t port[0x600],manager[0x80];uint32_t config[8];
template<class T>void field(uint8_t*p,unsigned off,T v){*reinterpret_cast<T*>(p+off)=v;}
uint32_t &reg(unsigned off){return *reinterpret_cast<uint32_t*>(mmio+off);}
uint32_t buffer(){return 0x64000+0x100*ddi;}
bool XE::tglDekelAccess(void*p,uint32_t i,bool wr,uint32_t&v){
 assert(p==port&&ddi>=3&&ddi<=8&&(i==0x48||i==0x49));++accesses;if(!readable)return false;
 if(wr){regs[ddi][i-0x48]=v;++writes;}else{v=regs[ddi][i-0x48];++reads;}return true;
}
uint32_t signalNative(void*p,uint32_t s,uint32_t e,void*o){assert(p==port&&s==1&&e==1);++signalCalls;seenOverride=o;if(o)memcpy(signal,o,sizeof(signal));return 0x1234;}
void clockNative(void*p,bool){assert(p==port);++clockCalls;for(auto &v:regs[ddi])v&=~0x3E;}
void modeNative(void*p){assert(p==port&&accesses==0);++modeCalls;}
void writeNative(void*p,uint32_t r,uint32_t v){assert(p==port);++writeCalls;seenReg=r;seenValue=v;}
uint32_t enableNative(void*p){assert(p==port&&delays==0);++enableCalls;return nativeResult;}
void IODelay(unsigned us){assert(us==100);++delays;if(readyAfter&&delays==readyAfter)reg(buffer())=0x80000000;}
void reset(unsigned d){
 assert(!mprotect(mmio,span,PROT_READ|PROT_WRITE));memset(mmio,0,span);memset(port,0,sizeof(port));memset(manager,0,sizeof(manager));memset(config,0,sizeof(config));
 ddi=d;object=XE{};object.tglAUXParentMask=object.tglTCWiring.legacy=0x1F8;
 object.orgTGLSignalLevels=(uintptr_t)signalNative;object.orgTGLPhyClockGating=(uintptr_t)clockNative;
 object.orgTGLSetDklDPMode=(uintptr_t)modeNative;object.orgTGLPortWriteReg=(uintptr_t)writeNative;object.orgTGLEnablePort=(uintptr_t)enableNative;
 config[0]=d;config[2]=config[3]=1;field(port,0x548,config);field(port,0x550,4u);field(port,0x578,manager);field(port,0x590,buffer());
 field(port,0x124,4u);field(port,0x128,20u);field(manager,0x50,mmio);field(manager,0x60,uint32_t(span));
 for(unsigned i=0;i<9;++i){regs[i][0]=0xAABB883E;regs[i][1]=0xCCDD993C;}
 for(unsigned i=3;i<=8;++i)reg(0x64000+0x100*i)=0xDEADBEEF;
 accesses=reads=writes=clockCalls=modeCalls=signalCalls=writeCalls=enableCalls=delays=readyAfter=0;
 readable=true;seenOverride=nullptr;nativeResult=0xE00002D6;
}
void neighbors(){for(unsigned i=3;i<=8;++i)if(i!=ddi){assert(regs[i][0]==0xAABB883E&&regs[i][1]==0xCCDD993C);assert(reg(0x64000+0x100*i)==0xDEADBEEF);}}
int main(){
 mmio=static_cast<uint8_t*>(mmap(nullptr,span,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0));assert(mmio!=MAP_FAILED);
 for(bool q:{false,true})for(unsigned d=3;d<=8;++d){
  quiet=q;reset(d);
  assert(XE::wrapTGLSignalLevels(port,1,1,nullptr)==0x1234&&signalCalls==1&&seenOverride&&signal[0]==2&&signal[1]==0&&signal[2]==8);
  XE::wrapTGLPhyClockGating(port,false);assert(clockCalls==0&&writes==0&&regs[d][0]==0xAABB883E&&regs[d][1]==0xCCDD993C);neighbors();
  XE::wrapTGLPhyClockGating(port,true);assert(clockCalls==1&&regs[d][0]==(0xAABB883E&~0x3Eu));neighbors();
  for(unsigned lanes:{1u,2u,4u}){reset(d);field(port,0x124,lanes);XE::wrapTGLSetDklDPMode(port);
   assert(modeCalls==1&&regs[d][0]==((0xAABB883E&~0xC0u)|(lanes==1?0:0x80))&&regs[d][1]==((0xCCDD993C&~0xC0u)|(lanes==1?0x40:0x80)));neighbors();}
  for(unsigned rate:{6u,10u,20u}){reset(d);field(port,0x128,rate);XE::wrapTGLPortWriteReg(port,buffer(),0xC123FF87);
   const unsigned symbols=rate==6?17:rate==10?27:54;
   assert(writeCalls==1&&seenReg==buffer()&&seenValue==((0xC123FF87&~0xFF00u)|(symbols<<8)));neighbors();}
  for(unsigned kind=0;kind<6;++kind){reset(d);
   if(kind==0){reg(buffer())=0x80000080;readyAfter=2;}
   if(kind==1)reg(buffer())=0x80000080;
   if(kind==2)reg(buffer())=0x80;
   if(kind==3)reg(buffer())=0xFFFFFFFF;
   if(kind==4)reg(buffer())=0x80000000;
   if(kind==5){nativeResult=0xBAD;assert(!mprotect(mmio,span,PROT_NONE));}
   auto r=XE::wrapTGLEnablePort(port);assert(enableCalls==1);
   assert(r==((kind==0||kind==4)?0u:nativeResult));assert(delays==(kind==0?2u:kind==1?30u:0u));
   if(kind!=5)neighbors();
  }
  for(unsigned reason=0;reason<4;++reason){reset(d);
   if(reason==0)object.tglTCWiring.legacy=0;
   if(reason==1){object.tglTCWiring.legacy=0;object.tglTCWiring.typeC=1u<<d;}
   if(reason==2){object.tglTCWiring.legacy=0;object.tglTCWiring.excluded=1u<<d;}
   if(reason==3)object.tglAUXParentMask=0;
   assert(!mprotect(mmio,span,PROT_NONE));
   assert(XE::wrapTGLSignalLevels(port,1,1,nullptr)==0x1234&&!seenOverride);
   XE::wrapTGLPhyClockGating(port,false);XE::wrapTGLSetDklDPMode(port);
   XE::wrapTGLPortWriteReg(port,buffer(),0xC123FF87);assert(seenValue==0xC123FF87);
   assert(XE::wrapTGLEnablePort(port)==nativeResult);
   assert(!accesses&&!delays&&clockCalls==1&&modeCalls==1&&signalCalls==1&&writeCalls==1&&enableCalls==1);
  }
  // Native port table/register mismatch must never redirect a write or poll.
  reset(d);field(port,0x590,buffer()+0x100);XE::wrapTGLPortWriteReg(port,buffer(),0xC123FF87);assert(seenValue==0xC123FF87);
  assert(!mprotect(mmio,span,PROT_NONE));assert(XE::wrapTGLEnablePort(port)==nativeResult&&delays==0);
  reset(d);XE::wrapTGLPortWriteReg(port,buffer()+0x100,0xC123FF87);assert(seenReg==buffer()+0x100&&seenValue==0xC123FF87);
  reset(d);field(port,0x128,30u);XE::wrapTGLPortWriteReg(port,buffer(),0xC123FF87);assert(seenValue==0xC123FF87);
  reset(d);readable=false;XE::wrapTGLPhyClockGating(port,false);assert(clockCalls==1&&writes==0);
 }
 assert(!mprotect(mmio,span,PROT_READ|PROT_WRITE));munmap(mmio,span);
 puts("PASS actual 5 TX wrappers across 6 mock-qualified ports, quiet/on: native coefficient/clock/mode semantics, all lane counts/rates, buffer-local stagger, 3ms late-enable success/timeout/all-ones, protected-MMIO wiring/backend exclusions, native errors and neighboring outputs preserved. No physical qualification.");
}
