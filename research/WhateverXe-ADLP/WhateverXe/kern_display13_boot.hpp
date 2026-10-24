// Combo multi-GOP bootstrap: preserve pipe A, park other GOPs before native
// ownership exists. This does not pretend both GOPs were adopted by the donor.
#ifndef KERN_DISPLAY13_BOOT_HPP
#define KERN_DISPLAY13_BOOT_HPP
#include "kern_alderlake_n.hpp"
#include "kern_display13_fb_irq.hpp"
#include "kern_display13_fb_topology.hpp"
#include "kern_xelp_memory_layout.hpp"
namespace Display13Boot {
enum class Status : uint32_t {Unavailable,Complete,Topology,Readback,Timeout,PeerChanged};
struct Result {
    uint32_t version {1};Status status {Status::Unavailable};
    uint32_t stage {},writes {},before {},after {},lastReg {},expected {},observed {},framesBefore {},framesAfter {};
};
template<class IO> Result normalize(IO &io,const AlderLakeN::DisplayReadout &boot,bool admitted,
                                    const Display13HDMIWiring &w,const XELPMemory::Layout &memory) {
    Result r;r.before=boot.activePipes;
    if(!admitted)return r;
    r.status=Status::Topology;
    const auto active=boot.activePipes;
    if(!memory.valid || !Display13FBTopology::multiOutputWiring(w) || boot.status!=AlderLakeN::ReadoutStatus::Complete ||
       !(active&1) || active==1 || (active&~7U) || (active&~boot.poweredPipes))return r;
    uint32_t ports[3] {},clocks[3] {},used=0;
    // Every active firmware pipe must be a distinct selected linear TMDS GOP.
    // Pipe identity and physical DDI are independent. No foreign cursor/plane/DSB.
    for(unsigned p=0;p<3;++p)if(boot.poweredPipes&(1U<<p)) {
        const auto bank=p*0x1000;uint32_t v=0;
        if(!AlderLakeN::value(boot,0x70080+bank,v) || (v&63))return r;
        for(unsigned n=(active&(1U<<p)) ? 1 : 0;n<5;++n)
            if(!AlderLakeN::value(boot,0x70180+bank+n*0x100,v) || (v&0x80000000))return r;
        for(unsigned d=0;d<3;++d) {
            const auto reg=Display13FBIRQ::DSBControl(p,d);
            if(!io.readable(reg) || (io.read(reg)&0x80000001))return r;
        }
        if(!(active&(1U<<p))) {
            if(!AlderLakeN::value(boot,0x70008+bank,v) || (v&0xC0000000))return r;
            continue;
        }
        uint32_t ctl=0,trans=0,surface=0,live=0;
        if(!AlderLakeN::value(boot,0x70180+bank,ctl) || !AlderLakeN::value(boot,0x60400+bank,trans))return r;
        const auto port=Display13FBTopology::bootDDI(w,trans);
        if(port>=3 || (used&(1U<<port)))return r;
        ports[p]=port;used|=1U<<port;
        uint32_t stride=0,size=0,position=0,offset=0,ht=0,vt=0,conf=0;
        if(((trans>>20)&7) || !AlderLakeN::value(boot,0x70188+bank,stride) ||
           !AlderLakeN::value(boot,0x70190+bank,size) || !AlderLakeN::value(boot,0x7018C+bank,position) ||
           !AlderLakeN::value(boot,0x701A4+bank,offset) || !AlderLakeN::value(boot,0x60000+bank,ht) ||
           !AlderLakeN::value(boot,0x6000C+bank,vt) || !AlderLakeN::value(boot,0x70008+bank,conf) ||
           (conf&0xC0000000)!=0xC0000000 || !AlderLakeN::value(boot,0x7019C+bank,surface) ||
           (surface&4095) || !AlderLakeN::value(boot,0x701AC+bank,live) || live!=surface)return r;
        const auto geometry=XELPGeometry::linear(ctl,stride,size,position,offset);
        if(!XELPGeometry::timing(geometry,ht,vt) || uint64_t(surface)+geometry.bytes>memory.usable())return r;
        const auto clock=0x46140+4*p,buf=0x64000+0x100*port;
        if(!io.readable(clock) || !io.readable(buf) || !(io.read(buf)&0x80000000) ||
           !io.readable(0x60404+bank) || io.read(0x60404+bank))return r;
        clocks[p]=io.read(clock);
        if(clocks[p]==UINT32_MAX || (clocks[p]>>28)!=port+1)return r;
    }
    for(unsigned i=0;i<boot.count;++i) {
        const auto pair=boot.registers[i];
        if(pair.offset==0x70040 || pair.offset==0x71040 || pair.offset==0x72040)continue;
        if(io.read(pair.offset)!=pair.value) {r.status=Status::Readback;return r;}
    }
    uint32_t parked=0,current=0;
    const auto peer=[&] {
        for(unsigned p=0;p<3;++p)if((active&(1U<<p)) && !(parked&(1U<<p)) && p!=current)
            if(io.read(0x46140+4*p)!=clocks[p])return false;
        for(unsigned i=0;i<boot.count;++i) {
            const auto pair=boot.registers[i];const auto reg=pair.offset;
            // 4500C is a diagnostic observation that changes when the firmware
            // plane drains; it is not a shared resource configuration certificate.
            // Preserve actual bank/DDI and known shared control registers.
            bool mutableReg=reg==0x4500C;
            for(unsigned p=1;p<3;++p)if((active&(1U<<p)) && (p==current || (parked&(1U<<p)))) {
                const auto bank=0x1000*p;
                if((reg>=0x60000+bank && reg<0x61000+bank) ||
                   (reg>=0x70000+bank && reg<0x71000+bank) || reg==0x64000+0x100*ports[p])mutableReg=true;
            }
            if(reg==0x70040 || reg==0x71040 || reg==0x72040)continue;
            if(!mutableReg && io.read(reg)!=pair.value) {
                r.lastReg=reg;r.expected=pair.value;r.observed=io.read(reg);return false;
            }
        }
        return true;
    };
    const auto write=[&](uint32_t reg,uint32_t value) {io.write(reg,value);++r.writes;io.read(reg);};
    const auto wait=[&](uint32_t reg,uint32_t mask,uint32_t expected) {
        r.lastReg=reg;r.expected=expected;
        for(unsigned n=0;n<1000;++n) {
            r.observed=io.read(reg);
            if(r.observed==UINT32_MAX || !peer()) {r.status=Status::PeerChanged;return false;}
            if((r.observed&mask)==expected)return true;
            io.delay();
        }
        r.status=Status::Timeout;return false;
    };
    for(unsigned p=1;p<3;++p)if(active&(1U<<p)) {
        current=p;const auto bank=p*0x1000,bufReg=0x64000+0x100*ports[p];
        uint32_t plane=0,surface=0,conf=0,trans=0,buf=0;
        AlderLakeN::value(boot,0x70180+bank,plane);AlderLakeN::value(boot,0x7019C+bank,surface);
        AlderLakeN::value(boot,0x70008+bank,conf);AlderLakeN::value(boot,0x60400+bank,trans);buf=io.read(bufReg);
        r.stage=1;r.framesBefore=io.read(0x70040+bank);
        write(0x70180+bank,plane&~0x80000000U);write(0x7019C+bank,surface);
        for(unsigned n=0;n<1000;++n) {
            r.framesAfter=io.read(0x70040+bank);
            if(r.framesAfter==UINT32_MAX || !peer()) {r.status=Status::PeerChanged;return r;}
            if(uint32_t(r.framesAfter-r.framesBefore)>=2 && uint32_t(r.framesAfter-r.framesBefore)<0x10000000)break;
            if(n==999) {r.status=Status::Timeout;return r;}io.delay();
        }
        if(!wait(0x70180+bank,0x80000000,0))return r;
        // Real frame latch, STOP_ACK and buffer idle precede DDB release.
        // Shared PLL/PHY/PW and predecessor mapping remain held.
        r.stage=2;write(0x70008+bank,conf&~0x80000000U);
        if(!wait(0x70008+bank,0xC0000000,0))return r;
        r.stage=3;write(0x60404+bank,0);write(0x60400+bank,trans&~(0x80000000U|(15U<<27)|(7U<<24)));
        if(!wait(0x60400+bank,0x80000000,0))return r;
        r.stage=4;write(bufReg,buf&~0x80000000U);
        if(!wait(bufReg,0x80000080,0x80))return r;
        r.stage=5;write(0x46140+4*p,clocks[p]&0x0FFFFFFF);
        if(!wait(0x46140+4*p,UINT32_MAX,clocks[p]&0x0FFFFFFF))return r;
        if(!Display13FBIRQ::consumersStopped(io,1U<<p)) {r.status=Status::Readback;return r;}
        r.stage=6;write(0x7027C+bank,0);write(0x7037C+bank,0);write(0x7017C+bank,0);
        if(!wait(0x7027C+bank,UINT32_MAX,0) || !wait(0x7037C+bank,UINT32_MAX,0) || !wait(0x7017C+bank,UINT32_MAX,0))return r;
        parked|=1U<<p;
    }
    current=UINT32_MAX;
    const auto after=AlderLakeN::readDisplay(io,true);r.after=after.activePipes;
    if(after.status!=AlderLakeN::ReadoutStatus::Complete || after.activePipes!=1 || !peer()) {r.status=Status::PeerChanged;return r;}
    r.stage=7;r.status=Status::Complete;return r;
}
}
#endif
