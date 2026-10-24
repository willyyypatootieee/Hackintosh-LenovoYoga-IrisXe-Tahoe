#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#include "kern_tgl_tc_boot.hpp"
#include "kern_tgl_port_map.hpp"
struct IOService;
struct IORegistryEntry {
 std::vector<uint32_t> pre; std::vector<uint64_t> post;
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char *key,const void *data,unsigned size){
  if(std::string(key).find("TGLPreTrainingRoute")==0) {
   auto p=static_cast<const uint32_t*>(data);pre.assign(p,p+size/4);
  } else {auto p=static_cast<const uint64_t*>(data);post.assign(p,p+size/8);}
 }
};
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
void clock_get_uptime(uint64_t *p){*p=1;}
void absolutetime_to_nanoseconds(uint64_t t,uint64_t *p){*p=t;}
unsigned observations,nativeCalls;
struct XE {
 // FIELDS
 bool tglActive=true;void *tglObservationController{};uintptr_t orgTGLTrainingPattern{};
 static uint32_t wrapTGLTrainingPattern(void *,uint32_t);
 static void tglObserveActivation(void*,void*,void*,unsigned,bool,uint32_t){++observations;}
 static void tglTimeEvent(unsigned,unsigned,bool,unsigned,unsigned){}
};
XE object,*callbackXE=&object;
// FUNCTION
constexpr size_t span=0x200000;
uint8_t *mmio;
alignas(8) uint8_t path[0x3660],port[0x600],fb[0x4A50],controller[0xC80],manager[0x80];
uint32_t config[6];unsigned ddi,pipeID,patternID;bool emitTP,expectPre;uint32_t nativeResult;
uint32_t &reg(unsigned off){return *reinterpret_cast<uint32_t*>(mmio+off);}
template<class T> void field(uint8_t *p,unsigned off,T v){*reinterpret_cast<T*>(p+off)=v;}
uint32_t clk(){return ((ddi+1)<<28)|0x3456;}
uint32_t func(){return ((ddi+1)<<27)|0x02000020|((*reinterpret_cast<uint32_t*>(port+0x124)-1)<<1);}
uint32_t native(void *p,uint32_t pattern){
 assert(p==path&&pattern==patternID&&observations==1);++nativeCalls;
 if(expectPre){assert(reg(0x46140+4*pipeID)==clk());assert(reg(0x60400+0x1000*pipeID)==func());}
 if(emitTP)reg(0x60540+0x1000*pipeID)=0x80100A00;
 return nativeResult;
}
void protect(unsigned start,unsigned end,int mode){
 auto page=unsigned(sysconf(_SC_PAGESIZE));start=start/page*page;end=(end+page-1)/page*page;
 assert(!mprotect(mmio+start,end-start,mode));
}
void reset(unsigned d=4,unsigned p=1,unsigned pattern=1){
 protect(0,span,PROT_READ|PROT_WRITE);memset(mmio,0,span);
 memset(path,0,sizeof(path));memset(port,0,sizeof(port));memset(fb,0,sizeof(fb));
 memset(controller,0,sizeof(controller));memset(manager,0,sizeof(manager));memset(config,0,sizeof(config));
 ddi=d;pipeID=p;patternID=pattern;object=XE{};observations=nativeCalls=0;nativeResult=0;emitTP=false;expectPre=false;
 object.orgTGLTrainingPattern=(uintptr_t)native;object.tglObservationController=controller;
 // Fixture-only qualification; production keeps just TC2.
 object.tglAUXParentMask=0x1F8;object.tglTCWiring={};object.tglTCWiring.valid=true;object.tglTCWiring.legacy=0x1F8;
 config[0]=ddi;config[2]=1;field(port,0x548,config);field(port,0x550,4u);
 field(port,0x578,manager);field(port,0x124,4u);port[0x11A]=1;
 field(path,0x3648,port);field(path,0x3640,pipeID);field(path,0x32B0,fb);
 field(fb,0x1D0,controller);field(fb,0x4A40,manager);field(controller,0xC40,manager);
 field(manager,0x50,mmio);field(manager,0x60,uint32_t(span));
 reg(0x45404)=0xFF;reg(0x45454)=0x3FFFF;
 // Other transcoders/ports are live and must remain untouched.
 for(unsigned i=0;i<3;++i){reg(0x46140+4*i)=0xB1234567;reg(0x60400+0x1000*i)=0xD2345678;reg(0x60540+0x1000*i)=0xC3456789;}
 for(unsigned i=3;i<=8;++i)reg(0x64000+0x100*i)=0x80000000|i;
 if(pipeID<3){reg(0x46140+4*pipeID)=0x3456;reg(0x60400+0x1000*pipeID)=0x01000020;reg(0x60540+0x1000*pipeID)=0;}
 if(ddi>=3&&ddi<=8)reg(0x64000+0x100*ddi)=0x80;
 *IORegistryEntry::getRegistryRoot()={};
 // No FIA domain is acquired by this wrapper: any accidental diagnostic
 // read of the historical TC2 ownership register must fault.
 protect(0x160000,0x170000,PROT_NONE);
}
void invoke(){assert(XE::wrapTGLTrainingPattern(path,patternID)==nativeResult);assert(nativeCalls==1&&observations==2);}
void neighbors(){
 for(unsigned i=0;i<3;++i)if(i!=pipeID){assert(reg(0x46140+4*i)==0xB1234567);assert(reg(0x60400+0x1000*i)==0xD2345678);assert(reg(0x60540+0x1000*i)==0xC3456789);}
 for(unsigned i=3;i<=8;++i)assert(reg(0x64000+0x100*i)==(i==ddi?0x80:(0x80000000|i)));
}
int main(){
 mmio=static_cast<uint8_t*>(mmap(nullptr,span,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0));assert(mmio!=MAP_FAILED);
 unsigned cases=0;
 for(unsigned d=3;d<=8;++d)for(unsigned p=0;p<3;++p)for(unsigned lanes:{1u,2u,4u})for(unsigned framing=0;framing<2;++framing)for(unsigned pat:{1u,0u,2u,3u,4u,5u}){
  reset(d,p,pat);field(port,0x124,lanes);port[0x11A]=framing;emitTP=true;expectPre=pat==1;
  // Give only this port's DDI power. A fixed TC2 test would fail here.
  reg(0x45454)=3u<<(2*d);reg(0x45404)=p==2?0xFF:0x3F;
  invoke();assert(reg(0x60540+0x1000*p)==(0x80100A00|(framing<<18)));neighbors();
  auto &r=*IORegistryEntry::getRegistryRoot();assert(r.post.size()==11&&r.post[9]==p&&r.post[10]==d);
  if(pat==1){assert(r.pre.size()==22&&r.pre[1]==0&&r.pre[18]==d&&r.pre[21]==356);}
  else {assert(r.pre.empty());assert(reg(0x46140+4*p)==0x3456&&reg(0x60400+0x1000*p)==0x01000020);}
  ++cases;
 }
 // Invalid wiring, backend, identities, pipe, mode: no wrapper MMIO at all.
 for(unsigned kind=0;kind<16;++kind){
  reset();nativeResult=0xE00002D6;
  switch(kind){
   case 0:object.tglTCWiring.legacy=0;break;
   case 1:object.tglAUXParentMask=0;break;
   case 2:object.tglTCWiring={};break;
   case 3:object.tglTCWiring.legacy=0;object.tglTCWiring.typeC=0x10;break;
   case 4:object.tglTCWiring.legacy=0;object.tglTCWiring.excluded=0x10;break;
   case 5:field(path,0x3640,3u);break;
   case 6:config[0]=9;break;
   case 7:config[0]=0;break;
   case 8:object.tglActive=false;break;
   case 9:object.tglObservationController=nullptr;break;
   case 10:field(port,0x578,static_cast<void*>(nullptr));break;
   case 11:field(fb,0x4A40,static_cast<void*>(nullptr));break;
   case 12:port[0x117]=1;break;
   case 13:field(port,0x550,0u);break;
   case 14:config[2]=0;break;
   case 15:field(manager,0x60,0u);break;
  }
  protect(0,span,PROT_NONE);invoke();assert(IORegistryEntry::getRegistryRoot()->pre.empty());
  // Repeat with native success to exercise post-native guards too.
  nativeCalls=observations=0;nativeResult=0;invoke();assert(IORegistryEntry::getRegistryRoot()->post.empty());
 }
 // Power absence is checked before touching the route/TP/buffer banks, both
 // before pattern1 and after native success. Includes PW4 on transcoder C.
 for(unsigned d=3;d<=8;++d)for(unsigned p=0;p<3;++p)for(unsigned kind=0;kind<6;++kind){
  reset(d,p);reg(0x45454)=3u<<(2*d);reg(0x45404)=p==2?0xFF:0x3F;
  switch(kind){
   case 0:reg(0x45454)&=~(1u<<(2*d));break;
   case 1:reg(0x45454)&=~(2u<<(2*d));break;
   case 2:reg(0x45454)=0xFFFFFFFF;break;
   case 3:reg(0x45404)=0xFFFFFFFF;break;
   case 4:reg(0x45404)&=~(p==2?0x40:0x10);break;
   case 5:reg(0x45404)&=~(p==2?0x80:0x20);break;
  }
  protect(0x60000,0x68000,PROT_NONE);invoke();auto &r=*IORegistryEntry::getRegistryRoot();assert(r.pre[1]==5&&r.post.empty());
 }
 // Pre-route exclusions preserve native error and registers; negotiated
 // framing preserves disabled/MST outputs and invalid negotiation bytes.
 for(unsigned kind=0;kind<5;++kind){
  reset();nativeResult=7;
  if(kind==0)field(port,0x124,3u);
  if(kind==1)reg(0x60400+0x1000*pipeID)|=0x80000000;
  if(kind==2)reg(0x64000+0x100*ddi)|=0x80000000;
  if(kind==3)reg(0x46140+4*pipeID)=0xA0003456;
  if(kind==4)reg(0x60400+0x1000*pipeID)=0x48000020;
  auto c=reg(0x46140+4*pipeID),f=reg(0x60400+0x1000*pipeID);invoke();
  assert(IORegistryEntry::getRegistryRoot()->pre[1]==(kind<3?kind==0?1:2:kind));
  assert(reg(0x46140+4*pipeID)==c&&reg(0x60400+0x1000*pipeID)==f);
 }
 for(auto tp:{0x00040000u,0x88040000u,0xFFFFFFFFu}){
  reset(4,1,2);reg(0x61540)=tp;invoke();assert(reg(0x61540)==tp);
 }
 reset(4,1,2);port[0x11A]=2;reg(0x61540)=0x80000000;invoke();assert(reg(0x61540)==0x80000000);
 protect(0,span,PROT_READ|PROT_WRITE);munmap(mmio,span);
 printf("PASS actual training wrapper: %u port/pipe/lane/pattern/framing cases; 16 no-MMIO admission guards, 108 guarded power failures, native order/error, active-neighbor preservation, no FIA access; mock backend qualification only\n",cases);
}
