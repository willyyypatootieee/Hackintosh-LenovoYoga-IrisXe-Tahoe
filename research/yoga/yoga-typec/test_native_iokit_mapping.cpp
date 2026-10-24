#include "NativeIokitMapping.cpp"
#include "NativeIokitAux.cpp"
#include "NativeIokitDpcd.cpp"
#include "DpTrainingSequence.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace YogaTypec;
template<class T> static void put(void *object,size_t offset,T value) {
    memcpy(static_cast<uint8_t *>(object)+offset,&value,sizeof(value));
}
struct Lifetime : NativeMappingLifetime {
    bool locked=false, valid=true, exact=true, enter=true;
    uintptr_t ctrl=0, mgr=0;
    unsigned entries=0,leaves=0;
    bool tryEnter() override { ++entries; if (!enter) return false; assert(!locked); return locked=true; }
    void leave() override { assert(locked); locked=false; ++leaves; }
    bool current(IOService *,Route r,uint64_t e) override { return locked && valid && r.tcIndex==2 && e==9; }
    uintptr_t controllerVptr() const override { return ctrl; }
    uintptr_t managerVptr() const override { return mgr; }
    bool exactBuildVerified() const override { return exact; }
};
struct Fixture {
    IOService controller;
    OSObject manager;
    IOPCIDevice provider;
    IOMemoryDescriptor descriptor;
    IOMemoryMap mapping;
    Lifetime life;
    std::vector<uint32_t> memory;
    NativeIokitMapping backend;
    Fixture() : memory(0x200000/4), backend(&controller,life) {
        controller.className="AppleIntelBaseController";
        manager.className="AppleIntelRegisterAccessManager";
        controller.parent=&provider;
        provider.bar=&descriptor; mapping.descriptor=&descriptor;
        mapping.virtualAddress=reinterpret_cast<uintptr_t>(memory.data());
        life.ctrl=field<uintptr_t>(&controller,0); life.mgr=field<uintptr_t>(&manager,0);
        put(&controller,0xc60,&provider); put(&controller,0xc40,&manager); put(&controller,0xc70,&mapping);
        put(&controller,0x9b0,mapping.virtualAddress); put(&controller,0xc78,uint32_t(mapping.length));
        put(&manager,0x50,mapping.virtualAddress); put(&manager,0x60,uint32_t(mapping.length));
        memory[0x51004/4]=0x40000000;
    }
    void balanced() {
        assert(!life.locked && controller.refs==1 && manager.refs==1 && provider.refs==1 &&
               mapping.refs==1 && descriptor.refs==1);
    }
};
struct AuxBinding : NativeAuxPortBinding {
    OSObject port;
    uint32_t config[6]={5,11,1,1,0x400,0x201};
    IORecursiveLock nativeLock;
    IOService *global=nullptr;
    bool globalVerified=true;
    Fixture &fixture;
    bool valid=true, originalReady=true;
    unsigned originalCalls=0;
    unsigned defers=0;
    bool writeExpected=false;
    bool trainerWire=false;
    unsigned deferOperation=0, operations=0;
    bool operationDeferred=false;
    uint8_t sinkPattern=0;
    uint64_t expectedEpoch=9;
    static AuxBinding *active;
    AuxBinding(Fixture &f) : fixture(f) {
        global=&f.controller;
        port.className="AppleIntelPortHAL";
        put(&port,0x548,static_cast<const uint32_t *>(config));
        put(&port,0x550,uint32_t(3));
        put(&port,0x590,uint32_t(0x64500)); put(&port,0x594,uint32_t(0x64510)); put(&port,0x598,uint32_t(0x64514));
        put(&fixture.controller,0xc30,&nativeLock);
        fixture.memory[0x44470/4]=1u<<18;
        fixture.memory[0x45444/4]=(1u<<11)|(1u<<10);
        fixture.memory[0x64500/4]=1u<<6;
        active=this;
    }
    bool current(IOService *c,OSObject *p,Route r,uint64_t e) override {
        return valid && c==&fixture.controller && p==&port && r.tcIndex==2 && e==expectedEpoch;
    }
    uintptr_t portVptr() const override { return field<uintptr_t>(&port,0); }
    const uint32_t *retainedConfig() const override { return config; }
    Original verifiedOriginal() const override { return originalReady ? original : nullptr; }
    IOService *const *verifiedGlobalControllerSlot() const override { return globalVerified ? &global : nullptr; }
    static uint32_t original(void *p,uint32_t command,uint32_t *words,uint32_t bytes,bool flag) {
        auto &a=*active;
        assert(p==&a.port && a.nativeLock.depth==1 && command<=1 && !flag);
        const uint32_t address=(words[0]>>8)&0xfffff;
        const unsigned count=(words[0]&255)+1;
        assert(count<=16 && bytes==(command ? count+4 : 4));
        // Original nests the SAME recursive lock, then leaves hardware reply
        // available. Exercise the concrete kernel adapter's actual MMIO reads.
        assert(IORecursiveLockTryLock(&a.nativeLock)); ++a.originalCalls;
        uint8_t wire[20]={}; unsigned replyBytes=1;
        if (a.trainerWire) {
            if (!a.operationDeferred && a.operations+1==a.deferOperation) {
                a.operationDeferred=true; wire[0]=0x20;
            } else {
                a.operationDeferred=false; ++a.operations;
                if (command) {
                    assert(address==0x102 && (count==1 || count==3));
                    a.sinkPattern=reinterpret_cast<uint8_t *>(words+1)[0];
                } else {
                    replyBytes=count+1;
                    if (address==0) {
                        assert(count==16);
                        wire[1]=0x14; wire[2]=30; wire[3]=0x44; wire[4]=0x80;
                    } else if (address==0x202) {
                        assert(count==6); wire[1]=0x77; wire[2]=0x77; wire[3]=1;
                    } else {
                        assert(address==0x102 && count==1); wire[1]=a.sinkPattern;
                    }
                }
            }
        } else {
            assert(command==unsigned(a.writeExpected));
            assert(words[0]==(a.writeExpected ? 0x80010200u : 0x90020205u));
            if (a.writeExpected) assert(reinterpret_cast<uint8_t *>(words+1)[0]==0x21);
            const bool defer=a.originalCalls<=a.defers;
            wire[0]=defer ? 0x20 : 0;
            if (!defer && !a.writeExpected) { replyBytes=7; wire[1]=0x77; wire[2]=0x77; wire[3]=1; }
        }
        a.fixture.memory[0x64510/4]=(1u<<30)|(replyBytes<<20);
        for (unsigned i=0;i<20;i+=4) {
            uint32_t value=0;
            for (unsigned j=0;j<4;++j) value|=uint32_t(wire[i+j])<<(24-8*j);
            a.fixture.memory[(0x64514+i)/4]=value;
        }
        memset(words,0xa5,20);
        IORecursiveLockUnlock(&a.nativeLock);
        return 0;
    }
};
AuxBinding *AuxBinding::active=nullptr;
// Native mapping/locking/command/retry/DPCD implementations are unmodified.
// Only hardware replies and the not-yet-integrated source PHY remain doubles.
struct NativeTrainer : NativeIokitDpcd {
    Fixture &f;
    AuxBinding &b;
    NativeIokitAux &physical;
    uint8_t pattern=0;
    NativeTrainer(Fixture &fixture,AuxBinding &binding,NativeIokitAux &p)
        : NativeIokitDpcd(p), f(fixture), b(binding), physical(p) {}
    bool authorized(Route r,uint64_t e) { return f.life.valid && b.current(&f.controller,&b.port,r,e); }
    bool cancelled(Route,uint64_t) { return false; }
    uint64_t nowMicros() { assert(!b.nativeLock.depth); return testUptimeMicros; }
    bool linkParametersReady(Route,uint64_t,unsigned n,uint32_t rate) { return n==2 && rate==540000; }
    bool sourceSupportsPattern(Route,uint64_t,uint8_t) { return true; }
    bool sourceDriveMatches(Route,uint64_t,const DpDriveLevel *) { return true; }
    bool setSourcePattern(Route,uint64_t,uint8_t p) { assert(!physical.commandLockHeld()); pattern=p; return true; }
    bool sourceTrainingDisabled(Route,uint64_t) { return pattern==0; }
};
int main() {
    {
        Fixture f; uint32_t value=0;
        assert(readNativeDssmActualLocked(f.backend,{2},9,value)==DssmReadResult::Ok);
        assert(value==0x40000000 && f.life.leaves==1); f.balanced();
        f.memory[0x51004/4]=0;
        assert(readNativeDssmActualLocked(f.backend,{2},9,value)==DssmReadResult::Ok && value==0); f.balanced();
        f.memory[0x51004/4]=UINT32_MAX; value=0x1234;
        assert(readNativeDssmActualLocked(f.backend,{2},9,value)==DssmReadResult::ReadFailed && value==0x1234); f.balanced();
    }
    for (unsigned failure=0;failure<23;++failure) {
        Fixture f;
        switch (failure) {
            case 0: f.life.exact=false; break;
            case 1: f.life.valid=false; break;
            case 2: f.life.enter=false; break;
            case 3: f.controller.inactive=true; break;
            case 4: f.provider.device=0xffff; break;
            case 5: f.provider.command=0; break;
            case 6: put(&f.controller,0xc70,static_cast<IOMemoryMap *>(nullptr)); break;
            case 7: put(&f.controller,0x918,uint8_t(1)); break;
            case 8: put(&f.controller,0x1be0,uint64_t(1)); break;
            case 9: put(&f.controller,0x1bf0,uint64_t(1)); break;
            case 10: f.mapping.options=0; break; // default cache is NOT verified UC
            case 11: f.mapping.options|=kIOMapReadOnly; break;
            case 12: f.mapping.task=nullptr; break;
            case 13: f.mapping.physical+=4096; break;
            case 14: f.descriptor.physical+=4096; break;
            case 15: f.mapping.length=0x51004; break;
            case 16: f.descriptor.length-=4096; break;
            case 17: put(&f.controller,0xc78,uint32_t(0)); break;
            case 18: put(&f.manager,0x50,uintptr_t(0)); break;
            case 19: f.life.ctrl+=8; break;
            case 20: f.life.mgr+=8; break;
            case 21: f.mapping.descriptor=nullptr; break;
            case 22: f.controller.parent=nullptr; break;
        }
        uint32_t value=0x1234;
        assert(readNativeDssmActualLocked(f.backend,{2},9,value)!=DssmReadResult::Ok);
        assert(value==0x1234); f.balanced();
    }
    {
        Fixture f; assert(f.backend.lockMapping()); assert(f.backend.retainMapping({2},9));
        uint32_t value=0x1234;
        assert(!f.backend.readRetained32(0x64510,value) && value==0x1234); // narrow read surface
        f.life.valid=false;
        assert(!f.backend.readRetained32(0x51004,value) && value==0x1234);
        f.backend.releaseMapping(); f.backend.unlockMapping(); f.balanced();
    }
    {
        Fixture f; AuxBinding b(f);
        assert(f.backend.lockMapping() && f.backend.retainMapping({2},9));
        NativeIokitAux physical(f.backend,b,&b.port,{2},9);
        NativeAuxCommand<NativeIokitAux> adapter(physical);
        uint8_t output[6]={};
        const auto r=adapter.read({2},9,0x202,output,6);
        assert(r.result==NativeAuxResult::Ok && r.actual==6 && output[0]==0x77 && output[1]==0x77 && output[2]==1);
        assert(b.originalCalls==1 && b.nativeLock.depth==0 && b.nativeLock.acquisitions==2 && b.nativeLock.releases==2);
        f.backend.releaseMapping(); f.backend.unlockMapping(); f.balanced();
    }
    for (unsigned failure=0;failure<15;++failure) {
        Fixture f; AuxBinding b(f);
        assert(f.backend.lockMapping() && f.backend.retainMapping({2},9));
        switch (failure) {
            case 0: b.valid=false; break;
            case 1: b.originalReady=false; break;
            case 2: b.nativeLock.available=false; break;
            case 3: b.config[0]=1; break;
            case 4: put(&b.port,0x550,uint32_t(2)); break; // mode2 is TBT, not DP-alt
            case 5: f.memory[0x44470/4]|=1u<<2; break;
            case 6: f.memory[0x44470/4]=0; break;
            case 7: f.memory[0x45444/4]=1u<<11; break;
            case 8: f.memory[0x64500/4]=0; break;
            case 9: f.memory[0x45444/4]=UINT32_MAX; break;
            case 10: put(&b.port,0x594,uint32_t(0x64110)); break;
            case 11: b.expectedEpoch=10; break;
            case 12: f.life.valid=false; break;
            case 13: b.global=&f.provider; break;
            case 14: b.globalVerified=false; break;
        }
        NativeIokitAux physical(f.backend,b,&b.port,{2},9);
        NativeAuxCommand<NativeIokitAux> adapter(physical);
        uint8_t output[6]={1,2,3,4,5,6};
        const auto r=adapter.read({2},9,0x202,output,6);
        assert(r.result!=NativeAuxResult::Ok && !r.actual && b.originalCalls==0 && output[0]==1 && output[5]==6);
        assert(b.nativeLock.depth==0 && b.nativeLock.acquisitions==b.nativeLock.releases);
        f.backend.releaseMapping(); f.backend.unlockMapping(); f.balanced();
    }
    for (unsigned defers : {0u,1u,6u,7u}) {
        for (bool write : {false,true}) {
            Fixture f; AuxBinding b(f); b.defers=defers; b.writeExpected=write;
            testUptimeMicros=0; testSleeps=0; testSleptLocked=false; testSleepLock=&b.nativeLock;
            assert(f.backend.lockMapping() && f.backend.retainMapping({2},9));
            NativeIokitAux physical(f.backend,b,&b.port,{2},9);
            NativeIokitDpcd dpcd(physical);
            size_t actual=99; uint8_t output[6]={0xcc,0xcc,0xcc,0xcc,0xcc,0xcc}, input=0x21;
            const bool ok=write ? dpcd.writeDpcd({2},9,0x102,&input,1,actual) :
                                 dpcd.readDpcd({2},9,0x202,output,6,actual);
            assert(ok==(defers<7) && actual==(ok ? (write ? 1u : 6u) : 0u));
            assert(b.originalCalls==(defers<7 ? defers+1 : 7) && testSleeps==(defers<7 ? defers : 6));
            assert(!b.nativeLock.depth && !testSleptLocked && dpcd.writeIntentObserved()==write);
            if (!write) assert(output[0]==(ok ? 0x77 : 0xcc));
            f.backend.releaseMapping(); f.backend.unlockMapping(); f.balanced();
            testSleepLock=nullptr;
        }
    }
    for (unsigned operation=0;operation<=8;++operation) {
        Fixture f; AuxBinding b(f); b.trainerWire=true; b.deferOperation=operation;
        testUptimeMicros=0; testSleeps=0; testSleptLocked=false; testSleepLock=&b.nativeLock;
        assert(f.backend.lockMapping() && f.backend.retainMapping({2},9));
        NativeIokitAux physical(f.backend,b,&b.port,{2},9);
        NativeTrainer backend(f,b,physical);
        DpTrainingSequence<NativeTrainer> trainer(backend,{2},9);
        assert(trainer.start(2,540000)==DpTrainResult::Progress);
        DpTrainResult result=DpTrainResult::Progress; DpDriveLevel levels[4]={};
        for (unsigned step=0;step<50 && result==DpTrainResult::Progress;++step) {
            testUptimeMicros+=1000; result=trainer.step(levels);
        }
        assert(result==DpTrainResult::Ready && b.operations==8 && !b.sinkPattern && !backend.pattern);
        assert(b.originalCalls==8+unsigned(operation!=0) && testSleeps==unsigned(operation!=0));
        assert(!testSleptLocked && !b.nativeLock.depth && backend.writeIntentObserved());
        f.backend.releaseMapping(); f.backend.unlockMapping(); f.balanced(); testSleepLock=nullptr;
    }
    puts("PASS: native IOKit implementation, typed provenance, actual read and balanced references");
}
