#include "kern_display13_boot.hpp"
#include <map>
#include <vector>
#include <cassert>
#include <cstdio>
struct IO {
    std::map<uint32_t,uint32_t> regs;
    std::vector<AlderLakeN::Register> writes;
    bool noFrames {},noStop {},noIdle {},changePeer {};
    IO() {
        regs[0x45404]=0xFC0F;regs[0x4438C]=0x1C000700;
        regs[0x64000]=regs[0x64100]=0x80000000;
        regs[0x46140]=0x10000000;regs[0x46144]=0x20000000;regs[0x61200]=0x1000;
        for(unsigned p=0;p<2;++p) {
            const auto b=p*0x1000;
            regs[0x60400+b]=p ? 0x90030500 : 0x88030100;
            regs[0x70008+b]=0xC0000000;regs[0x70180+b]=0x84000008;
            regs[0x70188+b]=120;regs[0x70190+b]=0x0437077F;
            regs[0x60000+b]=0x0897077F;regs[0x6000C+b]=0x04640437;
            regs[0x7027C+b]=0x03FF0000;
        }
    }
    bool readable(uint32_t reg) {return reg<0x1000000;}
    uint32_t read(uint32_t reg) {return regs[reg];}
    void write(uint32_t reg,uint32_t value) {
        writes.push_back({reg,value});regs[reg]=value;
        if((reg==0x71008 || reg==0x72008) && !(value&0x80000000) && !noStop)regs[reg]&=~0x40000000;
        if((reg==0x64000 || reg==0x64100 || reg==0x64200) && !(value&0x80000000) && !noIdle)regs[reg]|=0x80;
        if(writes.size()==1)regs[0x4500C]=0;
        if(changePeer && writes.size()==1)regs[0x70180]^=8;
    }
    void delay() {if(!noFrames) {++regs[0x70040];if(regs[0x71008]&0xC0000000)++regs[0x71040];if(regs[0x72008]&0xC0000000)++regs[0x72040];}}
};
const uint32_t prop[] {1,7,1,2,3};
const auto wiring=TGLHDMIWiring::fromProperty(prop,sizeof(prop));
const auto memory=XELPMemory::decode(0x03C1,0x7A800000,0);
int main() {
    IO io;const auto before=io.regs;
    const auto r=Display13Boot::normalize(io,AlderLakeN::readDisplay(io,true),true,wiring,memory);
    assert(r.status==Display13Boot::Status::Complete && r.after==1 && r.stage==7 && r.writes==10);
    assert(!io.regs[0x7127C] && !io.regs[0x7137C] && !io.regs[0x7117C]);
    for(auto w:io.writes)assert(w.offset==0x71180 || w.offset==0x7119C || w.offset==0x71008 ||
        w.offset==0x61404 || w.offset==0x61400 || w.offset==0x64100 || w.offset==0x46144 ||
        w.offset==0x7127C || w.offset==0x7137C || w.offset==0x7117C);
    for(auto pair:before)if(pair.first!=0x70040 && pair.first!=0x46144 && pair.first<0x61000 && pair.first!=0x60404)
        assert(io.regs[pair.first]==pair.second);
    assert(io.regs[0x46140]==0x10000000 && !io.regs[0x46144] && io.regs[0x61200]==0x1000);
    IO frames;frames.noFrames=true;
    assert(Display13Boot::normalize(frames,AlderLakeN::readDisplay(frames,true),true,wiring,memory).status==Display13Boot::Status::Timeout);
    assert(frames.writes.size()==2 && frames.regs[0x7127C]);
    IO stop;stop.noStop=true;
    assert(Display13Boot::normalize(stop,AlderLakeN::readDisplay(stop,true),true,wiring,memory).status==Display13Boot::Status::Timeout);
    assert(stop.writes.size()==3 && stop.regs[0x7127C]);
    IO idle;idle.noIdle=true;
    assert(Display13Boot::normalize(idle,AlderLakeN::readDisplay(idle,true),true,wiring,memory).status==Display13Boot::Status::Timeout);
    assert(idle.writes.size()==6 && idle.regs[0x7127C]);
    IO peer;peer.changePeer=true;
    assert(Display13Boot::normalize(peer,AlderLakeN::readDisplay(peer,true),true,wiring,memory).status==Display13Boot::Status::PeerChanged);
    assert(peer.writes.size()==2 && peer.regs[0x7127C]);
    for(auto reg:{0x71080U,0x71280U,Display13FBIRQ::DSBControl(1,0),0x60404U}) {
        IO foreign;foreign.regs[reg]=0x80000001;
        assert(Display13Boot::normalize(foreign,AlderLakeN::readDisplay(foreign,true),true,wiring,memory).status==Display13Boot::Status::Topology);
        assert(foreign.writes.empty());
    }
    IO port;port.regs[0x61400]=0x88030500;
    assert(Display13Boot::normalize(port,AlderLakeN::readDisplay(port,true),true,wiring,memory).status==Display13Boot::Status::Topology && port.writes.empty());
    IO denied;assert(Display13Boot::normalize(denied,AlderLakeN::readDisplay(denied,true),false,wiring,memory).status==Display13Boot::Status::Unavailable && denied.writes.empty());
    // Three firmware GOPs, all six physical-port/pipe permutations.
    for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b)for(unsigned c=0;c<3;++c) {
        if(a==b || a==c || b==c)continue;
        IO t;unsigned ports[] {a,b,c};
        for(unsigned p=0;p<3;++p) {
            const auto bank=p*0x1000;
            for(auto reg:{0x70008U,0x70180U,0x70188U,0x70190U,0x60000U,0x6000CU,0x7027CU})t.regs[reg+bank]=t.regs[reg];
            t.regs[0x60400+bank]=0x80030100|((ports[p]+1)<<27);
            t.regs[0x46140+4*p]=(ports[p]+1)<<28;
            t.regs[0x64000+0x100*ports[p]]=0x80000000;
        }
        auto n=Display13Boot::normalize(t,AlderLakeN::readDisplay(t,true),true,wiring,memory);
        assert(n.status==Display13Boot::Status::Complete && n.before==7 && n.after==1 && n.writes==20);
        assert(t.regs[0x64000+0x100*a]&0x80000000);
        assert(!t.regs[0x7127C] && !t.regs[0x7227C] && t.regs[0x7027C]);
    }
    for(auto width:{1280U,1920U,2560U,3840U})for(auto height:{720U,1080U,1440U,2160U}) {
        IO t;
        for(unsigned p=0;p<2;++p) {
            const auto bank=p*0x1000;
            t.regs[0x70188+bank]=(width*4+63)/64;
            t.regs[0x70190+bank]=((height-1)<<16)|(width-1);
            t.regs[0x60000+bank]=((width+280-1)<<16)|(width-1);
            t.regs[0x6000C+bank]=((height+45-1)<<16)|(height-1);
        }
        assert(Display13Boot::normalize(t,AlderLakeN::readDisplay(t,true),true,wiring,memory).status==Display13Boot::Status::Complete);
    }
    puts("PASS two-GOP bootstrap: actual B STOP/idle before DDB release, A/shared preservation, bounded failures and foreign topology rejection");
}
