#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include "kern_display13_tc_aux.hpp"
#include "kern_display13_power_bridge.hpp"
using namespace Display13TC;
using namespace Display13Power;
struct IO {
    std::map<uint32_t,uint32_t> regs {{0x45404,0},{0x45454,0},{0x45444,0},{0x42000,0x8E380000},{0x1010A0,0x76543210}};
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    unsigned selected=0,elapsed=0,locks=0,unlocks=0,healthReads=0;
    bool held=false,bankFailed=false,allow=true,dropRoute=false,dropRestore=false,unhealthy=false;
    IO(unsigned tc):selected(tc) {
        regs[0x64310+0x100*tc]=0x72200813; // DONE/time-out/receive-error plus TBT routing.
        regs[0x16836C+0x1000*tc]=1U<<15;
    }
    bool readable(uint32_t r){return regs.count(r);}
    uint32_t read(uint32_t r) {
        assert(readable(r));
        if(r>=0x168000 && r<=0x16BFFF) {
            assert(held && ((regs[0x1010A0]>>(8*selected))&15)==2);++healthReads;
            return unhealthy?0:regs[r];
        }
        return regs[r];
    }
    void write(uint32_t r,uint32_t v) {
        assert(readable(r));writes.emplace_back(r,v);
        if(r==0x45404 || r==0x45454 || r==0x45444) {
            const auto request=r==0x45404?0xA80AU:0x2AAAU;
            regs[r]=(v&~(request>>1))|((v&request)>>1);return;
        }
        if(r==0x1010A0) {
            assert(held);if(dropRestore && v==0x76543210)return;regs[r]=v;return;
        }
        assert(r==0x64310+0x100*selected && !(v&0x80000000));
        if(dropRoute)return;
        // Independent AUX semantics: set ordinary control, only written 1
        // acknowledges DONE/error. A route RMW must leave all status pending.
        const auto status=regs[r]&0x52000000;
        regs[r]=(v&~0x52000000)|(status&~v);
    }
    bool admitted(unsigned tc){return tc==selected && allow && !bankFailed;}
    bool lanesPowered(){return (regs[0x45404]&5)==5;}
    bool auxPowered(unsigned tc){return regs[0x45444]&(1U<<(2*(tc+3)));}
    unsigned lock(){assert(!held);held=true;++locks;return 0x123;}
    void unlock(unsigned token){assert(held && token==0x123);held=false;++unlocks;}
    void bankFault(){bankFailed=true;}
    void delayUs(unsigned us){assert(!held);elapsed+=us;}
    bool tcAuxSupported(){return allow && !bankFailed;}
    bool prepareTCAux(unsigned tc){return Aux::prepare(*this,tc).status==Display13TC::Status::Complete;}
    bool tcAuxHealthy(unsigned tc){bool healthy=false;return Aux::health(*this,tc,healthy).status==Display13TC::Status::Complete && healthy;}
};
int main() {
    unsigned cases=0;
    for(unsigned tc=0;tc<4;++tc) {
        IO io(tc);NativeBridge b;assert(b.begin(io,0,1U|(8U<<tc)).status==Display13Power::Status::Complete);
        assert(b.getDDI(io,tc+3).status==Display13Power::Status::Complete);
        const auto control=0x64310+0x100*tc,original=io.regs[control],hip=io.regs[0x1010A0];
        assert(b.getAUX(io,tc+3).status==Display13Power::Status::Complete);
        assert(io.regs[control]==(original&~0x800U) && io.regs[0x1010A0]==hip);
        assert(io.locks==1 && io.unlocks==1 && io.healthReads==1 && !io.held);
        assert(b.getAUX(io,tc+3).status==Display13Power::Status::Complete && io.locks==1);
        assert(b.putAUX(io,tc+3).status==Display13Power::Status::Complete);
        assert(suspend(io,b.state).status==Display13Power::Status::Complete);
        assert(restore(io,b.state).status==Display13Power::Status::Complete && io.locks==2 && io.regs[0x1010A0]==hip);
        assert(b.putAUX(io,tc+3).status==Display13Power::Status::Complete);
        assert(!(io.regs[0x45444]&(3U<<(2*(tc+3)))) && !(io.regs[0x45404]&12));
        cases+=6;
    }
    // Refuse invalid, unselected, unpowered or in-flight AUX before writing.
    for(unsigned fault=0;fault<5;++fault) {
        IO io(0);io.regs[0x45404]=15;unsigned tc=0;
        if(fault==0)tc=4;if(fault==1)io.allow=false;if(fault==2)io.regs[0x45404]=3;
        if(fault==3)io.regs[0x64310]|=1U<<31;if(fault==4)io.regs[0x64310]=UINT32_MAX;
        assert(Aux::prepare(io,tc).status!=Display13TC::Status::Complete && io.writes.empty());++cases;
    }
    {IO io(0);io.regs[0x45404]=15;io.dropRoute=true;
     assert(Aux::prepare(io,0).status==Display13TC::Status::Readback);++cases;}
    {IO io(0);NativeBridge b;io.unhealthy=true;assert(b.begin(io,0,9).status==Display13Power::Status::Complete);
     assert(b.getAUX(io,3).status==Display13Power::Status::AuxHealthTimeout && b.state.faulted && io.elapsed==1000);
     assert(io.healthReads==1001 && io.regs[0x1010A0]==0x76543210 && !io.held && io.locks==io.unlocks);
     assert((io.regs[0x45404]&0xA80A)==0x802 && !(io.regs[0x45444]&0x2AAA) && !b.auxRefs[3]);++cases;}
    {IO io(0);io.regs[0x45404]=15;io.regs[0x45444]=0xC0;io.dropRestore=true;bool healthy=false;
     assert(Aux::health(io,0,healthy).status==Display13TC::Status::Readback && !healthy && io.bankFailed && !io.held);
     const auto n=io.writes.size();assert(Aux::health(io,0,healthy).status==Display13TC::Status::Invalid && io.writes.size()==n);++cases;}
    puts("PASS 32 TC AUX cases: four channel coordinates, W1C preservation/no command launch, shared-HIP restore/IRQ lifetime, native bridge nested/suspend/restore, bounded health failure and power rollback. CPU only.");
    assert(cases==32);
}
