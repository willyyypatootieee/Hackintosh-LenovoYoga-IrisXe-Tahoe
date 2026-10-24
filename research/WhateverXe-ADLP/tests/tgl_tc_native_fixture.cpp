#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>
#include <algorithm>
#include <sys/mman.h>
#include "kern_tgl_tc_native.hpp"
using Binding=TGLTCNativeBinding;using Status=Binding::Status;
TGLTCNativeFunctions functions;uintptr_t vtable[64] {};unsigned auxGets {},auxPuts {},zeroCalls {};
template<class T> T &at(void *p,unsigned off){return *reinterpret_cast<T *>(static_cast<uint8_t *>(p)+off);}
void jump(uint8_t *p,uintptr_t t){p[0]=0x48;p[1]=0xb8;memcpy(p+2,&t,8);p[10]=0xff;p[11]=0xe0;}
void rel(uint8_t *p,unsigned site,unsigned target,uint8_t opcode){assert(p[site]==opcode);int32_t delta=target-site-5;memcpy(p+site+1,&delta,4);}
void nativeBzero(void *p,size_t n){++zeroCalls;memset(p,0,n);}
// The real AppleIntelPort AUX wrapper derives power from gController+C8 and
// DDI from port+548. Only its lower hardware AUX operation is mocked. Preserve
// the existing parent's extra PG3 reference so the separate Mode hold can be
// distinguished from the AUX hold rather than accidentally borrowing it.
void auxGet(void *power,uint32_t ddi){assert(power==at<void *>(*functions.controllerSlot,0xC8));assert(ddi>=3&&ddi<=8);
 ++auxGets;++at<uint32_t>(power,0x54+4*ddi);functions.getPG(power,3);}
void auxPut(void *power,uint32_t ddi){assert(power==at<void *>(*functions.controllerSlot,0xC8));assert(ddi>=3&&ddi<=8);
 ++auxPuts;assert(at<uint32_t>(power,0x54+4*ddi)>1);--at<uint32_t>(power,0x54+4*ddi);functions.putPG(power,3);}
void *ports[9] {};void *lookup(uint32_t d){return d<9?ports[d]:nullptr;}
struct Bus {
 alignas(8) uint8_t controller[0xD00] {},power[0x90] {},manager[0x70] {},port[6][0x600] {};
 uint32_t config[6][8] {};std::vector<uint8_t> mmio;TGLTCCold cold;
 bool locked=false,pcode=false,fia=false,submitted=false,admitted=true,firmware=true,idle=true;
 uint32_t command {},blocks {},unblocks {},fiaReads {},fiaWrites {};uint64_t ticks {};
 Bus():mmio(0x180000){
  *functions.controllerSlot=controller;at<void *>(controller,0xC8)=power;at<void *>(controller,0xC40)=manager;
  at<void *>(power,0x10)=controller;at<void *>(power,0x78)=manager;
  at<void *>(manager,0x50)=mmio.data();at<uint32_t>(manager,0x60)=mmio.size();
  // Positive unrelated refs keep the real native PG get/put on its accounting
  // path. Hardware power transitions are not emulated or claimed by this test.
  for(unsigned i=1;i<=4;++i)at<uint32_t>(power,0x1C+4*i)=2;
  reg(0x45404)=0x3F;reg(0x42000)=0x07000000;
  for(unsigned i=0;i<6;++i){auto d=i+3;ports[d]=port[i];config[i][0]=d;config[i][5]=0x201;
   at<void *>(port[i],0)=vtable;at<void *>(port[i],0x548)=config[i];at<void *>(port[i],0x578)=manager;
   at<uint32_t>(port[i],0x590)=0x64000+0x100*d;at<uint32_t>(power,0x54+4*d)=1;}
  for(auto base:{0x163000u,0x16e000u,0x16f000u}){reg(base+0x8A0)=base==0x163000?0x10:0;reg(base+0x890)=3;reg(base+0x894)=0x40000000;}
 }
 uint32_t &reg(uint32_t r){return at<uint32_t>(mmio.data(),r);}
 Binding bind(unsigned d){return Binding::bind(functions,controller,ports[d],d,reinterpret_cast<uintptr_t>(mmio.data()),mmio.size());}
 uintptr_t owner(){return reinterpret_cast<uintptr_t>(controller);}
};
struct Transport {
 Bus &b;
 bool admissionReady(){return b.admitted;}bool firmwareVerified(){return b.firmware;}
 bool inheritedVerified(){return false;}bool quiescent(){return b.idle;}
 void coldLock(){assert(!b.locked);b.locked=true;}void coldUnlock(){assert(b.locked);b.locked=false;}
 bool pcodeTryLock(){assert(b.locked&&!b.pcode);b.pcode=true;b.submitted=false;return true;}
 void pcodeUnlock(){assert(b.pcode);b.pcode=false;}
 bool readable(uint32_t r){return !(r&3)&&r<=b.mmio.size()-4;}
 uint32_t read(uint32_t r){assert(readable(r));
  if(r==0x138124){assert(b.pcode);return 0;}
  if(r==0x138128||r==0x13812c){assert(b.pcode);return 0;}
  if(r>=0x163000){assert(!b.pcode&&b.cold.phase==TGLTCCold::Phase::Blocked);++b.fiaReads;}
  return b.reg(r);
 }
 void write(uint32_t r,uint32_t v){assert(readable(r));
  if(r==0x138128){assert(b.pcode);b.command=v;return;}
  if(r==0x13812c){assert(b.pcode&&!v);return;}
  if(r==0x138124){assert(b.pcode&&v==0x80000026);b.submitted=true;if(b.command)++b.unblocks;else ++b.blocks;return;}
  assert(b.fia&&!b.pcode);++b.fiaWrites;b.reg(r)=v;
 }
 uint64_t nowUs(){return b.ticks;}void delayUs(unsigned n){b.ticks+=n;}void sleepMs(unsigned n){assert(!b.fia);b.ticks+=1000*n;}
 void fiaLock(unsigned i){assert(i<3&&!b.fia);b.fia=true;}void fiaUnlock(unsigned i){assert(i<3&&b.fia);b.fia=false;}
};
using IO=TGLTCNativeModeIO<Transport>;
int main(int argc,char **argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
 auto image=static_cast<uint8_t *>(mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0));assert(image!=MAP_FAILED);
 memcpy(image,bytes.data(),bytes.size());jump(image+0x300000,(uintptr_t)nativeBzero);
 for(auto site:{0x1b16eu,0x1b17cu})rel(image,site,0x300000,0xe8);rel(image,0x1b196,0x300000,0xe9);
 vtable[0x120/8]=(uintptr_t)(image+0x1b11c);
 // Native gController references carry external relocations. Link them to one
// shared slot, not to fake per-port controller pointers.
 for(auto site:{0x1d250u,0x1d28cu}){assert(image[site]==0x48&&image[site+1]==0x8d);int32_t d=0x300100-site-7;memcpy(image+site+3,&d,4);}
 jump(image+0x300200,(uintptr_t)auxGet);jump(image+0x300300,(uintptr_t)auxPut);
 rel(image,0x1d275,0x300200,0xe9);rel(image,0x1d2b1,0x300300,0xe9);
 functions.baseMode=(uint32_t(*)(void*,uint32_t))(image+0x1b4c8);
 functions.getAux=(void(*)(void*))(image+0x1d23e);functions.putAux=(void(*)(void*))(image+0x1d27a);
 functions.getPG=(void(*)(void*,uint32_t))(image+0x5bedc);functions.putPG=(void(*)(void*,uint32_t))(image+0x5b366);
 functions.controllerSlot=(void**)(image+0x300100);functions.portByDDI=lookup;functions.qualified=true;
 unsigned cases=0;
 for(unsigned d=3;d<=8;++d)for(bool skip:{false,true})for(bool owned:{false,true}){
  Bus b;at<uint32_t>(b.controller,0xC58)=skip?2:0;auto binding=b.bind(d);assert(binding.ready());IO io{{b},binding};TGLTCMode mode;
  const auto a=auxGets,p=auxPuts;assert(b.cold.beginBootstrap(io,b.owner()).ok());
  TGLTypeCPort tc;TGLFiaPort fia;assert(TGLTypeCPort::resolve(d,tc)&&tc.resolveFia(0x10,fia));
  if(owned)b.reg(fia.owned)|=fia.readyMask;
  assert(mode.adoptFirmware(io,b.cold,b.owner(),(uintptr_t)ports[d],d).ok());
  assert(io.nativeMode()==4&&auxGets-a==unsigned(!skip));
  assert(at<uint32_t>(b.power,0x28)==(skip?3u:4u)); // Mode PG3 is independent
  for(unsigned n=0;n<3;++n)assert(mode.connect(io,b.cold,b.owner(),(uintptr_t)ports[d],d).ok());
  assert(auxGets-a==unsigned(!skip));b.idle=false;auto writes=b.fiaWrites;
  assert(mode.disconnect(io,b.cold,b.owner(),(uintptr_t)ports[d],d).status==TGLTCMode::Status::InUse&&b.fiaWrites==writes);
  b.idle=true;assert(mode.disconnect(io,b.cold,b.owner(),(uintptr_t)ports[d],d).ok());
  assert(io.nativeMode()==0&&auxPuts-p==unsigned(!skip)&&at<uint32_t>(b.power,0x54+4*d)==1);
  for(unsigned i=1;i<=4;++i)assert(at<uint32_t>(b.power,0x1C+4*i)==2);
  assert(b.cold.total==1&&b.cold.bootstrapHeld&&!b.unblocks);++cases;
 }
 for(unsigned fault=0;fault<10;++fault){Bus b;
  switch(fault){case 0:*functions.controllerSlot=nullptr;break;case 1:at<uint8_t>(b.controller,0x918)=1;break;
   case 2:ports[4]=b.port[0];break;case 3:b.config[1][0]=3;break;case 4:at<void *>(b.power,0x10)=nullptr;break;
   case 5:b.power[0x18]=1;break;case 6:at<void *>(b.port[1],0x578)=nullptr;break;
   case 7:at<void *>(b.power,0x78)=nullptr;break;case 8:at<void *>(b.manager,0x50)=nullptr;break;
   case 9:at<uint32_t>(b.port[1],0x590)=0x64400+0x100;break;}
  auto binding=b.bind(4);assert(!binding.ready());IO io{{b},binding};TGLTCMode mode;
  assert(mode.connect(io,b.cold,b.owner(),(uintptr_t)b.port[1],4).status==TGLTCMode::Status::NotAdmitted);
  assert(!b.blocks&&!b.fiaReads&&!b.fiaWrites);
 }
 for(unsigned fault=0;fault<4;++fault){Bus b;auto binding=b.bind(4);IO io{{b},binding};TGLTCMode mode;
  const auto a=auxGets;b.reg(fault<2?0x45404:0x42000)=fault%2?UINT32_MAX:0;
  assert(mode.connect(io,b.cold,b.owner(),(uintptr_t)ports[4],4).status==TGLTCMode::Status::PowerError);
  assert(mode.coldHeld&&mode.laneHeld&&!b.fiaReads&&auxGets==a&&io.nativeMode()==0);
  b.reg(0x45404)=0x3F;b.reg(0x42000)=0x07000000;
  assert(mode.disconnect(io,b.cold,b.owner(),(uintptr_t)ports[4],4).ok());
  assert(at<uint32_t>(b.power,0x28)==2&&b.unblocks==1);
 }
 // Three simultaneously bound physical ports, every release order. Hardware
 // routes and lifecycle proofs remain mocked; real native accounting is used.
 unsigned order[]={0,1,2},orders=0;
 do{Bus b;auto a=b.bind(4),c=b.bind(5),d=b.bind(8);IO ios[]={IO{{b},a},IO{{b},c},IO{{b},d}};TGLTCMode modes[3];
  for(unsigned i=0;i<3;++i){auto &io=ios[i];assert(modes[i].connect(io,b.cold,b.owner(),(uintptr_t)io.binding.port,io.binding.ddi).ok());}
  assert(at<uint32_t>(b.power,0x28)==8&&b.blocks==1);
  for(auto i:order){auto &io=ios[i];assert(modes[i].disconnect(io,b.cold,b.owner(),(uintptr_t)io.binding.port,io.binding.ddi).ok());}
  assert(at<uint32_t>(b.power,0x28)==2&&b.cold.total==0&&b.unblocks==1);++orders;
 }while(std::next_permutation(order,order+3));
 assert(zeroCalls);munmap(image,0x400000);
 printf("PASS native ABI bridge: %u TC1..6 firmware/adoption/AUX cases; 10 identity rejections; 4 power failures retained/retried; %u three-port release orders. Real base/reset, port-AUX and PG1..3 accounting. Hardware transitions/lifecycle exclusion mocked.\n",cases,orders);
}
