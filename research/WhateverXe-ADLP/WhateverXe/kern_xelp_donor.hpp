// CPU portions of fixes for the shared c004fc1a donor. Caller owns admission.
#ifndef KERN_XELP_DONOR_HPP
#define KERN_XELP_DONOR_HPP
#include <stdint.h>
namespace XELPDonor {
inline bool displayPoolRequest(uint32_t enabled,uint32_t bytes) {
    return enabled && bytes && !(bytes&4095) && bytes<=0x4000000;
}
struct Poll {uint32_t value {},elapsedUs {},reads {};};
template<class IO> Poll waitEDIDReady(IO &io,uint32_t initial) {
    Poll r {initial,0,0};
    while(!(r.value&0xC00) && r.elapsedUs<50000) {
        io.delayUs(10);r.elapsedUs+=10;r.value=io.readStatus();++r.reads;
    }
    return r;
}
inline bool skipHDMIDPLanes(bool selected,bool exactCaller,bool native,uint32_t available,uint32_t lanes) {
    return selected && exactCaller && !native && !available && !lanes;
}
}
#endif
