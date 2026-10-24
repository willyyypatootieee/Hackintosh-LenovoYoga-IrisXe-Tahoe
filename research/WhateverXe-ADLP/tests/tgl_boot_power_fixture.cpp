#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>
#include <algorithm>
#include <sys/mman.h>
#include "kern_tgl_tc_cold.hpp"
#include "kern_tgl_boot_readout.hpp"
// Execute original init/probeBootPipe; hardware and release callees are mocks.
// This proves requested releases, not actual physical shutdown of the output.
alignas(8) uint8_t controller[0x2000],power[0x80],manager[0x80],port[0x600];
uintptr_t vtable[64];std::map<uint32_t,uint32_t> registers;
std::vector<unsigned> ddiDrops,auxDrops,pgDrops;
unsigned pgWrites;
void pgState(void *p,bool enable,unsigned request){
 assert(p==power);++pgWrites;
 assert((request&0xff)==request);
 // MMIO transition is mocked. Only native count/request generation is replayed.
 registers[0x45404]=(request&0xaa)|((request&0xaa)>>1);
}
TGLTCCold cold;
struct Transport {
 bool locked=false,mail=false;unsigned block=0,unblock=0;uint32_t low=0;
 void coldLock(){assert(!locked);locked=true;}void coldUnlock(){assert(locked);locked=false;}
 bool pcodeTryLock(){assert(locked&&!mail);mail=true;return true;}void pcodeUnlock(){assert(mail);mail=false;}
 uint32_t read(uint32_t r){assert(mail && (r==0x138124||r==0x138128||r==0x13812c));return 0;}
 void write(uint32_t r,uint32_t v){assert(mail);if(r==0x138128)low=v;else if(r==0x138124){assert(v==0x80000026);if(low)++unblock;else ++block;}}
 uint64_t nowUs(){return 0;}void delayUs(unsigned){assert(false);}void sleepMs(unsigned){assert(false);}
} transport;
uint32_t readReg(void *p,uint64_t reg){assert(p==manager);assert(registers.count(reg));return registers.at(reg);}
void logMock(const char*,...){}
void assertProtected(){assert(cold.bootstrapHeld && cold.phase==TGLTCCold::Phase::Blocked && cold.total>=1 && transport.unblock==0);}
void disableDDI(void *p,unsigned d){assert(p==power && d<9);assertProtected();ddiDrops.push_back(d);auto &n=*reinterpret_cast<uint32_t*>(power+0x30+4*d);assert(n);--n;}
void disableAux(void *p,unsigned d){assert(p==power && d<9);assertProtected();auxDrops.push_back(d);auto &n=*reinterpret_cast<uint32_t*>(power+0x54+4*d);assert(n);--n;}
void disablePG(void *p,unsigned pg){assert(p==power && pg>=1 && pg<=4);assertProtected();pgDrops.push_back(pg);for(unsigned i=1;i<=pg;++i){auto &n=*reinterpret_cast<uint32_t*>(power+0x1c+4*i);if(n)--n;}}
void *getPort(unsigned d){assert(d<9);return port;}
uint32_t protocol(void *p){assert(p==port);return 2;}
struct ReadoutIO {
 void (*get)(void*,unsigned);void (*put)(void*,unsigned);
 bool admissionReady(){return cold.bootstrapHeld && cold.phase==TGLTCCold::Phase::Blocked;}
 uint32_t refs(unsigned i){return *reinterpret_cast<uint32_t*>(power+0x20+4*i);}
 void nativeGetPG4(){get(power,4);}void nativePutPG4(){put(power,4);}
 bool readable(uint32_t r){return registers.count(r);}
 uint32_t read(uint32_t r){assertProtected();return readReg(manager,r);}
};
void unexpected(){assert(false);}
void jump(uint8_t *p,uintptr_t t){p[0]=0x48;p[1]=0xb8;std::memcpy(p+2,&t,8);p[10]=0xff;p[11]=0xe0;}
void callTo(uint8_t *p,unsigned site,unsigned target){assert(p[site]==0xe8);int32_t n=target-site-5;std::memcpy(p+site+1,&n,4);}
int main(int argc,char **argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(f)),{});
 const size_t size=4*1024*1024;assert(b.size()>0x138bb0 && b.size()<size);
 auto m=(uint8_t*)mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0);assert(m!=MAP_FAILED);std::memcpy(m,b.data(),b.size());
 using Init=void(*)(void*,void*);auto init=(Init)(m+0x59f88);
 using PG=void(*)(void*,unsigned);auto getPG=(PG)(m+0x5bedc),putPG=(PG)(m+0x5b366);
 auto target=[&](uintptr_t fn,unsigned offset,std::initializer_list<unsigned> sites){jump(m+offset,fn);for(auto site:sites)callTo(m,site,offset);};
 target((uintptr_t)std::memset,0x300000,{0x59fcb,0x59fde,0x59ff2});
 target((uintptr_t)readReg,0x300020,{0x5a033,0x5a04b,0x5a063,0x45400,0x45414,0x45428});
 target((uintptr_t)logMock,0x300040,{0x5a286,0x5a2bd,0x5a2f4,0x5a32b,0x5a3b2,0x5a43c,0x5a466,0x5a4c4,0x5a523,0x5a552,0x5a581,0x5a5b4,0x5a5ea,0x5a620,0x5a656,0x45525});
 target((uintptr_t)disableDDI,0x300060,{0x5a447});target((uintptr_t)disableAux,0x300080,{0x5a471});
 target((uintptr_t)disablePG,0x3000a0,{0x5a530,0x5a55f,0x5a58e,0x5a5fe,0x5a634,0x5a66a});
 jump(m+0x5b640,(uintptr_t)pgState); // only the hardware transition callee
 target((uintptr_t)getPort,0x3000c0,{0x5a3fc});target((uintptr_t)unexpected,0x3000e0,{0x5a3ba,0x5a694,0x5bf40});
 int32_t rel;std::memcpy(&rel,m+0x5a36c,4);assert(0x5a370+rel==0x453d6); // actual native probe
 vtable[0x178/8]=(uintptr_t)protocol;*reinterpret_cast<void**>(port)=vtable;
 unsigned order[]={4,5,6},cases=0,multi=0;
 do {for(unsigned mask=1;mask<8;++mask){
  std::memset(controller,0,sizeof(controller));std::memset(power,0,sizeof(power));
  *reinterpret_cast<void**>(controller+0xc40)=manager;*reinterpret_cast<uint32_t*>(controller+0xd5c)=3;
  registers={{0x45404,0x55},{0x45454,0},{0x45444,0},{0x60400,0},{0x61400,0},{0x62400,0}};
  unsigned first=9,count=0;
  for(unsigned pipe=0;pipe<3;++pipe)if(mask&(1u<<pipe)){
   unsigned d=order[pipe];if(first==9)first=d;++count;
   registers[0x60400+0x1000*pipe]=0x80000000|((d+1)<<27)|(2u<<24);
   registers[0x45454]|=1u<<(2*d);registers[0x45444]|=1u<<(2*d);
  }
  cold={};transport={};ddiDrops.clear();auxDrops.clear();pgDrops.clear();
  assert(cold.beginBootstrap(transport,1).ok());init(power,controller);
  assert(ddiDrops.size()==count-1 && auxDrops.size()==count-1);
  for(auto d:ddiDrops)assert(d!=first);
  assert(cold.total==1 && cold.bootstrapHeld && transport.block==1 && !transport.unblock);
  // Native PG4 acquires all refs1..4 and preserves inherited per-PG counts.
  uint32_t before[4];for(unsigned i=0;i<4;++i)before[i]=*reinterpret_cast<uint32_t*>(power+0x20+4*i);
  assert(before[0]>0);pgWrites=0;getPG(power,4);
  for(unsigned i=0;i<4;++i)assert(*reinterpret_cast<uint32_t*>(power+0x20+4*i)==before[i]+1);
  if(!before[3])assert(pgWrites==1 && registers[0x45404]==0xff);
  putPG(power,4);
  for(unsigned i=0;i<4;++i)assert(*reinterpret_cast<uint32_t*>(power+0x20+4*i)==before[i]);
  // Actual native PG4 get/put connected to the production readout, with MMIO
  // transitions mocked. All pipes are retained, not just probeBootPipe's first.
  registers[0x42000]=0x07800000;
  for(unsigned d=3;d<=8;++d){TGLTypeCPort tc;TGLFiaPort fia;
   assert(TGLTypeCPort::resolve(d,tc)&&tc.resolveFia(0x10,fia));
   registers[tc.ddiBuffer]=0;registers[fia.live]=0x10;
   registers[fia.ready]=3;registers[fia.owned]=3;registers[fia.laneSelect]=0xff;}
  ReadoutIO readout {getPG,putPG};const auto snapshot=TGLBootReadout::capture(readout);
  assert(snapshot.status==TGLBootReadout::Status::Success && snapshot.activeMask==mask);
  assert(snapshot.getCalls==1 && snapshot.putCalls==1 && !snapshot.pgHeld);
  for(unsigned i=0;i<4;++i)assert(snapshot.before[i]==before[i] && snapshot.after[i]==before[i]);
  for(auto &p:snapshot.ports)assert(p.status==4);
  // A handoff callback runs while this PG4 reference is live. Taking its
  // OWN durable PG3/AUX-parent reference must survive PG4's balanced put.
  bool consumed=false;
  const auto handed=TGLBootReadout::capture(readout,[&](const TGLBootReadout::Result &v){
   assert(v.pgHeld && v.status==TGLBootReadout::Status::Success);
   for(unsigned i=0;i<4;++i)assert(readout.refs(i)==before[i]+1);
   getPG(power,3);consumed=true;
  });
  assert(consumed && handed.status==TGLBootReadout::Status::Success && !handed.pgHeld);
  for(unsigned i=0;i<4;++i)assert(readout.refs(i)==before[i]+(i<3?1:0));
  putPG(power,3);for(unsigned i=0;i<4;++i)assert(readout.refs(i)==before[i]);
  // These power-off requests do not prove the inherited hardware is idle.
  TGLTCCold::BootInventory unknown;assert(cold.finishBootstrap(transport,1,unknown).status==TGLTCCold::Status::InUse);
  assert(!transport.unblock);++cases;if(count>1)++multi;
 }}while(std::next_permutation(order,order+3));
 munmap(m,size);
 printf("PASS: %u original PowerWell::init + probeBootPipe cases; %u multiple-active-pipe cases request non-first DDI/AUX release; early bootstrap retained in all cases\n",cases,multi);
 puts("PASS: native PG4 get/put reference and request generation on all 42 inherited layouts, preserving original refs1..4");
 puts("PASS: production powered readout coupled to original PG4 get/put on all 42 layouts, all 3 pipes preserved");
 puts("MMIO/release callees are mocks; physical idle and bootstrap inventory remain unqualified. No driver loaded.");
}
