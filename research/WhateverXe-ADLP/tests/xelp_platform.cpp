#include "kern_xelp_probe.hpp"
#include "kern_xelp_donor.hpp"
#include "kern_tgl_fb_binding.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace XELP;
struct IO {
 std::map<uint32_t,uint32_t> regs;std::vector<uint32_t> reads;
 bool readable(uint32_t r){return regs.count(r);}
 uint32_t read(uint32_t r){reads.push_back(r);return regs.at(r);}
};
int main() {
 for(uint32_t id=0;id<65536;++id)for(unsigned rev:{0U,1U,255U}) {
  const auto p=identify((id<<16)|0x8086,rev);
   const bool tgl=id==0x9A40 || id==0x9A49 || id==0x9A78,adln=id>=0x46D0 && id<=0x46D4,
       adlp=id==0x46A6 && rev==0x0c;
   assert(p.admitted()==(tgl || (adln && rev==0) || adlp));
  assert(!adln || p.display==Display::XeLPD);
  assert(!identify((id<<16)|0x1234,rev).admitted());
 }
 alignas(8) uint8_t platform[0x1C0] {};
 ProbeConfig c;c.profile=identify(0x9A498086,1);c.combo=2;c.comboBackend=7;c.legacyBackend=0x1F8;c.legacy=0x1F8;
 ProbeBinding b {1,2,reinterpret_cast<uintptr_t>(platform),c};assert(b.valid());
 unsigned cases=0;
 // Independent old WEG equations, plus exact MMIO read order for every TC.
 for(unsigned d=3;d<=8;++d)for(unsigned h=0;h<2;++h)for(unsigned mod=0;mod<2;++mod)
 for(unsigned ready=0;ready<2;++ready)for(unsigned alt=0;alt<4;++alt) {
  const unsigned tc=d-3,instance=tc/2,index=tc%2;const uint32_t bases[]={0x163000,0x16E000,0x16F000};
  const auto liveReg=bases[instance]+0x8A0,readyReg=bases[instance]+0x890;
  IO io;io.regs={{0xC4000,h<<(24+tc)},{0x1638A0,mod<<4},{liveReg,(mod<<4)|(alt<<(5+8*index))},{readyReg,ready<<index}};
  // For FIA1 the per-port fields share its topology word.
  io.regs[liveReg]=(mod<<4)|(alt<<(5+8*index));
  io.regs[0x1638A0]|=mod<<4;
  auto record=reinterpret_cast<uint32_t *>(platform+0x20+24*d);record[0]=d;
  const auto mode=probeMode(io,b,2,reinterpret_cast<uintptr_t>(record),record);
  assert(mode==(h && mod && ready && !alt?4U:0U));
  std::vector<uint32_t> expected {0xC4000};
  if(h){expected.push_back(0x1638A0);if(mod){if(instance)expected.push_back(liveReg);expected.push_back(readyReg);}}
  assert(io.reads==expected);++cases;
 }
 // Selection never grants an unknown, Type-C or excluded port access.
 c.legacy=0;c.typeC=1U<<4;b.config=c;IO io;
 uint32_t *rec=reinterpret_cast<uint32_t *>(platform+0x20+24*4);rec[0]=4;
 assert(!probeMode(io,b,2,reinterpret_cast<uintptr_t>(rec),rec) && io.reads.empty());
 for(unsigned bad=0;bad<5;++bad){auto x=c;if(bad==0)x.abi=1;if(bad==1)x.bytes--;
  if(bad==2)x.profile.display=Display::XeLPD;if(bad==3)x.legacy=x.typeC;if(bad==4)x.legacyBackend=1U<<9;
  assert(!x.valid());}
  c={};c.profile=identify(0x46D18086,0);c.combo=3;c.comboBackend=3;b.config=c;
 for(unsigned d=0;d<9;++d){auto r=reinterpret_cast<uint32_t *>(platform+0x20+24*d);
  const uint32_t v[]={d,d+1,2,0,0x800,0x202};__builtin_memcpy(r,v,sizeof(v));
  assert(probeMode(io,b,2,reinterpret_cast<uintptr_t>(r),r)==(d<2?1U:0U));}
  assert(io.reads.empty());auto other=b;other.access=3;assert(!b.sameBinding(other));assert(b.sameBinding(b));
  c={};c.profile=identify(0x46A68086,0x0c);c.internal=1;b.config=c;
  auto panel=reinterpret_cast<uint32_t *>(platform+0x20);const uint32_t panelRecord[]={0,0,0,0,2,0x18};
  __builtin_memcpy(panel,panelRecord,sizeof(panelRecord));
  assert(probeMode(io,b,2,reinterpret_cast<uintptr_t>(panel),panel)==1);
  panel[4]=0x800;assert(!probeMode(io,b,2,reinterpret_cast<uintptr_t>(panel),panel));panel[4]=2;
  c.combo=1;assert(!c.valid());c.combo=0;c.internal=2;assert(!c.valid());
 // CPU association and power-consumer accounting are independent of transport.
 NativePorts ports {platform,(1U<<0)|(1U<<4)|(1U<<6)};
 assert(ports.bound(platform+0x20+24*4,4));assert(!ports.bound(platform+0x20+24*3,4));
 TGLFBBinding::PublishedPath paths[3];uintptr_t owners[]={1,2,3};unsigned dd[]={6,0,4};
 for(unsigned p=0;p<3;++p){TGLFBBinding::Readout r {TGLFBBinding::Status::Complete,2-p,p,dd[p],5*p};
  assert(TGLFBBinding::publish(paths[p],owners[p],10+p,r,p));}
 auto map=TGLFBBinding::powerPipes(paths,owners,ports.selected);
 assert(map.valid && map.known==ports.selected && map.mask[6]==1 && map.mask[0]==2 && map.mask[4]==4);
 owners[1]=0;paths[1]={};map=TGLFBBinding::powerPipes(paths,owners,ports.selected);
 assert(map.valid && map.mask[0]==7 && map.mask[6]==1 && map.mask[4]==4);
 for(uint32_t n:{0U,1U,4095U,4096U,0x4000000U,0x4001000U,UINT32_MAX})
  assert(XELPDonor::displayPoolRequest(1,n)==(n && !(n&4095) && n<=0x4000000));
 puts("PASS exhaustive PCI/revision admission, typed ABI/profile rejection, TGL six-port result/read-order parity, no Type-C reads, ADL-N Combo parity, three-pipe mixed-port publication/retirement.");
}
