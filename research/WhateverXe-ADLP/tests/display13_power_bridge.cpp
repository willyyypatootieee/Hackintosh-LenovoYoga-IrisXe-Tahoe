#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include <cstring>
#include "kern_display13_power_bridge.hpp"
using namespace Display13Power;
struct IO {
    std::map<uint32_t,uint32_t> regs {{0x45404,0xFC0F},{0x45454,0xC},{0x45444,0},{0x42000,0x8E380000}};
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    bool failAUX {};
    uint32_t failAUXState {}, failDDIState {};
    bool readable(uint32_t r) { return regs.count(r); }
    uint32_t read(uint32_t r) { return regs.at(r); }
    void write(uint32_t r,uint32_t value) {
        assert(r==0x45404 || r==0x45454 || r==0x45444); writes.emplace_back(r,value);
        const uint32_t states = r==0x45404 ? 0x5405 : 21;
        const uint32_t requests = r==0x45404 ? 0xA80A : 42;
        regs[r]=(value&~states)|((value&requests)>>1);
        if(r==0x45444 && failAUX)regs[r]&=~4U;
        if(r==0x45444)regs[r]&=~failAUXState;
        if(r==0x45454)regs[r]&=~failDDIState;
    }
    void delayUs(unsigned) {}
};
static bool ok(Result r) { return r.status==Status::Complete; }
static void slots(const NativeBridge &b, uint32_t a,uint32_t two,uint32_t three,uint32_t four) {
    assert(b.nativeRefs(0)==1 && b.nativeRefs(1)==a && b.nativeRefs(2)==two &&
        b.nativeRefs(3)==three && b.nativeRefs(4)==four);
}
int main() {
    // Native PG scopes are reproduced without their old physical bit masks.
    assert(nativeScope(1)==4 && nativeScope(2)==6 && nativeScope(3)==14 && nativeScope(4)==30);
    assert(!nativeScope(0) && !nativeScope(5) && !nativeScope(UINT32_MAX));
    { IO io; NativeBridge b; assert(ok(b.begin(io))); slots(b,1,0,0,0);
      assert(ok(b.getPG(io,0)) && ok(b.putPG(io,0))); slots(b,1,0,0,0);
      assert(io.writes.empty());
      assert(b.nativeDDIBRefs()==1 && b.state.refs[0]==2 && b.state.refs[2]==1 && b.state.refs[5]==1);
      // Generic controller/hotplug PG3 plus two references to actual pipe A.
      assert(ok(b.getPG(io,3)) && ok(b.getPG(io,1)) && ok(b.getPG(io,1))); slots(b,4,1,1,0);
      assert(b.state.refs[0]==3 && b.state.refs[1]==1 && b.state.refs[2]==2 && b.state.refs[3]==1);
      assert(ok(b.getDDI(io,1)) && ok(b.getDDI(io,1)) && b.nativeDDIBRefs()==3);
      assert(b.state.refs[5]==1); // nested logical references, one hardware lease
      assert(ok(b.putPG(io,3))); slots(b,3,0,0,0); assert(!b.state.refs[1] && !b.state.refs[3]);
      assert(ok(b.adoptBootPath(0x1000,0,1))); slots(b,3,0,0,0); assert(b.nativeDDIBRefs()==3);
      assert(ok(b.putPG(io,1)) && ok(b.putDDI(io,1))); slots(b,2,0,0,0);
      assert(ok(b.putPG(io,1)) && ok(b.putDDI(io,1))); slots(b,1,0,0,0);
      // Release the transferred initial references, matching native free.
      assert(ok(b.putPG(io,1)) && ok(b.putDDI(io,1))); slots(b,0,0,0,0);
      for (auto r:b.state.refs) assert(!r);
      assert(io.writes.empty() && b.state.borrowed==0x3F && io.regs[0x45404]==0xFC0F && io.regs[0x45454]==0xC);
      assert(b.putPG(io,1).status==Status::Unbalanced && b.putDDI(io,1).status==Status::Unbalanced); }
    // Exact boot path adoption transfers existing refs without an extra get.
    // Before adoption, temporary references can come/go but cannot drop the
    // initial A/DDI_B ownership. A coarse PG3 caller cannot identify the path.
    { IO io; NativeBridge b; assert(ok(b.begin(io)) && ok(b.getPG(io,3)));
      assert(b.putPG(io,1).status==Status::Unbalanced && b.putDDI(io,1).status==Status::Unbalanced);
       assert(b.adoptBootPath(0,0,1).status==Status::InvalidDomains &&
           b.adoptBootPath(0x1000,3,1).status==Status::InvalidDomains && b.adoptBootPath(0x1000,0,0).status==Status::InvalidDomains);
      assert(ok(b.getPG(io,1)) && ok(b.putPG(io,1)) && ok(b.getDDI(io,1)) && ok(b.putDDI(io,1)));
      slots(b,2,1,1,0); assert(b.nativeDDIBRefs()==1 && b.bootPGProtected && b.bootDDIProtected);
      assert(ok(b.putPG(io,3)) && ok(b.adoptBootPath(0x1000,0,1)));
      assert(b.bootPath==0x1000 && b.adoptBootPath(0x1000,0,1).status==Status::Unbalanced);
      assert(!b.forgetBootPath(0x2000) && b.bootPath==0x1000 && b.bootAdopted);
      assert(b.forgetBootPath(0x1000) && !b.bootPath && b.bootAdopted);
      assert(b.adoptBootPath(0x2000,0,1).status==Status::Unbalanced && !b.bootPath);

       assert(ok(b.putPG(io,1)) && ok(b.putDDI(io,1))); slots(b,0,0,0,0); }
    // The bridge transfers the protected references to the measured physical
    // scanout pipe. The DDI identity remains independent from that pipe index.
    { IO io; NativeBridge b; assert(ok(b.begin(io)) && ok(b.adoptBootPath(0x2000,1,1)));
      assert(b.bootPath==0x2000 && b.bootPort==1 && ok(b.putPG(io,1)) && ok(b.putDDI(io,1))); }
    // Other physical ports are not enabled as a side effect of generic
    // PG3/PG4 scopes. Missing domains are errors, not simulated successes.
    { IO io; NativeBridge b; assert(ok(b.begin(io)) && ok(b.getPG(io,4))); slots(b,2,1,1,1);
      assert(b.state.refs[4]==1 && b.getDDI(io,0).status==Status::InvalidDomains &&
          b.getDDI(io,3).status==Status::InvalidDomains && b.getPG(io,5).status==Status::InvalidDomains);
      assert(ok(b.putPG(io,4))); slots(b,1,0,0,0); assert(io.writes.empty()); }
    // When B/C and shared PW2 are actually off, native PG3/PG4 requests must
    // use Display13 bit3/13/15, never the donor's old bit5/7. Releasing PG3
    // first leaves the overlapping PG4 scope powered until its final put.
    { IO io; NativeBridge b; io.regs[0x45404]=0xC03;
      assert(ok(b.begin(io)) && ok(b.getPG(io,3)) && ok(b.getPG(io,4)));
      assert(io.writes.size()==3 && (io.writes[0].second&0xA8AA)==0x80A &&
          (io.writes[1].second&0xA8AA)==0x280A && (io.writes[2].second&0xA8AA)==0xA80A);
      assert(b.state.owned==0x1A && ok(b.putPG(io,3)) && io.writes.size()==3);
      assert(ok(b.putPG(io,4)) && !b.state.owned && io.writes.size()==6 &&
          (io.regs[0x45404]&0xA8AA)==0x802 && b.pgLease[0].owner && b.ddiLease[1].owner);
      assert((io.writes[3].second&0xA8AA)==0x280A && (io.writes[4].second&0xA8AA)==0x80A &&
          (io.writes[5].second&0xA8AA)==0x802); }
    // Aggregate slot overflow must be detected before acquiring a new scope.
    { IO io; NativeBridge b; assert(ok(b.begin(io))); b.pgRefs[0]=UINT32_MAX;
      assert(b.getPG(io,3).status==Status::Overflow && !b.pgLease[2].owner && io.writes.empty()); }
    { IO io; NativeBridge b; assert(ok(b.begin(io))); b.ddiRefs[1]=UINT32_MAX;
      assert(b.getDDI(io,1).status==Status::Overflow && b.ddiLease[1].owner && io.writes.empty()); }
    // Exact boot ownership is retained when state/fuse proof fails.
    { IO io; NativeBridge b; io.regs[0x45404]=io.regs[0x45454]=0;
      auto r=b.begin(io);assert(ok(r) && r.writes==3 && io.writes.size()==3 && b.state.owned==0x25); }
    { IO io; NativeBridge b; io.regs[0x42000]=0x8E180000;
      auto r=b.begin(io); assert(r.status==Status::FuseTimeout && b.state.faulted && !b.begun && !b.pgLease[0].owner);
      assert(io.writes.empty() && b.getPG(io,1).status==Status::NotReady); }
    { IO io; NativeBridge b; assert(ok(b.begin(io))); b.state.faulted=true;
      assert(b.putPG(io,1).status==Status::NotReady && b.adoptBootPath(0x1000,0,1).status==Status::NotReady && b.pgLease[0].owner && b.ddiLease[1].owner); }
    {IO io;NativeBridge b;assert(ok(b.begin(io)) && ok(suspend(io,b.state)));
     assert(b.getPG(io,1).status==Status::NotReady && b.getDDI(io,1).status==Status::NotReady &&
         b.putPG(io,1).status==Status::NotReady && b.putDDI(io,1).status==Status::NotReady);
     assert(b.pgRefs[0]==1 && b.ddiRefs[1]==1 && ok(restore(io,b.state)) && io.writes.empty());}
    { IO io; NativeBridge b; io.regs[0x45454]=8;
      auto r=b.begin(io); assert(r.status==Status::StateTimeout && b.state.faulted && !b.begun &&
          b.pgLease[0].owner && b.pgRefs[0]==1 && !b.ddiLease[1].owner && !b.ddiRefs[1] && io.writes.empty()); }
    // Native PowerWell object publication touches only the exact owner's
    // fields and reference arrays, preserving OSObject/vtable storage.
    { IO io; NativeBridge b;
      alignas(void*) uint8_t p[0x80], ctl[0xC60] {}; memset(p,0xA5,sizeof(p));
      *reinterpret_cast<void **>(ctl+0xC8)=p;
      *reinterpret_cast<void **>(ctl+0xC40)=reinterpret_cast<void *>(uintptr_t(0x12345678));
      assert(objectAssociations(p,sizeof(p),ctl,sizeof(ctl)));
      assert(!publishObject(b,p,sizeof(p),ctl,sizeof(ctl)));
      for(auto v:p) assert(v==0xA5); // no partial publication before real power proof
      assert(ok(b.begin(io)) && ok(b.getPG(io,3)) && ok(b.getDDI(io,1)));
      assert(publishObject(b,p,sizeof(p),ctl,sizeof(ctl)));
      for(unsigned i=0;i<0x10;++i) assert(p[i]==0xA5);
      for(unsigned i=0x19;i<0x1C;++i) assert(p[i]==0xA5);
      assert(*reinterpret_cast<void **>(p+0x10)==ctl && *reinterpret_cast<void **>(p+0x78)==reinterpret_cast<void *>(uintptr_t(0x12345678)) && !p[0x18]);
      const uint32_t expectedPG[] {1,2,1,1,0};
      for(unsigned i=0;i<5;++i) assert(*reinterpret_cast<uint32_t *>(p+0x1C+4*i)==expectedPG[i]);
      for(unsigned i=0;i<9;++i) assert(*reinterpret_cast<uint32_t *>(p+0x30+4*i)==(i==1 ? 2U:0U) && !*reinterpret_cast<uint32_t *>(p+0x54+4*i));
      assert(!publishObject(b,p,0x7F,ctl,sizeof(ctl)) && !publishObject(b,p,sizeof(p),ctl,0xC5F));
      ctl[0x918]=1; assert(!objectAssociations(p,sizeof(p),ctl,sizeof(ctl))); ctl[0x918]=0;
      ctl[0xC59]=1; assert(!objectAssociations(p,sizeof(p),ctl,sizeof(ctl))); ctl[0xC59]=0;
      *reinterpret_cast<void **>(ctl+0xC8)=nullptr; assert(!objectAssociations(p,sizeof(p),ctl,sizeof(ctl)));
      assert(io.writes.empty()); }
    // Nested native AUX_B calls share one real lease and publish the exact
    // donor's AUX slot1. Logical references and physical well refs differ.
    { IO io; NativeBridge b; assert(ok(b.begin(io)));
      assert(b.getAUX(io,0).status==Status::InvalidDomains && b.getAUX(io,3).status==Status::InvalidDomains && io.writes.empty());
      assert(ok(b.getAUX(io,1)) && ok(b.getAUX(io,1)) && b.auxRefs[1]==2 && b.state.refs[6]==1 && b.state.refs[0]==3);
      assert(io.writes.size()==1 && io.writes[0].first==0x45444 && io.writes[0].second==8);
      assert(ok(b.putAUX(io,1)) && b.auxRefs[1]==1 && io.writes.size()==1);
      assert(ok(b.putAUX(io,1)) && !b.auxRefs[1] && !b.state.refs[6] && b.state.refs[0]==2 &&
          io.writes.size()==2 && !(io.regs[0x45444]&8));
      assert(b.putAUX(io,1).status==Status::Unbalanced);
      b.auxRefs[1]=UINT32_MAX;const auto writes=io.writes.size();
      assert(b.getAUX(io,1).status==Status::Overflow && io.writes.size()==writes); }
    { IO io; NativeBridge b; io.regs[0x45444]=0xC;
      assert(ok(b.begin(io)) && ok(b.getAUX(io,1)) && ok(b.putAUX(io,1)) && io.writes.empty() &&
          (b.state.borrowed&0x40) && io.regs[0x45444]==0xC); }
    { IO io; NativeBridge b;assert(ok(b.begin(io)));io.failAUX=true;
      auto r=b.getAUX(io,1);assert(r.status==Status::StateTimeout && b.state.faulted && !b.auxRefs[1] &&
          !b.auxLease[1].owner && b.state.refs[0]==2 && !b.state.refs[6] && !(io.regs[0x45444]&8)); }
    { IO io; NativeBridge b; assert(ok(b.begin(io)) && ok(b.getAUX(io,1)) && ok(suspend(io,b.state)));
      assert(b.getAUX(io,1).status==Status::NotReady && b.putAUX(io,1).status==Status::NotReady && b.auxRefs[1]==1);
      assert(ok(restore(io,b.state)) && ok(b.putAUX(io,1)) && !b.auxRefs[1]); }
    { IO io; NativeBridge b; alignas(void*) uint8_t p[0x80] {},ctl[0xC60] {};
      *reinterpret_cast<void **>(ctl+0xC8)=p;*reinterpret_cast<void **>(ctl+0xC40)=ctl;
      assert(ok(b.begin(io)) && ok(b.getAUX(io,1)) && publishObject(b,p,sizeof(p),ctl,sizeof(ctl)));
      for(unsigned i=0;i<9;++i)assert(*reinterpret_cast<uint32_t *>(p+0x54+4*i)==(i==1?1U:0U)); }
    // Combo C uses physical DDI/AUX index2, independent of hierarchical PG4.
    // Exercise all board masks and each possible boot DDI. No other DDI lease.
    for(unsigned mask=1;mask<8;++mask)for(unsigned boot=0;boot<3;++boot) {
        if(!(mask&(1U<<boot)))continue;
        IO io;NativeBridge b;assert(ok(b.begin(io,boot,mask)));
        assert(ok(b.adoptBootPath(0x1000,0,boot)));
        for(unsigned port=0;port<3;++port)if(mask&(1U<<port)) {
            assert(ok(b.getDDI(io,port)) && ok(b.getAUX(io,port)));
            assert((io.regs[DDI]&(3U<<(2*port)))==(3U<<(2*port)));
            assert((io.regs[AUX]&(3U<<(2*port)))==(3U<<(2*port)));
        }
        assert(ok(b.getPG(io,4)) && ok(suspend(io,b.state)) && ok(restore(io,b.state)));
        for(unsigned port=0;port<3;++port)if(mask&(1U<<port)) {
            assert(ok(b.putAUX(io,port)) && ok(b.putDDI(io,port)));
        }
        assert(ok(b.putPG(io,4)) && ok(b.putDDI(io,boot)) && ok(b.putPG(io,1)));
        for(auto ref:b.state.refs)assert(!ref);
    }
    {IO io;NativeBridge b;assert(ok(b.begin(io,0,7)));io.failDDIState=16;
     assert(b.getDDI(io,2).status==Status::StateTimeout && b.state.faulted && !b.ddiRefs[2]);}
    {IO io;NativeBridge b;assert(ok(b.begin(io,0,7)));io.failAUXState=16;
     assert(b.getAUX(io,2).status==Status::StateTimeout && b.state.faulted && !b.auxRefs[2]);}
    assert(nativeDomains(1)==415 && nativeDomains(2)==127 && nativeDomains(3)==511);
    assert(!nativeDomains(0) && nativeDomains(4)==1567 && !nativeDomains(UINT32_MAX));
    // Invalid port selection cannot initialize the bridge or touch hardware.
    for(auto ports:{0U,8U,UINT32_MAX}) {
        IO io;NativeBridge b;assert(b.begin(io,0,ports).status==Status::InvalidDomains);
        assert(!b.state.ready && io.writes.empty());
    }
    {IO io;NativeBridge b;
     assert(b.begin(io,1,1).status==Status::InvalidDomains && b.begin(io,0,2).status==Status::InvalidDomains &&
        b.begin(io,UINT32_MAX,3).status==Status::InvalidDomains && !b.state.ready && io.writes.empty());}
    // Explicit A-only bridge: A owns slot0, unselected B stays outside the
    // bridge even when firmware has its physical request asserted.
    {IO io;NativeBridge b;assert(ok(b.begin(io,0,1)) && b.bootPort==0 && b.selectedPorts==1 && b.state.allowed==415);
     slots(b,1,0,0,0);assert(b.nativeDDIRefs(0)==1 && !b.nativeDDIBRefs() && !(b.state.borrowed&32));
     assert(io.regs[DDI]==15 && b.ddiLease[0].domains==129 && b.state.refs[0]==2);
     assert(b.putDDI(io,0).status==Status::Unbalanced && b.adoptBootPath(0x1000,0,1).status==Status::InvalidDomains);
     const auto writes=io.writes.size();
     for(auto port:{1U,2U,8U,UINT32_MAX}) {
         assert(b.getDDI(io,port).status==Status::InvalidDomains && b.putDDI(io,port).status==Status::InvalidDomains);
         assert(b.getAUX(io,port).status==Status::InvalidDomains && b.putAUX(io,port).status==Status::InvalidDomains);
     }
     assert(io.writes.size()==writes && !b.nativeDDIRefs(8) && !b.nativeAUXRefs(UINT32_MAX));
     assert(ok(b.getAUX(io,0)) && ok(b.getAUX(io,0)) && b.auxRefs[0]==2 && b.state.refs[8]==1 && io.regs[AUX]==3);
     alignas(void*) uint8_t p[0x80] {},ctl[0xC60] {};
     *reinterpret_cast<void **>(ctl+0xC8)=p;*reinterpret_cast<void **>(ctl+0xC40)=ctl;
     assert(publishObject(b,p,sizeof(p),ctl,sizeof(ctl)));
     for(unsigned i=0;i<9;++i) {
         assert(*reinterpret_cast<uint32_t *>(p+0x30+4*i)==(i==0?1U:0U));
         assert(*reinterpret_cast<uint32_t *>(p+0x54+4*i)==(i==0?2U:0U));
     }
     assert(ok(b.putAUX(io,0)) && io.regs[AUX]==3 && ok(b.putAUX(io,0)) && !io.regs[AUX]);
      assert(ok(b.adoptBootPath(0x1000,0,0)) && ok(b.putPG(io,1)) && ok(b.putDDI(io,0)) && io.regs[DDI]==12);
      for(auto refs:b.state.refs)assert(!refs);}
    // The exact eDP candidate retains an AUX_A lease with the existing DDI-A
    // boot lease. It cannot be dropped before the verified handoff, and no
    // other connector acquires a lease as a side effect.
    {IO io;NativeBridge b;assert(ok(b.begin(io,0,1,true)) && b.bootPort==0 && b.bootAUXProtected &&
       b.nativeDDIRefs(0)==1 && b.nativeAUXRefs(0)==1 && !b.nativeDDIBRefs());
     assert(b.putAUX(io,0).status==Status::Unbalanced && b.getAUX(io,1).status==Status::InvalidDomains);
     assert(ok(b.adoptBootPath(0x1000,0,0)) && !b.bootAUXProtected && ok(b.putAUX(io,0)) &&
       ok(b.putDDI(io,0)) && ok(b.putPG(io,1)));for(auto refs:b.state.refs)assert(!refs);}
    // Dual admission does not acquire the second DDI on begin. Logical
    // nested references share one physical lease independently per port.
    {IO io;NativeBridge b;assert(ok(b.begin(io,1,3)) && io.writes.empty() && !b.ddiLease[0].owner);
     assert(ok(b.getDDI(io,0)) && ok(b.getDDI(io,0)) && b.ddiRefs[0]==2 && b.ddiRefs[1]==1 &&
        b.state.refs[7]==1 && b.state.refs[5]==1 && io.regs[DDI]==15);
     assert(ok(b.getAUX(io,0)) && ok(b.getAUX(io,1)) && ok(b.getAUX(io,1)) && io.regs[AUX]==15 && b.state.refs[0]==5);
     assert(ok(b.putDDI(io,0)) && io.regs[DDI]==15 && ok(b.putDDI(io,0)) && io.regs[DDI]==12);
     assert(b.putDDI(io,1).status==Status::Unbalanced && b.ddiRefs[1]==1);
     assert(ok(b.putAUX(io,0)) && io.regs[AUX]==12 && b.auxRefs[1]==2 && b.state.refs[0]==3);
     assert(ok(b.putAUX(io,1)) && ok(b.putAUX(io,1)) && !io.regs[AUX] && b.state.refs[0]==2);
     assert(ok(b.adoptBootPath(0x1000,0,1)) && ok(b.putDDI(io,1)) && ok(b.putPG(io,1)));
     for(auto refs:b.state.refs)assert(!refs);}
    // A boot protection must not prevent a non-boot B lease's final put.
    {IO io;NativeBridge b;assert(ok(b.begin(io,0,3)) && ok(b.getDDI(io,1)) && ok(b.putDDI(io,1)));
     assert(!b.ddiRefs[1] && b.ddiRefs[0]==1 && b.bootDDIProtected && io.regs[DDI]==15);
     assert(b.putDDI(io,0).status==Status::Unbalanced);}
    // Both ports survive power override with exact logical counts, and
    // final native puts release only the correct physical request.
    {IO io;NativeBridge b;io.regs[Driver]=io.regs[DDI]=0;
     assert(ok(b.begin(io,0,3)) && ok(b.getPG(io,3)) && ok(b.getDDI(io,1)) && ok(b.getAUX(io,0)) && ok(b.getAUX(io,1)));
     const auto refs=b.state.refs[0];assert(ok(suspend(io,b.state)) && !io.regs[DDI] && !io.regs[AUX]);
     assert(b.getDDI(io,0).status==Status::NotReady && b.putAUX(io,1).status==Status::NotReady &&
        b.ddiRefs[0]==1 && b.ddiRefs[1]==1 && b.auxRefs[0]==1 && b.auxRefs[1]==1);
     assert(ok(restore(io,b.state)) && b.state.refs[0]==refs && io.regs[DDI]==15 && io.regs[AUX]==15);
     assert(ok(b.putAUX(io,1)) && io.regs[AUX]==3 && ok(b.putAUX(io,0)) && !io.regs[AUX]);
     assert(ok(b.putDDI(io,1)) && io.regs[DDI]==3 && ok(b.putPG(io,3)));
     assert(ok(b.adoptBootPath(0x1000,0,0)) && ok(b.putDDI(io,0)) && ok(b.putPG(io,1)) && !b.state.owned);
     for(auto refsCount:b.state.refs)assert(!refsCount);}
    // A failure must not release an active B lease or increment A counters.
    {IO io;NativeBridge b;assert(ok(b.begin(io,1,3)));io.failDDIState=1;
     auto r=b.getDDI(io,0);assert(r.status==Status::StateTimeout && r.domain==7 && b.state.faulted &&
        !b.ddiRefs[0] && !b.ddiLease[0].owner && b.ddiRefs[1]==1 && b.ddiLease[1].owner &&
        b.state.refs[0]==2 && io.regs[DDI]==12);}
    {IO io;NativeBridge b;assert(ok(b.begin(io,1,3)) && ok(b.getAUX(io,1)));io.failAUXState=1;
     auto r=b.getAUX(io,0);assert(r.status==Status::StateTimeout && r.domain==8 && b.state.faulted &&
        !b.auxRefs[0] && !b.auxLease[0].owner && b.auxRefs[1]==1 && b.auxLease[1].owner && io.regs[AUX]==12);}
    puts("PASS native PowerWell bridge: PG ABI, explicit A/B/dual admission, independent DDI/AUX refs, boot protection and fault retention. Synthetic IO only.");
}
