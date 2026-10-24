// Display13 system-memory GGTT entries for native cursor/DSB backing.
// Primary stolen scanout is borrowed separately, never rewritten here.
#ifndef KERN_DISPLAY13_FB_GTT_HPP
#define KERN_DISPLAY13_FB_GTT_HPP
#include <stdint.h>
namespace Display13FBGTT {
constexpr uint64_t Aperture=0x10000000;
constexpr uint64_t AddressMask=0x7FFFFFF000ULL; // Conservative donor DMA limit.
inline bool cursorSize(uint32_t bytes) {
    return bytes && bytes<=0x180000 && !(bytes&0x7FFFF);
}
inline bool auxiliaryRange(uint64_t start,uint32_t bytes,uint32_t cursorBytes,bool dsb,uint64_t stolen) {
    return stolen && !(stolen&4095) && stolen<Aperture && cursorSize(cursorBytes) && start==stolen+(dsb ? cursorBytes : 0) &&
        bytes==(dsb ? 0x12000U : cursorBytes) && start+bytes<=Aperture;
}
inline bool physicalRange(uint64_t physical,uint64_t span,uint32_t bytes) {
    return bytes && !(bytes&4095) && physical && !(physical&~AddressMask) &&
        span>=bytes && physical<=AddressMask-(bytes-4096);
}
// Gen12 GGTT uses PRESENT only for system memory. Bit1 is local-memory,
// not the old writable/cache flag used by the donor's '|7' encoding.
inline uint64_t pte(uint64_t physical,uint32_t offset) {return (physical+offset)|1ULL;}
template<class IO> bool install(IO &io,uint64_t start,uint32_t bytes,uint32_t cursorBytes,
                               bool dsb,uint64_t physical,uint64_t span,uint32_t &written,uint64_t stolen) {
    written=0;
    if(!auxiliaryRange(start,bytes,cursorBytes,dsb,stolen) || !physicalRange(physical,span,bytes))return false;
    for(uint32_t i=0;i<bytes;i+=4096) {
        const auto reg=static_cast<uint32_t>(0x800000+((start+i)>>9));
        const auto expected=pte(physical,i);
        io.writePTE(reg,expected);++written;
        if(io.readPTE(reg)!=expected)return false;
    }
    return true;
}
}
#endif
