// ADL-P GT2 / Display 13 profile. This is deliberately not an ADL-N alias:
// ADL-P uses the same Gen12 fuse layout but the full six-DSS GT2 capacity.
#ifndef kern_alderlake_p_hpp
#define kern_alderlake_p_hpp
#include <stdint.h>
#include "kern_xelp_platform.hpp"

namespace AlderLakeP {
constexpr uint32_t Identity = 0x46A68086;
constexpr uint8_t RevisionD0 = 0x0C;
constexpr uint32_t DisplayVersion = 13;

inline bool matches(uint32_t identity,uint8_t revision) {
    const auto p=XELP::identify(identity,revision);
    return p.family==XELP::Family::AlderLakeP && p.gt==XELP::GT::ADLPGT2 && p.admitted();
}

struct Resources { uint32_t slices {},dss {},nativeSubslices {},eus {},l3Banks {},urbKiB {}; };

// Gen12 exposes one bit per DSS and one disable bit per pair of EUs. A 46A6
// GT2 must expose its complete six-DSS / 96-EU geometry to this donor. Keep
// the input bounded so unpowered/all-ones or reduced SKU fuses are rejected.
inline bool resources(uint32_t slice,uint32_t dss,uint32_t euDisable,uint32_t l3Disable,
                      Resources &out) {
    out={};
    constexpr uint32_t FullDSS=(1U<<6)-1;
    if(slice!=1 || dss!=FullDSS || euDisable || l3Disable!=0xF0)return false;
    out={1,6,12,96,8,512};
    return true;
}
}
#endif
