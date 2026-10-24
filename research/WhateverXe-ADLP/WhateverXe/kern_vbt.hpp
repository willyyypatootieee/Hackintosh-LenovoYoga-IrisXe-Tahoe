// Bounded firmware facts, separate from donor ABI and runtime link state.
// Wire offsets/port numbering checked against Intel VBT definitions in Linux
// v6.12. This parser is independently implemented; no Linux code is included.
#ifndef KERN_VBT_HPP
#define KERN_VBT_HPP
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "kern_xelp_platform.hpp"
#include "kern_tgl_hdmi_wiring.hpp"
namespace XELP { namespace VBT {
inline uint16_t u16(const uint8_t *p) {return uint16_t(uint16_t(p[0]) | uint16_t(p[1])<<8);}
inline uint32_t u32(const uint8_t *p) {return uint32_t(u16(p)) | uint32_t(u16(p+2))<<16;}
inline uint64_t u64(const uint8_t *p) {return uint64_t(u32(p)) | uint64_t(u32(p+4))<<32;}
enum class Status : uint32_t { Missing, Valid, Header, Checksum, Blocks, Version, Children, Conflict, Platform };
struct Port {
    uint32_t type {},dvo {},ddc {},aux {},flags {},capabilities {};
    // flags preserves byte23 (lane reversal, LSPCON, HPD polarity, lane count).
    // capabilities preserves byte33 (USB-C/TBT capabilities, NOT current mode).
    bool dp() const {return type&4;}
    bool internal() const {return type&0x1000;}
    bool fixedTMDS() const {return (type&0x12)==0x12 && !(type&(4|0x1000|0x400|0x20|1)) &&
        !(flags&0x17) && !(capabilities&3);}
};
// A strictly observed ADL-P internal panel route. This describes only the
// firmware's fixed eDP panel; Type-C/USB-C records remain outside this path.
struct InternalPanel {
    uint32_t ddi {PortCount},ddc {},aux {},type {},flags {};
    bool valid {};
};
struct Ports {
    Status status {Status::Missing};
    uint32_t version {},bytes {},present {},unmapped {},ambiguous {};
    Port port[PortCount];
    bool valid() const {return status==Status::Valid;}
    // TGL external DP candidates, not a fixed/alt/TBT mode decision. Current
    // native AUX routing follows DDI; different firmware AUX needs a backend.
    uint32_t tglDPCandidates(uint32_t backend) const {
        if(!valid())return 0;
        uint32_t result=0;
        for(unsigned d=3;d<PortCount;++d) {
            const auto &p=port[d];
            bool shared=false;
            for(unsigned other=0;other<PortCount;++other)
                if(other!=d && (present&(1U<<other)) && port[other].dp() && port[other].aux==p.aux)shared=true;
            if((present&backend&(1U<<d)) && (p.type&0x14)==0x14 &&
               !(p.type&(0x1000|0x400|0x20|1)) && !(p.flags&0x17) &&
               !shared && p.aux==(d+1)*0x10)result|=1U<<d;
        }
        return result;
    }
    TGLHDMIWiring hdmi(uint32_t backend) const {
        auto w=TGLHDMIWiring::empty();
        if(!valid())return w; // no guessed wiring; caller retains existing guards
        uint32_t used=0;
        for(unsigned d=0;d<3;++d) {
            if(!(present&backend&(1U<<d)) || !port[d].fixedTMDS())continue;
            const auto pin=port[d].ddc;
            if(pin<1 || pin>3 || (used&(1U<<pin)))return {}; // ambiguous DDC ownership
            used|=1U<<pin;w.hdmi|=1U<<d;w.ddc[d]=pin;
        }
        return w;
    }
    InternalPanel internalPanel() const {
        InternalPanel r;
        if(!valid() || !(present&1U))return r;
        const auto &p=port[0];
        // Exact record observed on the guarded 46A6 D0 target: DDI A/eDP,
        // DVO 10, DDC 3, AUX 0x40 and the firmware's fixed panel flags.
        if(!p.internal() || !p.dp() || p.type!=0x1806 || p.dvo!=10 ||
           p.ddc!=3 || p.aux!=0x40 || p.flags!=0xC0)return r;
        // The fixed panel owns its AUX channel. A second internal record or
        // another DP record sharing AUX is a board topology we do not model.
        for(unsigned d=1;d<PortCount;++d)if(present&(1U<<d)) {
            const auto &other=port[d];
            if(other.internal() || (other.dp() && other.aux==p.aux))return r;
        }
        r={0,p.ddc,p.aux,p.type,p.flags,true};return r;
    }
};
// Return the donor's physical-DDI slot, never an FB index. Display13 VBT
// retains C/D/E labels even on ADL-N, whose physical Combo set is only A/B.
inline uint32_t ddi(Family family,uint32_t dvo) {
    const uint8_t dp[]={10,7,8,9,11,13,15,17,19};
    const uint8_t hd[]={0,1,2,3,12,14,16,18,20};
    for(unsigned p=0;p<9;++p)if(dvo==dp[p] || dvo==hd[p]) {
        if(family==Family::TigerLake)return p==2 ? PortCount : p;
        if(family==Family::AlderLakeN || family==Family::AlderLakeP)
            return p<2 ? p : p>=5 ? p-2 : PortCount;
    }
    return PortCount;
}
inline Ports parse(const void *data,size_t length,Family family) {
    Ports r;
    if(!data || !length)return r;
    // ADL-P 46A6 uses the same Display-13 Combo A/B port coordinates as the
    // guarded ADL-N backend. Its runtime qualification remains separate; this
    // only permits its firmware VBT to describe those physical ports.
    if(family!=Family::TigerLake && family!=Family::AlderLakeN && family!=Family::AlderLakeP){r.status=Status::Platform;return r;}
    const auto p=static_cast<const uint8_t *>(data);r.status=Status::Header;
    if(length<48 || memcmp(p,"$VBT",4))return r;
    const size_t header=u16(p+22),size=u16(p+24),bdb=u32(p+28);
    if(header<48 || size<header || size>length || bdb<header || bdb>size || size-bdb<22)return r;
    uint8_t sum=0;for(size_t i=0;i<size;++i)sum+=p[i];
    if(sum){r.status=Status::Checksum;return r;}
    const auto b=p+bdb;const size_t bh=u16(b+18),bs=u16(b+20);
    if(memcmp(b,"BIOS_DATA_BLOCK ",16) || bh<22 || bs<bh || bs>size-bdb)return r;
    r.version=u16(b+16);r.bytes=static_cast<uint32_t>(size);
    // Reviewed field layout. Unknown future versions never become board facts.
    if(r.version<216 || r.version>256){r.status=Status::Version;return r;}
    r.status=Status::Blocks;const uint8_t *children=nullptr;size_t childBytes=0;
    for(size_t at=bh;at<bs;) {
        if(bs-at<3)return r;
        size_t count=u16(b+at+1);
        if(b[at]==53) { // MIPI sequence v3 uses a 32-bit block length
            if(bs-at<4)return r;
            if(b[at+3]>=3){if(bs-at<8)return r;count=u32(b+at+4);}
        }
        if(count>bs-at-3)return r;
        // Consume only General Definitions; unrelated later blocks need not
        // parse successfully (real firmware has off-by-one BDB tail sizes).
        if(b[at]==2){children=b+at+3;childBytes=count;break;}
        at+=3+count;
    }
    r.status=Status::Children;
    if(!children || childBytes<5)return r;
    const auto stride=children[4];
    if(stride<38 || stride>64 || (childBytes-5)%stride)return r;
    for(size_t at=5;at<childBytes;at+=stride) {
        const auto c=children+at;const auto type=u16(c+2);
        if(!type)continue; // disabled firmware entry is not a wired connector
        const auto d=ddi(family,c[16]);
        if(d==PortCount){++r.unmapped;continue;}
        if(r.present&(1U<<d)){r.status=Status::Conflict;return r;}
        auto &q=r.port[d];q.type=type;q.dvo=c[16];q.ddc=c[19];q.aux=c[25];q.flags=c[23];q.capabilities=c[33];
        r.present|=1U<<d;
        if(!q.fixedTMDS())r.ambiguous|=1U<<d;
    }
    r.status=Status::Valid;return r;
}
// OpRegion acquisition descriptor, kept pure for malformed/overflow testing.
struct Region {uint64_t address {};uint32_t bytes {},inlineBytes {};bool valid {};};
inline Region region(const void *data,size_t length,uint32_t asls) {
    Region r;const auto p=static_cast<const uint8_t *>(data);
    if(!p || length<8192 || !asls || asls>0xFFFFE000U || memcmp(p,"IntelGraphicsMem",16) || u32(p+16)<8)return r;
    r.valid=true;const auto boxes=u32(p+0x58);
    r.inlineBytes=((boxes&0x10)?0x1C00:8192)-0x400;
    if(p[23]!=2 || !(boxes&4))return r;
    auto address=u64(p+0x3BA);const auto bytes=u32(p+0x3C2);
    if(!address || bytes<48 || bytes>65536)return r;
    if(p[22]>=1){if(address<8192 || address>UINT64_MAX-asls)return r;address+=asls;}
    // x86 physical-address ceiling; reject overflow before mapping firmware.
    if(address>0xFFFFFFFFFFFFFULL || bytes>0x10000000000000ULL-address)return r;
    r.address=address;r.bytes=bytes;return r;
}
inline TGLHDMIWiring selectHDMI(bool explicitProperty,const void *bytes,uint32_t length,
                                 const Ports &firmware,uint32_t backend) {
    return explicitProperty ? TGLHDMIWiring::fromProperty(bytes,length) : firmware.hdmi(backend);
}
} }
#endif
