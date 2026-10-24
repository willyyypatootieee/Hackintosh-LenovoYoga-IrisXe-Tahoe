#include "kern_display13_fb_memory.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
using namespace Display13FBMemory;
static const auto memoryLayout=XELPMemory::decode(0x03C1,0x7A800001,0);
template<class T> void put(void *p,size_t off,T v) {memcpy(static_cast<uint8_t *>(p)+off,&v,sizeof(v));}
struct Fixture {
    uint8_t controller[0x1C20] {},fb[0x4B90] {},surface[64] {};
    std::vector<uint64_t> ptes;
    uintptr_t owner {0x123400},mapping {0x345600},memory {0x567800};
    uint64_t reserve {0x06192000},gpu {0x06400000},bytes {50*450*4096ULL};
    bool ownerReady {true},changeRecord {},changeOwner {},shortSegment {};
    unsigned reads {};uint64_t physicalBase {0x180000000ULL};
    Fixture() : ptes(50*450) {
        put(controller,0xC0,owner);put(controller,0xD5C,uint32_t(1));
        put(controller,0xD60,reinterpret_cast<uintptr_t>(fb));
        put(fb,0x1D0,reinterpret_cast<uintptr_t>(controller));put(fb,0x4288,uint8_t(0xFF));
        put(fb,0x4348,reinterpret_cast<uintptr_t>(surface));
        put(surface,4,uint32_t(50));put(surface,8,uint32_t(450));
        put(surface,0x10,mapping);put(surface,0x18,memory);put(surface,0x20,gpu);
        put(surface,0x2C,uint32_t(25600));put(surface,0x38,uint32_t(1));put(surface,0x3C,uint32_t(1));
        for(unsigned i=0;i<ptes.size();++i)ptes[i]=(0x180000000ULL+i*4096)|0x99;
    }
    bool memoryReady(uintptr_t map,uintptr_t mem,uintptr_t o,uint64_t n) {
        return ownerReady && map==mapping && mem==memory && o==owner && n<=bytes;
    }
    uint64_t physical(uintptr_t,uint64_t off,uint64_t &span) {span=shortSegment?1:4096;return physicalBase+off;}
    uint64_t readPTE(uint32_t reg) {
        assert(reg>=0x800000+(gpu>>9));const auto i=((reg-0x800000)*512ULL-gpu)/4096;++reads;
        if(i==3 && changeRecord)put(surface,0x2C,uint32_t(12800));
        if(i==3 && changeOwner)put(controller,0xC0,uintptr_t(0));
        return ptes.at(i);
    }
    SurfaceReadout inspect() {return inspectSurface(*this,controller,sizeof(controller),fb,sizeof(fb),owner,reserve,0x100000000ULL,memoryLayout);}
};
int main() {
    uint64_t sample=UINT64_MAX;
    assert(scanoutSampleOffset(true,60,1920,1080,0,256,256,sample) && sample==(8*60+8)*4096ULL);
    assert(scanoutSampleOffset(true,60,1920,1080,0,960,512,sample) && sample==(16*60+30)*4096ULL);
    assert(scanoutSampleOffset(false,120,1920,1080,0,960,512,sample) && sample==512*7680+960*4);
    assert(scanoutSampleOffset(true,61,1920,1080,32,0,0,sample) && sample==4096);
    assert(!scanoutSampleOffset(true,60,1920,1080,0,257,256,sample));
    assert(!scanoutSampleOffset(true,60,1920,1080,0,256,257,sample));
    assert(!scanoutSampleOffset(true,60,1920,1080,0,1920,256,sample));
    assert(!scanoutSampleOffset(true,60,1920,1080,0,1918,256,sample));
    assert(!scanoutSampleOffset(false,60,1920,1080,0,960,512,sample));
    assert(!scanoutSampleOffset(true,60,1920,1080,1,256,256,sample));
    assert(!scanoutSampleOffset(true,0,1920,1080,0,0,0,sample));

    Fixture f;auto r=f.inspect();assert(r.status==SurfaceStatus::Complete && r.verifiedPages==22500);
    assert(r.gpu==f.gpu && r.bytes==f.bytes && r.pitch==25600);
    // Native GGTT policy permits PAT attributes; the backing identity is exact.
    Fixture native;native.gpu=0x41300000;native.bytes=200*113*4096ULL;native.ptes.resize(200*113);
    put(native.surface,4,uint32_t(200));put(native.surface,8,uint32_t(113));
    put(native.surface,0x20,native.gpu);put(native.surface,0x3C,uint32_t(2));
    for(unsigned i=0;i<native.ptes.size();++i)native.ptes[i]=(0x180000000ULL+i*4096)|0x99;
    assert(native.inspect().status==SurfaceStatus::Complete && native.reads==22600);
    Fixture physical;physical.ptes[123]+=4096;assert(physical.inspect().status==SurfaceStatus::PTE);
    Fixture scratch;scratch.ptes[10]=0xdead0001;assert(scratch.inspect().status==SurfaceStatus::PTE);
    Fixture local;local.ptes[10]|=2;assert(local.inspect().status==SurfaceStatus::PTE);
    Fixture invalid;invalid.ptes[10]&=~1ULL;assert(invalid.inspect().status==SurfaceStatus::PTE);
    Fixture stolen;put(stolen.surface,0x20,stolen.reserve-4096);assert(stolen.inspect().status==SurfaceStatus::Range && !stolen.reads);
    Fixture end;put(end.surface,0x20,uint64_t(0x100000000ULL-4096));assert(end.inspect().status==SurfaceStatus::Range);
    Fixture owner;owner.ownerReady=false;assert(owner.inspect().status==SurfaceStatus::Shape && !owner.reads);
    Fixture refs;put(refs.surface,0x38,uint32_t(0));assert(refs.inspect().status==SurfaceStatus::Shape);
    Fixture big;put(big.surface,4,uint32_t(-1));assert(big.inspect().status==SurfaceStatus::Shape);
    Fixture span;span.shortSegment=true;assert(span.inspect().status==SurfaceStatus::PTE);
    Fixture changed;changed.changeRecord=true;assert(changed.inspect().status==SurfaceStatus::StateChanged);
    Fixture changedOwner;changedOwner.changeOwner=true;assert(changedOwner.inspect().status==SurfaceStatus::StateChanged);
    Fixture alternate;put(alternate.fb,0x4340,uintptr_t(1));assert(alternate.inspect().status==SurfaceStatus::Association);
    Fixture noOwner;put(noOwner.controller,0xC0,uintptr_t(0));assert(noOwner.inspect().status==SurfaceStatus::Association);
    assert(!associations(f.controller,sizeof(f.controller),f.fb,sizeof(f.fb),f.owner));
    // Two native FBs share a controller/accelerator, but have distinct
    // memory/map identities, GGTT addresses and physical scatter backing.
    {Fixture a,b;
     put(a.controller,0xD5C,uint32_t(2));put(a.controller,0xD68,reinterpret_cast<uintptr_t>(b.fb));
     put(b.fb,0x1D0,reinterpret_cast<uintptr_t>(a.controller));put(b.fb,0x1DC,uint32_t(1));
     a.reserve=b.reserve=0xE195000;a.gpu=0x41300000;b.gpu=0x48000000;
     b.mapping=0x333000;b.memory=0x444000;b.physicalBase=0x280000000ULL;
     put(a.surface,0x20,a.gpu);put(b.surface,0x20,b.gpu);put(b.surface,0x10,b.mapping);put(b.surface,0x18,b.memory);
     for(unsigned i=0;i<b.ptes.size();++i)b.ptes[i]=(b.physicalBase+4096ULL*i)|0x99;
     const auto first=a.inspect();
     const auto second=inspectSurface(b,a.controller,sizeof(a.controller),b.fb,sizeof(b.fb),b.owner,b.reserve,0x100000000ULL,memoryLayout);
     assert(first.status==SurfaceStatus::Complete && second.status==SurfaceStatus::Complete);
     assert(first.map!=second.map && first.memory!=second.memory && first.gpu+first.bytes<=second.gpu);
     b.ptes.back()=a.ptes.back();
     assert(inspectSurface(b,a.controller,sizeof(a.controller),b.fb,sizeof(b.fb),b.owner,b.reserve,0x100000000ULL,memoryLayout).status==SurfaceStatus::PTE);
     assert(a.inspect().status==SurfaceStatus::Complete);}
    puts("AlderLakeN native accelerator scanout backing fixtures passed");
}
