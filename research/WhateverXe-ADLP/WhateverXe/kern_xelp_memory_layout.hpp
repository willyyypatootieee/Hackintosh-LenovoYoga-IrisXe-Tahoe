// Gen11+ physical stolen memory: PCI BSM C0/C4 and GGC50.
// Linux v6.12 early-quirks.c gen11_stolen_base/gen9_stolen_size and
// include/drm/intel/i915_drm.h. No firmware mapping is invented or rewritten.
#ifndef KERN_XELP_MEMORY_LAYOUT_HPP
#define KERN_XELP_MEMORY_LAYOUT_HPP
#include <stdint.h>
namespace XELPMemory {
constexpr uint64_t DMAEnd=0x8000000000ULL, Aperture=0x10000000;
struct Layout {
    uint64_t base {},bytes {},gttBytes {},apertureBytes {};
    bool valid {};
    uint64_t allocationEnd {},reservationRegister {};
    bool reservationKnown {};
    uint64_t usable() const {return valid ? allocationEnd : 0;}
    uint64_t minimumReservation() const {return valid ? bytes+0x192000 : 0;}
    bool range(uint64_t offset,uint64_t size,bool excludeReserved=false) const {
        const auto limit=excludeReserved ? usable() : bytes;
        return valid && size && !(offset&4095) && !(size&4095) && offset<limit && size<=limit-offset;
    }
    uint64_t pte(uint64_t offset) const {return valid && offset<bytes && !(offset&4095) ? (base+offset)|1ULL : 0;}
};
inline Layout decode(uint16_t ggc,uint32_t low,uint32_t high,uint64_t aperture=Aperture) {
    Layout r;
    if(ggc==UINT16_MAX || low==UINT32_MAX || high==UINT32_MAX ||
       !aperture || (aperture&4095) || aperture>Aperture)return r;
    const auto gms=ggc>>8,ggms=(ggc>>6)&3;
    if(!gms || gms==0xFF || !ggms)return r;
    r.bytes=gms<0xF0 ? uint64_t(gms)*0x2000000 : uint64_t(gms-0xEF)*0x400000;
    r.gttBytes=uint64_t(1U<<ggms)*0x100000;
    r.base=(uint64_t(high)<<32)|(low&0xFFF00000U);r.apertureBytes=aperture;
    // Guard allocator headroom and DMA width; small unusable modes fail closed.
    if(!r.base || r.base>=DMAEnd || r.bytes<=r.gttBytes || r.bytes>DMAEnd-r.base ||
       r.bytes>=aperture || aperture-r.bytes<=0x195000 || r.bytes-r.gttBytes<0x1000000)return {};
    r.allocationEnd=r.bytes-r.gttBytes; // Conservative diagnostic bound until MMIO reservation is known.
    r.valid=true;return r;
}
// Gen11+ WOPCM/DSM reservation is independent of PCI GGMS. Linux v6.12
// i915_gem_stolen.c reads GEN6_STOLEN_RESERVED as a 64-bit register, then
// bounds the allocator by its physical base. Never infer it from GTT size.
inline bool reservation(Layout &r,uint64_t reg) {
    if(!r.valid || reg==UINT64_MAX)return false;
    const auto base=reg&0xFFFFFFFFFFF00000ULL;
    const uint64_t size=0x100000ULL<<((reg>>7)&3),top=r.base+r.bytes;
    if(!base || base<r.base || base>top || (base<top && size>top-base))return false;
    const auto end=base-r.base;
    if(end<0x1000000)return false;
    r.allocationEnd=end;r.reservationRegister=reg;r.reservationKnown=true;return true;
}
inline uint32_t displayPool(const Layout &r,uint32_t native,uint32_t requested=0) {
    if(!r.valid || !r.reservationKnown || !native || (native&4095) ||
       (requested && ((requested&4095) || requested>native)) || r.usable()<=4096)return 0;
    // Native FBMemMgr accounts one page plus the reservation size. Also
    // honor the physical reservation base when firmware leaves a gap.
    auto bytes=requested ? requested : native;
    const uint64_t nativeEnd=r.bytes-(0x100000ULL<<((r.reservationRegister>>7)&3));
    const auto limit=(r.usable()<nativeEnd ? r.usable() : nativeEnd)-4096;
    if(bytes>limit)bytes=static_cast<uint32_t>(limit);
    return bytes>=0x1000000 ? bytes : 0;
}
// The donor derives its recycle end from reservation SIZE alone. Firmware
// may place that reservation below top-size; clip the exported remainder
// to the independently decoded physical base before accelerator attachment.
inline bool boundRecycle(const Layout &r,uint64_t physical,uint32_t &bytes) {
    if(!r.valid || !r.reservationKnown)return false;
    if(!bytes)return true;
    if(physical<r.base || (physical&4095) || (bytes&4095))return false;
    const auto offset=physical-r.base;
    if(offset>r.usable())return false;
    if(bytes>r.usable()-offset)bytes=static_cast<uint32_t>(r.usable()-offset);
    return true;
}
inline bool samePhysical(const Layout &a,const Layout &b) {
    return a.valid && b.valid && a.base==b.base && a.bytes==b.bytes &&
        a.gttBytes==b.gttBytes && a.apertureBytes==b.apertureBytes;
}
inline bool same(const Layout &a,const Layout &b) {
    return samePhysical(a,b) && a.allocationEnd==b.allocationEnd &&
        a.reservationKnown==b.reservationKnown && a.reservationRegister==b.reservationRegister;
}
}
#endif
