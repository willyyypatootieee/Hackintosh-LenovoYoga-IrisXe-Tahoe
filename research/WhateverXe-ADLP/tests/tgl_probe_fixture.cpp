#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <initializer_list>
#include <sys/mman.h>
#include "kern_tgl_port_map.hpp"
#include "kern_xelp_probe.hpp"
#include "kern_tgl_tc_boot.hpp"
struct IOService;
#define arrsize(a) (sizeof(a)/sizeof((a)[0]))
#define SYSLOG(...)
constexpr uint32_t tglConnectionEvents=7,tglConnectionCapacity=64;
struct TGLConnectionRecord { uint64_t q[32]; };
TGLConnectionRecord tglConnectionFirst[7][64] {},tglConnectionLast[7][64] {};
uint32_t tglConnectionCounts[7] {},tglConnectionSequence {};
int lock,*tglConnectionLock=&lock;
unsigned IOSimpleLockLockDisableInterrupt(int *) { return 0; }
void IOSimpleLockUnlockEnableInterrupt(int *,unsigned) {}
uint32_t tglConnectionCaller(void *) { return 5; }
void clock_get_uptime(uint64_t *p) { *p=42; }
void absolutetime_to_nanoseconds(uint64_t a,uint64_t *p) { *p=a; }
void *current_thread() { return reinterpret_cast<void *>(7); }
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot() { static IORegistryEntry r;return &r; }
 template<class... T> void setProperty(T...) {}
};
void tglAUXRecord(void *,uint32_t,bool,uint32_t,uint32_t,uint32_t) {}
uint32_t providerProbe(void *);
struct XE {
 uint32_t tglModularProbed=0;
 bool tglActive=true;
 void *tglObservationController=nullptr;
 uintptr_t xelpProviderProbe=reinterpret_cast<uintptr_t>(providerProbe);
 bool xelpBindProbe(void*) {return xelpProviderProbe!=0;}
 // TC_WIRING_FIELDS
 static void tglRecordStartOrder(unsigned,void*,unsigned=9,uint64_t=0,uint64_t=0,uint64_t=0) {} static uint32_t wrapTGLProbePort(void *); };
XE object, *callbackXE=&object;
uint32_t providerProbe(void *p) {
 auto port=static_cast<uint8_t *>(p);auto c=*reinterpret_cast<uint32_t **>(port+0x548);
 // The production provider owns classification. Keep this fixture's original
 // hostile-MMIO setup to prove admission still precedes access dereferencing.
 if(c[0]<=2)return 1;
 if(c[0]>=9 || !(object.tglTCProbeMask()&(1U<<c[0])))return 0;
 auto access=*reinterpret_cast<uint8_t **>(port+0x578);if(!access)return 0;
 struct IO {
  uint8_t *access;
  bool readable(uint32_t r){auto n=*reinterpret_cast<uint32_t *>(access+0x60);return *reinterpret_cast<uintptr_t *>(access+0x50) && n>=4 && r<=n-4;}
  uint32_t read(uint32_t r){return *reinterpret_cast<volatile uint32_t *>(*reinterpret_cast<uintptr_t *>(access+0x50)+r);}
 } io {access};
 return XELP::legacyTGLMode(io,c[0],object.tglTCProbeMask());
}

// PRODUCTION_FUNCTIONS
int main(int argc,char **) {
 // This exclusion fixture deliberately restricts the backend to TC2.
 object.tglAUXParentMask=1u<<4;object.tglTCWiring=TGLTCWiring::fromMasks(1u<<4,0,0);
 constexpr size_t size=0x170000;
 auto mmio=(uint8_t*)mmap(nullptr,size,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(mmio!=MAP_FAILED);
 assert(mprotect(mmio,0xD0000,PROT_READ|PROT_WRITE)==0);
 alignas(8) uint8_t port[0x1000] {},access[0x80] {};
 uint32_t config[8] {4};
 *reinterpret_cast<uint32_t **>(port+0x548)=config;
 *reinterpret_cast<uint8_t **>(port+0x578)=access;
 *reinterpret_cast<uint8_t **>(access+0x50)=mmio;
 auto &length=*reinterpret_cast<uint32_t *>(access+0x60);length=size;
 auto &hpd=*reinterpret_cast<uint32_t *>(mmio+0xC4000);hpd=0;
 if(argc>1) tglConnectionLock=nullptr;
 // HPD is off, FIA/pipe areas inaccessible. The real observer must not
 // bypass the probe guard. Run baseline with observer both on and off.
 assert(XE::wrapTGLProbePort(port)==0);
 tglConnectionLock=&lock;
 hpd=0xffffffff;assert(XE::wrapTGLProbePort(port)==0);
 // An inactive/unqualified backend may not dereference its MMIO manager.
 *reinterpret_cast<uint8_t **>(port+0x578)=mmio+0x160000;
 for(unsigned d:{0u,1u,2u,3u,5u,6u,7u,8u,9u}) {
  config[0]=d;assert(XE::wrapTGLProbePort(port)==(d<=2?1u:0u));
 }
 config[0]=4;
 // An explicit non-legacy/Unknown/excluded TC2 must not reach MMIO even
 // though TC2 remains a qualified backend. Unsupported legacy intent also
 // cannot enable another backend through a mask alone.
 for(auto wiring:{TGLTCWiring::fromMasks(0,0,0),TGLTCWiring::fromMasks(0,1u<<4,0),
                  TGLTCWiring::fromMasks(0,0,1u<<4),TGLTCWiring::fromMasks(1u<<3,0,0)}) {
  object.tglTCWiring=wiring;assert(XE::wrapTGLProbePort(port)==0);
  config[0]=3;assert(XE::wrapTGLProbePort(port)==0);config[0]=4;
 }
 object.tglTCWiring=TGLTCWiring::fromMasks(1u<<4,0,0);
 config[0]=4;*reinterpret_cast<uint8_t **>(port+0x578)=access;
 assert(mprotect(mmio,size,PROT_READ|PROT_WRITE)==0);
 auto &live=*reinterpret_cast<uint32_t *>(mmio+0x1638A0);
 auto &ready=*reinterpret_cast<uint32_t *>(mmio+0x163890);
 unsigned cases=0;
 for(unsigned hp:{0u,1u})for(unsigned modular:{0u,1u})for(unsigned rd:{0u,1u})for(unsigned alt=0;alt<4;++alt) {
  hpd=hp<<25;live=(modular<<4)|(alt<<13);ready=rd<<1;
  const auto expected=hp&&modular&&rd&&!alt?4u:0u;
  assert(XE::wrapTGLProbePort(port)==expected);++cases;
 }
 hpd=1u<<25;live=0x10;ready=2;length=0x1638A4;
 assert(XE::wrapTGLProbePort(port)==4); // exact final word included
 for(unsigned n:{0u,0xC4000u,0xC4003u,0x1638A3u}) {length=n;assert(XE::wrapTGLProbePort(port)==0);}
 length=size;live=~0u;assert(XE::wrapTGLProbePort(port)==0);
 live=0x10;ready=~0u;assert(XE::wrapTGLProbePort(port)==0);
 // All seven observer events remain memory-only even with no usable map.
 *reinterpret_cast<uint8_t **>(port+0x578)=mmio;
 assert(mprotect(mmio,size,PROT_NONE)==0);
 for(unsigned e=0;e<7;++e)for(bool done:{false,true}) {
  tglConnectionRecord(port,e,done,3,4,nullptr);
  const auto &q=tglConnectionLast[e][(tglConnectionCounts[e]-1)%64].q;
  assert(q[0]==42 && q[1]==7 && q[3]==e && q[4]==done && q[5]==3 && q[6]==4 && q[9]==4);
  assert(q[23]==0x4D454D31);for(unsigned i=18;i<=22;++i)assert(q[i]==0);
 }
 // Pure classification accepts the matching HPD/ready fields only, across
 // modular and representable non-modular FIA geometries. No MMIO here.
 unsigned geometries=0;
 for(unsigned tc=0;tc<6;++tc)for(bool modular:{false,true}) {
  TGLTypeCPort m;TGLFiaPort f;assert(TGLTypeCPort::resolve(tc+3,m));
  assert(m.legacyHPD(1u<<(24+tc)));assert(!m.legacyHPD(~0u));
  assert(!m.legacyHPD(1u<<(24+(tc+1)%6)));
  if(!m.resolveFia(modular?0x10:0,f)) {assert(!modular && tc>=4);continue;}
  auto idx=modular?tc%2:tc;
  assert(f.readyForLegacy(0,1u<<idx));
  assert(!f.readyForLegacy(0,0));
  assert(!f.readyForLegacy(0x20u<<(8*idx),1u<<idx));
  assert(!f.readyForLegacy(0x40u<<(8*idx),1u<<idx));
  assert(!f.readyForLegacy(~0u,1u<<idx));assert(!f.readyForLegacy(0,~0u));++geometries;
 }
 // Automatic candidates pass the exact same provider probe; only a
 // successful mode4 result registers backend hooks. Disconnect keeps the
 // registration for AUX cleanup, but never forces the next probe's mode.
 assert(mprotect(mmio,size,PROT_READ|PROT_WRITE)==0);
 *reinterpret_cast<uint8_t **>(port+0x578)=access;
 object.tglTCWiring=TGLTCWiring::fromMasks(0,0,0);object.tglTCAutoCandidates=0x10;
 object.tglTCAutoRegistered=0;object.tglAUXParentMask=0x10;config[0]=4;
 length=size;hpd=0;live=0x10;ready=2;
 assert(XE::wrapTGLProbePort(port)==0&&!object.tglLegacyTCEligible(4));
 hpd=1u<<25;live=0x2010;
 assert(XE::wrapTGLProbePort(port)==0&&!object.tglLegacyTCEligible(4));
 live=0x10;assert(XE::wrapTGLProbePort(port)==4&&object.tglTCAutoRegistered==0x10);
 assert(object.tglLegacyTCEligible(4)&&object.tglTCModeAllowed(4,4));
 hpd=0;assert(XE::wrapTGLProbePort(port)==0&&object.tglLegacyTCEligible(4));
 hpd=1u<<25;live=0x6010;
 assert(XE::wrapTGLProbePort(port)==0&&!object.tglTCModeAllowed(4,3));
 assert(object.tglTCModeAllowed(4,0));
 assert(XE::wrapTGLProbePort(nullptr)==0);
 object.tglActive=false;assert(XE::wrapTGLProbePort(mmio)==0);object.tglActive=true;
 *reinterpret_cast<uint32_t **>(port+0x548)=nullptr;assert(XE::wrapTGLProbePort(port)==0);
 munmap(mmio,size);
 printf("PASS: real probe with protected inactive FIA, 32 TC2 states, boundary/invalid guards; 14 real memory-only observer records; %u FIA classification geometries; qualification unchanged\n",geometries);
 assert(cases==32);
}
