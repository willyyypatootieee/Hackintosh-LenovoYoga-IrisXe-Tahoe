// Exact TGL donor EDID read boundary 54264 lacks status rereads in its
// time-only wait. Caller qualifies board-selected DDC/128-byte EDID and that call site.
// Poll real HW_RDY/NAK only, with a finite budget; never synthesize data.
#ifndef KERN_TGL_GMBUS_HPP
#define KERN_TGL_GMBUS_HPP
#include "kern_xelp_donor.hpp"
namespace Display13GMBus {
using Poll = XELPDonor::Poll;
template<class IO> Poll waitReady(IO &io,uint32_t initial) {
    return XELPDonor::waitEDIDReady(io,initial);
}
}
#endif
