// Generation-aware implementation behind the donor's probePortMode ABI.
// IO owns the powered mapping; this performs reads only, never TC acquisition.
#ifndef KERN_XELP_PROBE_HPP
#define KERN_XELP_PROBE_HPP
#include "kern_xelp_platform.hpp"
#include "kern_tgl_port_map.hpp"
namespace XELP {
struct ProbeBinding {
    uintptr_t controller {},access {},platform {};
    ProbeConfig config;
    bool valid() const {return controller && access && platform && config.valid();}
    bool sameBinding(const ProbeBinding &b) const {
        return controller==b.controller && access==b.access && platform==b.platform && same(config,b.config);
    }
    bool bound(uintptr_t manager,uintptr_t record,const uint32_t *c) const {
        return valid() && manager==access && c && c[0]<PortCount && record==platform+0x20+24*c[0];
    }
};
template<class IO> uint32_t legacyTGLMode(IO &io,uint32_t ddi,uint32_t selected) {
    if(ddi<=2)return 1; // Preserve native Combo/panel mode classification.
    if(ddi>=PortCount || !(selected&(1U<<ddi)))return 0;
    TGLTypeCPort port;TGLFiaPort fia;
    if(!TGLTypeCPort::resolve(ddi,port) || !io.readable(0xC4000) || !port.legacyHPD(io.read(0xC4000)) ||
       !io.readable(0x1638A0))return 0;
    const auto first=io.read(0x1638A0);
    if(!(first&0x10) || !port.resolveFia(first,fia) || !io.readable(fia.live) || !io.readable(fia.ready))return 0;
    const auto live=fia.live==0x1638A0 ? first : io.read(fia.live);
    return fia.readyForLegacy(live,io.read(fia.ready)) ? 4 : 0;
}
template<class IO> uint32_t probeMode(IO &io,const ProbeBinding &b,uintptr_t manager,uintptr_t record,const uint32_t *c) {
    if(!b.bound(manager,record,c))return 0;
    if(b.config.profile.display==Display::XeD)
        return legacyTGLMode(io,c[0],b.config.legacy&b.config.legacyBackend);
    // Exact internal DDI-A eDP is a distinct Display13 transport. Do not
    // classify its panel record as TMDS or run the Combo DDC policy.
    if(b.config.internal)
        return b.config.internal==1 && c[0]==0 && c[1]==0 && c[2]==0 && c[3]==0 && c[4]==2 && c[5]==0x18 ? 1 : 0;
    // Display13 fixed Combo HDMI remains the only qualified Combo transport.
    if(!(b.config.combo&b.config.comboBackend&(1U<<c[0])))return 0;
    return c[1]>=1 && c[1]<=3 && c[2]==2 && !c[3] && c[4]==0x800 && (c[5]&0xB)==2 ? 1 : 0;
}
}
#endif
