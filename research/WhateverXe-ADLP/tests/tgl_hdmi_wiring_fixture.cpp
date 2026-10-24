#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include <string>
#include "kern_tgl_hdmi_wiring.hpp"
#include "kern_vbt.hpp"
#include "kern_xelp_donor.hpp"
struct OSObject {virtual ~OSObject()=default;};
struct OSData:OSObject {std::vector<uint8_t> data;void *getBytesNoCopy(){return data.data();}uint32_t getLength(){return data.size();}};
struct IOService {OSObject *property {};unsigned reads {};OSObject *getProperty(const char *name){assert(!strcmp(name,"tgl-hdmi-wiring"));++reads;return property;}};
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 template<class... A> void setProperty(A...){}
};
#define OSDynamicCast(t,p) dynamic_cast<t*>(p)
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
#define arrsize(a) (sizeof(a)/sizeof(a[0]))
uintptr_t testCaller=0x100;
#define __builtin_return_address(n) reinterpret_cast<void *>(testCaller)
void clock_get_uptime(uint64_t *p){*p=0;}void absolutetime_to_nanoseconds(uint64_t t,uint64_t *p){*p=t;}
void *current_thread(){return reinterpret_cast<void *>(1);}
struct XE {
 bool tglHDMIB=true,tglNoPanel=true,tglStolenMemoryReady=false;
 struct {uint32_t fStolenMemorySize=0;} framebufferPatch;
 XELP::VBT::Ports firmwarePorts;
 // FIELDS
 uintptr_t orgTGLGetOSInformation {},orgTGLGMBus {},orgTGLComputeLanes {},tglHDMILaneCaller=0x100;
 uint64_t tglNoPanelPlatform[0x1C0/8] {};
 static void tglBeginBootstrapTrial(void*){}
 static bool wrapTGLGetOSInformation(void *);
 static uint32_t wrapTGLGMBus(void *,uint32_t,uint32_t,void *);
 static bool wrapTGLComputeLanes(void *,const void *,uint32_t,uint32_t,uint32_t *);
};
XE object,*callbackXE=&object;
// OBSERVATIONS
// FUNCTIONS
alignas(8) uint8_t controller[0xD50] {},original[0x1C0] {},port[0x600] {};
unsigned nativeLanesCalls=0,nativeGMBusCalls=0;bool nativeLanesResult=false,nativeOSResult=true,edidSelected=false;
bool nativeOS(void*){return nativeOSResult;}
bool nativeLanes(void*,const void*,uint32_t,uint32_t,uint32_t*){++nativeLanesCalls;return nativeLanesResult;}
uint32_t nativeGMBus(void*,uint32_t,uint32_t,void*){++nativeGMBusCalls;edidSelected=false;
 for(auto &o:tglGMBusObservations)if(o.thread==1)edidSelected=o.edid;return 0x91;}
uint8_t *&platform(){return *reinterpret_cast<uint8_t **>(controller+0xD40);}
void setup(){
 object=XE{};object.orgTGLGetOSInformation=(uintptr_t)nativeOS;object.orgTGLComputeLanes=(uintptr_t)nativeLanes;object.orgTGLGMBus=(uintptr_t)nativeGMBus;
 memset(controller,0,sizeof(controller));memset(original,0x5a,sizeof(original));platform()=original;original[0x12]=9;
 for(unsigned d=0;d<9;++d){uint32_t cfg[]={d,d+1,1,d>=3?1u:0u,0x400,0x201};memcpy(original+0x20+24*d,cfg,24);}
 const uint32_t panel[]={0,0,0,0,2,0x18};memcpy(original+0x20,panel,24);
 nativeLanesCalls=nativeGMBusCalls=0;nativeLanesResult=false;nativeOSResult=true;testCaller=0x100;
 memset(port,0,sizeof(port));memset(tglGMBusObservations,0,sizeof(tglGMBusObservations));
}
OSData property(uint32_t mask,uint32_t a,uint32_t b,uint32_t c,uint32_t version=1){OSData p;for(auto v:{version,mask,a,b,c})for(unsigned i=0;i<4;++i)p.data.push_back(v>>(8*i));return p;}
void expectUnchangedExcept(const uint8_t *after,std::initializer_list<unsigned> offsets){
 auto expected=std::vector<uint8_t>(original,original+sizeof(original));for(auto at:offsets)memcpy(expected.data()+at,after+at,4);
 assert(!memcmp(after,expected.data(),sizeof(original)));
}
int main(){
 setup();auto current=property(2,0,2,0);IOService absent{&current};assert(XE::tglLoadHDMIWiring(&absent));assert(XE::wrapTGLGetOSInformation(controller));
 const auto p=platform();assert(p!=original&&object.tglHDMIWiringApplied&&object.tglHDMIWiring.ddc[1]==2);
 expectUnchangedExcept(p,{0x30,0x34,0x40,0x48,0x4C}); // panel type/flags, HDMI protocol/type/flags
 assert(*reinterpret_cast<uint32_t *>(p+0x40)==2 && *reinterpret_cast<uint32_t *>(p+0x48)==0x800 && *reinterpret_cast<uint32_t *>(p+0x4C)==0x202);
 // Changes to unrelated TC wiring are neither overwritten nor treated as a
 // reason to reject the independent combo profile.
 setup();reinterpret_cast<uint32_t *>(original+0x20+4*24)[1]=14;IOService defaultProvider{&current};
 assert(XE::tglLoadHDMIWiring(&defaultProvider)&&XE::wrapTGLGetOSInformation(controller));
 assert(reinterpret_cast<uint32_t *>(platform()+0x20+4*24)[1]==14);
 // No board inference without an explicit property, even with the feature flag.
 setup();IOService unknown;assert(XE::tglLoadHDMIWiring(&unknown));
 assert(!object.tglHDMIWiring.hdmi&&XE::wrapTGLGetOSInformation(controller));
 // Firmware default reaches the actual loader and native topology unchanged.
 setup();object.firmwarePorts.status=XELP::VBT::Status::Valid;
 object.firmwarePorts.present=2;object.firmwarePorts.port[1]={0x60d2,1,2};IOService firmware;
 assert(XE::tglLoadHDMIWiring(&firmware)&&!object.tglHDMIWiringExplicit&&object.tglHDMIWiring.hdmi==2);
 assert(XE::wrapTGLGetOSInformation(controller));
 assert(reinterpret_cast<uint32_t *>(platform()+0x38)[1]==2);
 // An explicitly malformed override still rejects a usable firmware default.
 setup();object.firmwarePorts.status=XELP::VBT::Status::Valid;
 object.firmwarePorts.present=2;object.firmwarePorts.port[1]={0x60d2,1,2};
 OSData malformed;IOService invalidOverride{&malformed};assert(!XE::tglLoadHDMIWiring(&invalidOverride));
 // Explicit combo A replaces the donor panel; A+B retains separate DDC pins.
 for(unsigned mask:{1u,3u}){setup();auto a=property(mask,1,mask&2?2:0,0);IOService ap{&a};
  assert(XE::tglLoadHDMIWiring(&ap)&&XE::wrapTGLGetOSInformation(controller));
  auto cfg=reinterpret_cast<uint32_t *>(platform()+0x20);
  assert(cfg[0]==0&&cfg[1]==1&&cfg[2]==2&&cfg[4]==0x800&&cfg[5]==0x202&&object.tglHDMIEligible(cfg));
 }
 // Panel-preserving path still applies HDMI and copies the shared topology.
 setup();object.tglNoPanel=false;IOService panelProvider{&current};
 assert(XE::tglLoadHDMIWiring(&panelProvider)&&XE::wrapTGLGetOSInformation(controller));
 assert(!memcmp(platform()+0x20,original+0x20,24));
 expectUnchangedExcept(platform(),{0x40,0x48,0x4C});
 setup();object.tglNoPanel=false;IOService panelOnly;
 assert(XE::tglLoadHDMIWiring(&panelOnly)&&XE::wrapTGLGetOSInformation(controller));
 assert(platform()!=original&&!memcmp(platform(),original,sizeof(original)));
 unsigned busCases=0;
 for(unsigned pin=1;pin<=3;++pin){setup();auto data=property(2,0,pin,0);IOService provider{&data};
  assert(XE::tglLoadHDMIWiring(&provider)&&!object.tglHDMIDDCEligible(pin));
  assert(XE::wrapTGLGetOSInformation(controller));
  expectUnchangedExcept(platform(),{0x30,0x34,0x3C,0x40,0x48,0x4C});
  auto cfg=reinterpret_cast<uint32_t *>(platform()+0x38);assert(cfg[1]==pin&&object.tglHDMIEligible(cfg));
  *reinterpret_cast<void **>(port+0x548)=cfg;
  uint32_t lanes=0;assert(XE::wrapTGLComputeLanes(port,nullptr,32,0,&lanes)&&lanes==0&&nativeLanesCalls==1);
  testCaller=0x101;assert(!XE::wrapTGLComputeLanes(port,nullptr,32,0,&lanes));testCaller=0x100;
  lanes=1;assert(!XE::wrapTGLComputeLanes(port,nullptr,32,0,&lanes));lanes=0;
  assert(!XE::wrapTGLComputeLanes(port,nullptr,32,1,&lanes));
  auto saved=cfg[1];cfg[1]=pin==1?2:1;assert(!XE::wrapTGLComputeLanes(port,nullptr,32,0,&lanes));cfg[1]=saved;
  for(unsigned bus=0;bus<=14;++bus)for(unsigned fault=0;fault<5;++fault){
   uint32_t request[16] {};request[1]=1;request[3]=0xA0;request[14]=128;
   uint32_t speed=fault==1?1:0;if(fault==2)request[1]=0;if(fault==3)request[3]=0xA2;if(fault==4)request[14]=64;
   assert(XE::wrapTGLGMBus(controller,bus,speed,request)==0x91);
   assert(edidSelected==(bus==pin&&fault==0));++busCases;
  }
  auto changed=property(2,0,pin==1?2:1,0);provider.property=&changed;
  assert(XE::tglLoadHDMIWiring(&provider)&&provider.reads==1&&object.tglHDMIWiring.ddc[1]==pin);
 }
 for(unsigned fault=0;fault<8;++fault){setup();auto data=property(2,0,2,0);OSObject wrong;IOService provider{&data};
  if(fault==0)provider.property=&wrong;if(fault==1)data.data.pop_back();if(fault==2)data.data.push_back(0);
  if(fault==3)data.data[0]=2;if(fault==4)data=property(8,0,0,0);
  if(fault==5)data=property(2,1,2,0);if(fault==6)data=property(2,0,4,0);if(fault==7)data=property(6,0,2,2);
  assert(!XE::tglLoadHDMIWiring(&provider)&&!object.tglHDMIWiring.valid);
  auto good=property(2,0,2,0);provider.property=&good;assert(!XE::tglLoadHDMIWiring(&provider)&&provider.reads==1);
 }
 setup();auto two=property(6,0,1,3);IOService provider{&two};assert(!XE::tglLoadHDMIWiring(&provider)&&object.tglHDMIWiring.valid);
 // Additional backend qualification is CPU-fixture-only, never inferred
 // from the property. Both selected existing connectors change atomically.
 setup();object.tglHDMIBackendMask=6;provider.reads=0;assert(XE::tglLoadHDMIWiring(&provider));
 assert(XE::wrapTGLGetOSInformation(controller));assert(object.tglHDMIEligible(reinterpret_cast<uint32_t *>(platform()+0x50)));
 for(unsigned fault=0;fault<3;++fault){setup();object.tglHDMIBackendMask=6;provider.reads=0;assert(XE::tglLoadHDMIWiring(&provider));
  if(fault==0)original[0x12]=8;if(fault==1)reinterpret_cast<uint32_t *>(original+0x20)[4]=0x400;
  if(fault==2)reinterpret_cast<uint32_t *>(original+0x50)[2]=3;
  auto before=std::vector<uint8_t>(original,original+sizeof(original));assert(!XE::wrapTGLGetOSInformation(controller));
  assert(platform()==original&&!object.tglHDMIWiringApplied&&!memcmp(original,before.data(),before.size()));
 }
 setup();object.tglHDMIB=false;auto explicitHDMI=property(2,0,2,0);IOService disabled{&explicitHDMI};assert(XE::tglLoadHDMIWiring(&disabled)&&object.tglHDMIB);
 setup();object.tglHDMIB=false;IOService none;assert(XE::tglLoadHDMIWiring(&none)&&XE::wrapTGLGetOSInformation(controller));assert(!object.tglHDMIWiring.hdmi);
 expectUnchangedExcept(platform(),{0x30,0x34});
 printf("PASS actual HDMI loader/topology/GMBUS/lane wrappers: default byte parity, immutable pin1..3 profiles, %u EDID selections/exclusions, invalid/backend/feature rejection, atomic 2-connector fixture and untouched TC data. Hardware qualification remains separate.\n",busCases);
}
