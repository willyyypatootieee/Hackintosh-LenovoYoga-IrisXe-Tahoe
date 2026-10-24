#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "kern_tgl_port_map.hpp"
#define arrsize(a) (sizeof(a)/sizeof(a[0]))
bool quiet=false,eligible=true,locked=false;unsigned locks=0;
bool tglQuietTraining(){return quiet;}
unsigned IOSimpleLockLockDisableInterrupt(void*){assert(!locked);locked=true;++locks;return 0;}
void IOSimpleLockUnlockEnableInterrupt(void*,unsigned){assert(locked);locked=false;}
std::map<std::string,std::vector<uint32_t>> published;
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char *key,const void *p,size_t n){assert(!locked);auto b=static_cast<const uint32_t*>(p);published[key]={b,b+n/4};}
};
struct XE {
 void *tglObservationController=nullptr,*tglDekelLock=(void*)1;
 bool tglLegacyTCEligible(uint32_t d){return eligible&&d>=3&&d<=8;}
 static void tglObserveActivation(void*,void*,void*,unsigned,bool,unsigned){}
 static void tglSnapshotTransmitter(void*,bool,uint32_t);
 static void tglSnapshotTransmitterOld(void*,bool,uint32_t);
};
XE obj,*callbackXE=&obj;
// FUNCTIONS
alignas(8) uint8_t port[0x600],manager[0x80];uint32_t config[6],mmio[0x170000/4];
template<class T>void field(void*p,unsigned n,T v){memcpy(static_cast<uint8_t*>(p)+n,&v,sizeof(v));}
uint32_t &reg(unsigned n){return mmio[n/4];}
void setup(unsigned ddi,bool modular){
 memset(port,0,sizeof(port));memset(manager,0,sizeof(manager));memset(config,0,sizeof(config));
 for(unsigned i=0;i<arrsize(mmio);++i)mmio[i]=i^0xa1230000;
 reg(0x1638A0)=modular?0x10:0;reg(0x1010A0)=0x76543210;reg(0x1010A4)=0xabcdef01;
 config[0]=ddi;config[2]=1;field(port,0x548,config);field(port,0x578,manager);field(port,0x550,uint32_t(4));
 field(manager,0x50,mmio);field(manager,0x60,uint32_t(sizeof(mmio)));
 published.clear();eligible=true;quiet=false;locks=0;
}
std::vector<uint32_t> record(const char *stem){for(auto &v:published)if(v.first.find(stem)==0)return v.second;assert(false);return {};}
int main(){
 // TC2's legacy record layout, values and keys are preserved byte-for-byte except sequence.
 setup(4,true);XE::tglSnapshotTransmitterOld(port,false,7);auto before=published;
 published.clear();XE::tglSnapshotTransmitter(port,false,7);
 for(auto &v:before){auto n=published.at(v.first);auto old=v.second;n[0]=old[0];assert(n==old);}
 unsigned cases=0;
 for(unsigned ddi=3;ddi<=8;++ddi)for(bool modular:{false,true}){
  setup(ddi,modular);TGLTypeCPort map;assert(TGLTypeCPort::resolve(ddi,map));TGLFiaPort fia;
  const bool available=map.resolveFia(reg(0x1638A0),fia);
  XE::tglSnapshotTransmitter(port,true,9);
  assert(reg(0x1010A0)==0x76543210&&reg(0x1010A4)==0xabcdef01&&!locked);
  if(!available){assert(published.empty()&&locks==0);continue;}
  assert(locks==1);auto key="TGLTransmitter"+std::to_string(ddi)+"Exit";auto q=published.at(key);
  assert(q[10]==map.ddiBuffer&&q[12]==fia.laneSelect&&q[14]==map.pllEnable&&q[20]==fia.owned);
  for(unsigned i=10;i<34;i+=2)assert(q[i+1]==reg(q[i]));
  auto phy=record(("TGLTransmitterPHY"+std::to_string(ddi)+"Last").c_str());
  for(unsigned i=3;i<33;i+=2)assert(phy[i+1]==reg(map.phyRegister(phy[i])));
  ++cases;
 }
 for(unsigned fault=0;fault<5;++fault){setup(8,true);
  if(fault==0)eligible=false;if(fault==1)quiet=true;
  if(fault==2)reg(0x1638A0)=~0u;
  if(fault==3)field(manager,0x60,uint32_t(0x16f894));
  if(fault==4)field(manager,0x60,uint32_t(0x169fff));
  XE::tglSnapshotTransmitter(port,false,0);assert(published.empty()&&locks==0);
 }
 puts("PASS actual PHY snapshot: TC2 old/new record parity; 10 modular/nonmodular port mappings, both HIP registers restored, no publication under lock, missing-FIA/map/wiring/quiet guards");
}
