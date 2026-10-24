// SPDX-License-Identifier: BSD-2-Clause
// Shared c004fc1a donor port-mode provider. ADL-N currently admits Combo
// HDMI only; TC HDMI is deferred and fixed DP needs a separate policy.
#ifndef TGL_COMPAT_PORT_PROBE_HPP
#define TGL_COMPAT_PORT_PROBE_HPP
#include <stdint.h>
#include <stddef.h>
#include "kern_xelp_probe.hpp"
namespace XELPPortProbe {
constexpr uint32_t ABI=1,ADLNPorts=3;
inline bool adln(uint32_t pci,uint8_t revision) {
    const auto p=XELP::identify(pci,revision);
    return (p.family==XELP::Family::AlderLakeN || p.family==XELP::Family::AlderLakeP) &&
        p.display==XELP::Display::XeLPD && p.admitted();
}
// Keep MI646's qualified TC2 policy unchanged. Other TGL TC transports are
// not admitted by the ADL-N extension, even when their numeric DDI matches.
template<class IO> uint32_t tglMode(IO &io,uint32_t ddi) {
    return XELP::legacyTGLMode(io,ddi,1U<<4);
}
}
#endif
