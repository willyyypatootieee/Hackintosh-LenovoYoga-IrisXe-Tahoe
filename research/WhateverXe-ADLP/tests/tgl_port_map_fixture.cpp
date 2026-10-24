#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include "kern_tgl_port_map.hpp"
#include "kern_tgl_tc_boot.hpp"
struct IOService;
bool quiet;
bool tglQuietTraining() { return quiet; }
using IOSimpleLock = int;
unsigned IOSimpleLockLockDisableInterrupt(IOSimpleLock *) { return 0; }
void IOSimpleLockUnlockEnableInterrupt(IOSimpleLock *, unsigned) {}
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot() { static IORegistryEntry r; return &r; }
 template<class... T> void setProperty(T...) {}
 void *getProperty(const char *) { return nullptr; }
};
struct XE {
 // FIELDS
 IOSimpleLock *tglDekelLock;
 uintptr_t orgTGLReadDekel,orgTGLWriteDekel;
 static bool tglDekelAccess(void *,uint32_t,bool,uint32_t &);
 static bool tglDekelAccessOld(void *,uint32_t,bool,uint32_t &);
 static uint32_t wrapTGLReadDekel(void *,uint32_t);
 static void wrapTGLWriteDekel(void *,uint32_t,uint32_t);
};
XE object, *callbackXE=&object;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
// PRODUCTION_ACCESS

unsigned nativeReads,nativeWrites;
uint32_t nativeRead(void *,uint32_t) { ++nativeReads; return 0x12345678; }
void nativeWrite(void *,uint32_t,uint32_t) { ++nativeWrites; }
struct PortFixture {
 alignas(8) uint8_t port[0x1000] {}, access[0x80] {};
 uint32_t config[8] {};
 std::vector<uint32_t> mmio;
 PortFixture(uint32_t ddi): mmio(0x170000/4,0xA5FABCDF) {
  config[0]=ddi;
  *reinterpret_cast<uint32_t **>(port+0x548)=config;
  *reinterpret_cast<uint32_t *>(port+0x550)=4;
  *reinterpret_cast<uint8_t **>(port+0x578)=access;
  *reinterpret_cast<uint32_t **>(access+0x50)=mmio.data();
  *reinterpret_cast<uint32_t *>(access+0x60)=mmio.size()*4;
  *reinterpret_cast<uint32_t *>(port+0x128)=20;
  const uint32_t raw[]={0x20D4,0x20D8,0x2130,0x212C,0x46030,0x2200,0x2204,0x2208,
   0x168210,0x168214,0x168218,0x220C,0x2244,0x224C,0x2C0,0x2C4,0x2C8,0x12C0,0x12C4,0x12C8,0xA0,0x10A0};
  for(unsigned i=0;i<22;++i) *reinterpret_cast<uint32_t *>(port+0x580+(0x34+i)*4)=raw[i];
 }
};
int main() {
 int lock;object.tglDekelLock=&lock;
 object.orgTGLReadDekel=(uintptr_t)nativeRead;object.orgTGLWriteDekel=(uintptr_t)nativeWrite;
 // Independent literal coordinates from the Linux v6.12 register definitions.
 const uint32_t phy[]={0x168000,0x169000,0x16A000,0x16B000,0x16C000,0x16D000};
 const uint32_t hip[]={0x1010A0,0x1010A0,0x1010A0,0x1010A0,0x1010A4,0x1010A4};
 const uint32_t pll[]={0x46030,0x46034,0x46038,0x4603C,0x46040,0x46044};
 const uint32_t shifts[]={0,8,16,24,0,8};
 const uint32_t fiaBases[]={0x163000,0x16E000,0x16F000};
 const uint32_t internal[]={0x20D4,0x20D8,0x2130,0x212C,0,0x2200,0x2204,0x2208,
  0x2210,0x2214,0x2218,0x220C,0x2244,0x224C,0x2C0,0x2C4,0x2C8,0x12C0,0x12C4,0x12C8,0xA0,0x10A0};
 unsigned accesses=0,comparisons=0;
 for(unsigned tc=0;tc<6;++tc) {
  TGLTypeCPort m;assert(TGLTypeCPort::resolve(tc+3,m));
  assert(m.phyBase==phy[tc] && m.hipRegister==hip[tc] && m.hipShift==shifts[tc] && m.pllEnable==pll[tc]);
  TGLFiaPort f;assert(m.resolveFia(0x10,f));
  assert(f.instance==tc/2 && f.index==tc%2 && f.live==fiaBases[tc/2]+0x8A0);
  assert(f.ready==fiaBases[tc/2]+0x890 && f.owned==fiaBases[tc/2]+0x894 && f.laneSelect==fiaBases[tc/2]+0x8C0);
  assert(f.readyMask==(1u<<(tc%2)) && f.altMask==(0x60u<<(8*(tc%2))));
  assert(f.laneSelectMask==(0xFu<<(4*(tc%2))) && f.laneSelectShift==4*(tc%2));
  assert(m.resolveFia(0,f)==(tc<4));
  if(tc<4) assert(f.instance==0 && f.index==tc && f.live==0x1638A0 && f.altMask==(0x60u<<(8*tc)));
  assert(!m.resolveFia(0xffffffff,f));
  for(bool q:{false,true}) for(unsigned rate:{6u,10u,20u}) for(unsigned i=0;i<22;++i) for(bool write:{false,true}) {
   quiet=q;PortFixture a(tc+3);
   *reinterpret_cast<uint32_t *>(a.port+0x128)=rate;
   auto before=a.mmio;uint32_t value=0x12500;
   assert(XE::tglDekelAccess(a.port,0x34+i,write,value));
   const auto offset=i==4?pll[tc]:phy[tc]+(internal[i]&0xFFF);
   const auto hipExpected=(0xA5FABCDFu&~(0xFu<<shifts[tc]))|(((internal[i]>>12)&0xF)<<shifts[tc]);
   assert(a.mmio[hip[tc]/4]==(i==4?0xA5FABCDFu:hipExpected));
   if(write) assert(a.mmio[offset/4]==value); else assert(value==0xA5FABCDFu);
   // All other words must be unchanged except the selected PHY register,
   // HIP selector, and PMD lane resume at signal writes.
   const bool signal=write && (i==14||i==15||i==17||i==18);
   for(unsigned j=0;j<a.mmio.size();++j) {
    if(j==offset/4 || j==hip[tc]/4 || (signal && j==(phy[tc]+0xD00)/4)) continue;
    assert(a.mmio[j]==before[j]);
   }
   if(signal) assert(a.mmio[(phy[tc]+0xD00)/4]==0);
   ++accesses;
   if(tc==1) {
    PortFixture b(4);*reinterpret_cast<uint32_t *>(b.port+0x128)=rate;
    uint32_t oldValue=0x12500;
    assert(XE::tglDekelAccessOld(b.port,0x34+i,write,oldValue));
    assert(value==oldValue && a.mmio==b.mmio);++comparisons;
   }
  }
 }
 // Public route admission is intentionally unchanged: other ports still
 // call their original donor functions, even though coordinate math exists.
 for(unsigned ddi:{0u,1u,2u,3u,5u,6u,7u,8u,9u,~0u}) {
  PortFixture p(ddi);auto before=p.mmio;auto r=nativeReads,w=nativeWrites;
  assert(XE::wrapTGLReadDekel(p.port,0x34)==0x12345678);
  XE::wrapTGLWriteDekel(p.port,0x34,7);
  assert(nativeReads==r+1 && nativeWrites==w+1 && p.mmio==before);
 }
 // All six public routes, admitted only inside this fixture, must reach
 // their own actual PHY coordinates. Production's backend stays TC2.
 object.tglAUXParentMask=0x1F8;object.tglTCWiring.legacy=0x1F8;
 for(unsigned ddi=3;ddi<=8;++ddi) {
  PortFixture p(ddi);auto r=nativeReads,w=nativeWrites;
  assert(XE::wrapTGLReadDekel(p.port,0x34)==0xA5FABCDF);
  XE::wrapTGLWriteDekel(p.port,0x34,7);
  assert(p.mmio[(0x168000+0x1000*(ddi-3)+0xD4)/4]==7);
  assert(nativeReads==r&&nativeWrites==w);
  // Explicitly excluded/unknown TC2 now uses native just like any other DDI.
  object.tglTCWiring.legacy&=~(1u<<ddi);
  assert(XE::wrapTGLReadDekel(p.port,0x34)==0x12345678);
  XE::wrapTGLWriteDekel(p.port,0x34,9);
  assert(nativeReads==r+1&&nativeWrites==w+1);
  object.tglTCWiring.legacy|=1u<<ddi;
 }
 PortFixture p(4);uint32_t value=7;auto before=p.mmio;
 assert(!XE::tglDekelAccess(nullptr,0x34,true,value));
 for(unsigned ddi:{0u,1u,2u,9u,~0u}) {TGLTypeCPort m;assert(!TGLTypeCPort::resolve(ddi,m));}
 assert(!XE::tglDekelAccess(p.port,0x33,true,value));
 assert(!XE::tglDekelAccess(p.port,0x4A,true,value));
 *reinterpret_cast<uint32_t *>(p.port+0x580+0x34*4)=0;
 assert(!XE::tglDekelAccess(p.port,0x34,true,value) && p.mmio==before);
 *reinterpret_cast<uint32_t *>(p.port+0x580+0x34*4)=0x20D4;
 *reinterpret_cast<uint32_t *>(p.access+0x60)=0x169FFF;
 assert(!XE::tglDekelAccess(p.port,0x34,true,value) && p.mmio==before);
 printf("PASS: %u actual DKL accesses across 6 TC coordinates; %u TC2 old/new complete MMIO matches; FIA topologies and admission guards\n",accesses,comparisons);
}
