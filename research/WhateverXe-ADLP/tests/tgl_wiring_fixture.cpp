#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "kern_tgl_tc_boot.hpp"
#include "kern_vbt.hpp"
struct OSObject {virtual ~OSObject()=default;};
struct OSData:OSObject {
 std::vector<uint8_t> bytes;
 void *getBytesNoCopy(){return bytes.data();}uint32_t getLength(){return bytes.size();}
};
struct IOService {OSObject *property {};unsigned reads {};OSObject *getProperty(const char *n){assert(!strcmp(n,"tgl-tc-wiring"));++reads;return property;}};
struct IORegistryEntry {
 uint32_t record[10] {},candidates {};unsigned writes {};
 void setProperty(const char *name,uint32_t value,unsigned bits){assert(!strcmp(name,"WhateverXeTCDPCandidates")&&bits==32);candidates=value;}
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char *name,const void *data,unsigned length){assert(!strcmp(name,"TGLTCWiring")&&length==sizeof(record));memcpy(record,data,length);++writes;}
};
#define OSDynamicCast(type,p) dynamic_cast<type*>(p)
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
struct XE {
 bool tglActive=true;
 XELP::Profile xeProfile=XELP::identify(0x9a498086,1);XELP::VBT::Ports firmwarePorts;
 // FIELDS
 uintptr_t orgTGLLaneMask {};
 static uint32_t wrapTGLLaneMask(void *);
};
XE object,*callbackXE=&object;
// FUNCTIONS
unsigned nativeCalls;uint32_t nativeLanes(void *){++nativeCalls;return 0xA;}
OSData property(uint32_t legacy,uint32_t typeC,uint32_t excluded,uint32_t version=1){
 OSData d;for(auto q:{version,legacy,typeC,excluded})for(unsigned j=0;j<4;++j)d.bytes.push_back(q>>(8*j));return d;
}
void reset(){object=XE{};object.orgTGLLaneMask=(uintptr_t)nativeLanes;object.tglAUXParentReady=true;nativeCalls=0;*IORegistryEntry::getRegistryRoot()={};}
int main(){
 reset();IOService absent;
 assert(XE::tglLoadTCWiring(&absent)&&!object.tglTCWiringExplicit&&object.tglTCWiring.legacy==0);
 // A firmware candidate is not a registered backend, even with USB-C/TBT
 // capability. Only the actual powered provider probe can register it.
 reset();object.firmwarePorts.status=XELP::VBT::Status::Valid;
 object.firmwarePorts.present=0x18;object.firmwarePorts.port[3]={0x68c6,9,0,0x30,0xc0,3};
 object.firmwarePorts.port[4]={0x60d6,11,5,0x50,0xc0,3};
 assert(XE::tglLoadTCWiring(&absent)&&object.tglTCAutoCandidates==0x10);
 assert(object.tglTCProbeMask()==0x10&&!object.tglLegacyTCEligible(4));
 assert(object.tglTCModeAllowed(4,0)&&!object.tglTCModeAllowed(4,4));
 object.tglTCAutoRegistered=0x10;
 assert(object.tglLegacyTCEligible(4)&&object.tglTCModeAllowed(4,4));
 assert(!object.tglTCModeAllowed(4,2)&&!object.tglTCModeAllowed(4,3));
 // Present-invalid must never fall back to the known machine's preset.
 OSObject wrongType;auto good=property(1u<<4,0,0);
 for(unsigned kind=0;kind<5;++kind){
  reset();auto d=good;IOService provider;
  if(kind==0)provider.property=&wrongType;
  else {provider.property=&d;if(kind==1)d.bytes.pop_back();if(kind==2)d.bytes.push_back(0);if(kind==3)d.bytes[0]=2;if(kind==4)d.bytes[4]=1;}
  object.firmwarePorts.status=XELP::VBT::Status::Valid;object.firmwarePorts.present=0x10;object.firmwarePorts.port[4]={0x60d6,11,5,0x50};
  assert(!XE::tglLoadTCWiring(&provider)&&object.tglTCWiringExplicit&&!object.tglTCWiring.valid);
  assert(!object.tglTCAutoCandidates);
  provider.property=&good;assert(!XE::tglLoadTCWiring(&provider)&&provider.reads==1);
 }
 unsigned cases=0;
 for(unsigned ddi=3;ddi<=8;++ddi)for(unsigned role=0;role<4;++role){
  reset();uint32_t bit=1u<<ddi;auto d=property(role==1?bit:0,role==2?bit:0,role==3?bit:0);IOService provider{&d};
  assert(XE::tglLoadTCWiring(&provider)&&object.tglTCWiringExplicit);
  alignas(8) uint8_t port[0x580] {};uint32_t config[6]={ddi};
  *reinterpret_cast<uint32_t **>(port+0x548)=config;*reinterpret_cast<uint32_t *>(port+0x550)=4;
  const bool eligible=role==1;
  assert(object.tglLegacyTCEligible(ddi)==eligible&&object.tglAUXParentEligible(ddi)==eligible);
  assert(XE::wrapTGLLaneMask(port)==(eligible?0xFu:0xAu));
  assert(nativeCalls==unsigned(!eligible));
  auto other=property(0,0,0);provider.property=&other;
  assert(XE::tglLoadTCWiring(&provider)&&provider.reads==1&&object.tglLegacyTCEligible(ddi)==eligible);
  ++cases;
 }
 for(unsigned ddi=3;ddi<=8;++ddi){
  uint32_t bit=1u<<ddi;
  for(auto d:{property(bit,bit,0),property(bit,0,bit),property(0,bit,bit)})assert(!TGLTCWiring::fromProperty(d.bytes.data(),16).valid);
 }
 // Misaligned data, all 6 roles, 3 legacy intentions: no assumptions about
 // host alignment or the 3 framebuffer slots. Backend mask is independent.
 auto three=property((1u<<3)|(1u<<4)|(1u<<5),1u<<6,1u<<7);std::vector<uint8_t> unaligned(1,0);unaligned.insert(unaligned.end(),three.bytes.begin(),three.bytes.end());
 auto w=TGLTCWiring::fromProperty(unaligned.data()+1,16);assert(w.valid&&w.legacy==0x38&&w.typeC==0x40&&w.excluded==0x80);
 assert(w.legacyEligible(3,0x38)&&w.legacyEligible(4,0x38)&&w.legacyEligible(5,0x38));
 reset();IOService provider{&three};assert(XE::tglLoadTCWiring(&provider));
 auto q=IORegistryEntry::getRegistryRoot()->record;assert(q[7]==0x38&&q[8]==0&&q[9]==0x100);
 assert(!TGLTCWiring::fromProperty(nullptr,16).valid);
 puts("PASS actual immutable wiring loader: absent unknown wiring, present-invalid rejection, strict LE schema, overlap/range/version guards, 24 role/port eligibility and lane calls, 3-port intent vs separate backend mask; no hardware qualification");
}
