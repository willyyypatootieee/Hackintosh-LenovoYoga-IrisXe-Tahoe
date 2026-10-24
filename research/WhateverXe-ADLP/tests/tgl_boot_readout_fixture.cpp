#include <cassert>
#include <cstdio>
#include <map>
#include "kern_tgl_boot_readout.hpp"
struct IO {
 bool admitted=true,held=false,withPower=true,withFuse=true,increment=true;
 unsigned gets=0,drops=0,reads=0;uint32_t deny=0;
 uint32_t counts[4] {1,1,0,0};std::map<uint32_t,uint32_t> regs;
 IO(){regs[0x45404]=0xF;regs[0x42000]=0x07800000;
  for(unsigned p=0;p<3;++p)regs[0x60400+0x1000*p]=0x80000000|((p+4)<<27);
  for(unsigned i=0;i<6;++i){TGLTypeCPort tc;TGLFiaPort f;assert(TGLTypeCPort::resolve(i+3,tc)&&tc.resolveFia(0x10,f));
   regs[tc.ddiBuffer]=0x80000000;regs[f.live]=0x10;regs[f.ready]=3;regs[f.owned]=3;regs[f.laneSelect]=0xff;}}
 bool admissionReady(){return admitted;}
 bool readable(uint32_t r){return r!=deny;}
 uint32_t refs(unsigned i){return counts[i];}
 void nativeGetPG4(){assert(!held);held=true;++gets;if(increment)for(auto &c:counts)++c;if(withPower)regs[0x45404]=0xff;if(!withFuse)regs[0x42000]=0;}
 void nativePutPG4(){assert(held);held=false;++drops;for(auto &c:counts)--c;regs[0x45404]=0xF;}
 uint32_t read(uint32_t r){assert(regs.count(r));++reads;if(r!=0x45404&&r!=0x42000)assert(held && (regs[0x45404]&0x55)==0x55 && (regs[0x42000]&0x07800000)==0x07800000);return regs[r];}
};
int main(){using S=TGLBootReadout::Status;
 {IO io;auto r=TGLBootReadout::capture(io);assert(r.status==S::Success&&r.activeMask==7&&io.gets==1&&io.drops==1&&!r.pgHeld);
  for(unsigned i=0;i<6;++i)assert(r.ports[i].status==4&&r.ports[i].ddi==i+3&&r.ports[i].instance==i/2&&r.ports[i].index==i%2);
  for(unsigned i=0;i<4;++i)assert(r.before[i]==r.after[i]&&r.held[i]==r.before[i]+1);}
 {IO io;io.admitted=false;auto r=TGLBootReadout::capture(io);assert(r.status==S::NotAdmitted&&!io.reads&&!io.gets);}
 {IO io;io.counts[3]=UINT32_MAX;auto r=TGLBootReadout::capture(io);assert(r.status==S::CountsInvalid&&!io.reads&&!io.gets);}
 {IO io;io.increment=false;auto r=TGLBootReadout::capture(io);assert(r.status==S::CountsChanged&&r.pgHeld&&io.gets==1&&!io.drops&&io.reads==1);}
 {IO io;io.withPower=false;auto r=TGLBootReadout::capture(io);assert(r.status==S::PowerNotReady&&io.drops==1&&io.reads==4);}
 {IO io;io.withPower=false;bool consumed=false;auto r=TGLBootReadout::capture(io,[&](const TGLBootReadout::Result &){consumed=true;});assert(!consumed&&r.status==S::PowerNotReady&&io.drops==1);}
 {IO io;io.withFuse=false;auto r=TGLBootReadout::capture(io);assert(r.status==S::PowerNotReady&&io.drops==1&&io.reads==4);}
 {IO io;io.regs[0x45404]=UINT32_MAX;auto r=TGLBootReadout::capture(io);assert(r.status==S::Unavailable&&!io.gets);}
 {IO io;io.deny=0x42000;auto r=TGLBootReadout::capture(io);assert(r.status==S::Unavailable&&!io.gets&&!io.reads);}
 {IO io;io.deny=0x61400;auto r=TGLBootReadout::capture(io);assert(r.status==S::Unavailable&&io.drops==1);}
 {IO io;io.regs[0x62400]=UINT32_MAX;auto r=TGLBootReadout::capture(io);assert(r.status==S::Unavailable&&io.drops==1);}
 {IO io;io.regs[0x1638A0]=UINT32_MAX;auto r=TGLBootReadout::capture(io);assert(r.status==S::Unavailable&&io.drops==1);for(auto &p:r.ports)assert(p.status==3);}
 {IO io;io.regs[0x16E894]=UINT32_MAX;auto r=TGLBootReadout::capture(io);assert(r.ports[2].status==3&&r.ports[3].status==3&&r.ports[4].status==4&&io.drops==1);}
 {IO io;io.deny=0x16F8C0;auto r=TGLBootReadout::capture(io);assert(r.ports[4].status==2&&r.ports[5].status==2&&io.drops==1);}
 {IO io;io.regs[0x1638A0]=0;auto r=TGLBootReadout::capture(io);assert(r.ports[4].status==1&&r.ports[5].status==1&&io.drops==1);}
 puts("PASS powered boot readout: PG4 balance, no unpowered reads, all three pipes and six modular TC ports, uncertain counts retained, allones/bounds/nonmodular unresolved. No idle certification.");
}
