// Explicit Combo A/B/C wiring. Board wiring and native backend
// qualification are separate: describing a port does not attach unsupported resources.
#ifndef KERN_DISPLAY13_FB_TOPOLOGY_HPP
#define KERN_DISPLAY13_FB_TOPOLOGY_HPP
#include "kern_display13_hdmi_wiring.hpp"
#include "kern_xelp_platform.hpp"
#include "kern_xelp_donor.hpp"
namespace Display13FBTopology {
constexpr uint32_t Bytes=0x1C0,Ports=9;
inline bool selectedWiring(const TGLHDMIWiring &w) {
    if(!w.valid || !w.hdmi || (w.hdmi&~7U))return false;
    uint32_t used=0;
    for(unsigned d=0;d<3;++d) {
        const auto pin=w.ddc[d];
        if(w.hdmi&(1U<<d)) {
            if(pin<1 || pin>3 || (used&(1U<<pin)))return false;
            used|=1U<<pin;
        } else if(pin)return false;
    }
    return true;
}
// Linux xe_lpd_display used by ADL-N has Combo A/B + TC1..4, no Combo C.
// Common three-Combo helpers above are not physical ADL-N admission.
inline bool adlnWiring(const TGLHDMIWiring &w) {return selectedWiring(w) && !(w.hdmi&~3U);}
// TC HDMI is outside the active backend; fixed DP is a separate policy.
inline bool supportedWiring(const TGLHDMIWiring &w) {return adlnWiring(w);}
inline bool multiOutputWiring(const TGLHDMIWiring &w) {
    return selectedWiring(w) && (w.hdmi&(w.hdmi-1));
}
inline bool singleOutputWiring(const TGLHDMIWiring &w) {
    return selectedWiring(w) && !multiOutputWiring(w);
}
inline uint32_t singleDDI(const TGLHDMIWiring &w) {
    if(!singleOutputWiring(w))return UINT32_MAX;
    for(unsigned d=0;d<3;++d)if(w.hdmi&(1U<<d))return d;
    return UINT32_MAX;
}
// TGL/XE_LPD TRANS_DDI_FUNC_CTL encodes physical DDI as port+1 at bit27.
// Dual board wiring alone does not identify the boot connector; use the
// active pipe's actual TMDS selection and reject an unselected connector.
inline uint32_t bootDDI(const TGLHDMIWiring &w,uint32_t trans) {
    if(!selectedWiring(w) || trans==UINT32_MAX || !(trans&(1U<<31)) || ((trans>>24)&7)>1)return UINT32_MAX;
    const auto selector=(trans>>27)&15;
    if(selector<1 || selector>3)return UINT32_MAX;
    const auto port=selector-1;
    return (w.hdmi&(1U<<port)) ? port : UINT32_MAX;
}
// A handoff adopts exactly one measured firmware scanout. A dual board policy
// does not authorize ignoring a second GOP or an unknown/unpowered bank.
inline uint32_t bootstrapPipe(bool complete,uint32_t powered,uint32_t active) {
    if(!complete || !active || (active&(active-1)) || (active&~7U) || (active&~powered))return UINT32_MAX;
    for(uint32_t pipe=0;pipe<3;++pipe)if(active&(1U<<pipe))return pipe;
    return UINT32_MAX;
}
inline uint32_t bootstrapDDI(const TGLHDMIWiring &w,bool complete,uint32_t powered,uint32_t active,uint32_t trans) {
    return bootstrapPipe(complete,powered,active)<3 ? bootDDI(w,trans) : UINT32_MAX;
}
// Observation-only eDP qualification. It validates the exact firmware panel
// record and the single measured firmware scanout, but never treats eDP as
// HDMI or authorizes the HDMI backend to mutate the panel connector.
inline uint32_t panelBootDDI(bool panelValid,uint32_t panelDDI,bool complete,uint32_t powered,
                             uint32_t active,uint32_t trans) {
    if(!panelValid || panelDDI>=3 || bootstrapPipe(complete,powered,active)>=3 || trans==UINT32_MAX ||
       !(trans&(1U<<31)) || ((trans>>27)&15)!=panelDDI+1 || ((trans>>24)&7)!=2)return UINT32_MAX;
    return panelDDI;
}
// Preserve donor record count, DDI identities, memory sizing and unrelated
// settings. Native objects retain pointers to this stable copy. Preflight
// every identity and the existing HDMI-B record before any write.
inline bool apply(void *destination,uint32_t bytes,const void *source,const TGLHDMIWiring &w) {
    if(!destination || !source || destination==source || bytes<Bytes || !selectedWiring(w))return false;
    const auto src=static_cast<const uint8_t *>(source);
    if(src[0x12]!=Ports)return false;
    const uint32_t panel[] {0,0,0,0,2,0x18};
    const uint32_t comboB[] {1,2,1,0,0x400,0x201};
    if(memcmp(src+0x20,panel,sizeof(panel)) || memcmp(src+0x38,comboB,sizeof(comboB)))return false;
    for(unsigned d=0;d<Ports;++d) {
        uint32_t ddi;memcpy(&ddi,src+0x20+24*d,sizeof(ddi));if(ddi!=d)return false;
    }
    auto dst=static_cast<uint8_t *>(destination);memcpy(dst,src,Bytes);
    if(!w.apply(dst,bytes))return false;
    for(unsigned d=0;d<Ports;++d)if(!(w.hdmi&(1U<<d))) {
        const uint32_t excluded[] {1,0}; // Native dummy connector, no panel flags.
        memcpy(dst+0x20+24*d+16,excluded,sizeof(excluded));
    }
    return true;
}
inline bool bound(const void *platform,const void *config,uint32_t ddi) {
    return platform && config && ddi<Ports &&
        static_cast<const uint8_t *>(config)==static_cast<const uint8_t *>(platform)+0x20+24*ddi;
}
inline bool selected(const void *platform,const void *config,const TGLHDMIWiring &w) {
    if(!selectedWiring(w))return false;
    for(unsigned d=0;d<3;++d)if((w.hdmi&(1U<<d)) && bound(platform,config,d) &&
        w.matches(static_cast<const uint32_t *>(config)))return true;
    return false;
}
// The exact 46A6 D0 panel policy retains donor DDI-A's panel record and makes
// every other connector a native dummy. It intentionally accepts neither a
// Combo record nor a Type-C record as a substitute for the panel.
inline bool applyInternalPanel(void *destination,uint32_t bytes,const void *source) {
    if(!destination || !source || destination==source || bytes<Bytes)return false;
    const auto src=static_cast<const uint8_t *>(source);
    if(src[0x12]!=Ports)return false;
    const uint32_t panel[] {0,0,0,0,2,0x18};
    if(memcmp(src+0x20,panel,sizeof(panel)))return false;
    for(unsigned d=0;d<Ports;++d) {
        uint32_t ddi;memcpy(&ddi,src+0x20+24*d,sizeof(ddi));if(ddi!=d)return false;
    }
    auto dst=static_cast<uint8_t *>(destination);memcpy(dst,src,Bytes);
    for(unsigned d=1;d<Ports;++d) {
        const uint32_t excluded[] {d,0,0,0,1,0};
        memcpy(dst+0x20+24*d,excluded,sizeof(excluded));
    }
    return true;
}
inline bool selectedInternalPanel(const void *platform,const void *config) {
    if(!bound(platform,config,0))return false;
    const uint32_t panel[] {0,0,0,0,2,0x18};
    return !memcmp(config,panel,sizeof(panel));
}
inline XELP::NativePorts boundInternalPanel(const void *platform) {
    const auto config=platform ? static_cast<const uint8_t *>(platform)+0x20 : nullptr;
    return selectedInternalPanel(platform,config) ? XELP::NativePorts {platform,1U} : XELP::NativePorts {};
}
inline uint32_t internalExcludedTypeC(const void *platform,uint32_t bytes) {
    if(!platform || bytes<Bytes || static_cast<const uint8_t *>(platform)[0x12]!=Ports)return 0;
    const auto p=static_cast<const uint8_t *>(platform);
    if(!selectedInternalPanel(platform,p+0x20))return 0;
    for(unsigned d=3;d<Ports;++d) {
        const uint32_t *config=reinterpret_cast<const uint32_t *>(p+0x20+24*d);
        if(config[0]!=d || config[4]!=1 || config[5])return 0;
    }
    return 0x3F000000;
}
// Transport adapter validates records under the native FB mutex. The binding
// core below consumes only validated port identities, independent of transport.
inline XELP::NativePorts boundPorts(const void *platform,const TGLHDMIWiring &w) {
    if(!platform || !selectedWiring(w))return {};
    for(unsigned d=0;d<3;++d)if((w.hdmi&(1U<<d)) &&
        !selected(platform,static_cast<const uint8_t *>(platform)+0x20+24*d,w))return {};
    return {platform,w.hdmi};
}
// Same exact donor CPU defect corrected for MI646: the mode validator
// asks a TMDS connector for DP lanes. Keep its remaining HDMI constraints.
inline bool skipDPLanes(const void *platform,const void *config,const TGLHDMIWiring &w,
                        bool exactCaller,bool native,uint32_t available,uint32_t lanes) {
    return XELPDonor::skipHDMIDPLanes(selected(platform,config,w),exactCaller,native,available,lanes);
}
// ICP/TGP SDE bits24..29 are TC1..6 hotplug, not legacy GMBUS bit24.
// Authorize masking only from the stable native connector policy: no TC
// record can be enabled or have panel/connection flags on the Combo HDMI path.
inline uint32_t excludedTypeC(const void *platform,uint32_t bytes,const TGLHDMIWiring &w) {
    if(!platform || bytes<Bytes || !selectedWiring(w))return 0;
    const auto p=static_cast<const uint8_t *>(platform);
    if(p[0x12]!=Ports)return 0;
    for(unsigned d=0;d<3;++d)if((w.hdmi&(1U<<d)) && !selected(platform,p+0x20+24*d,w))return 0;
    for(unsigned d=0;d<Ports;++d) {
        const auto config=reinterpret_cast<const uint32_t *>(p+0x20+24*d);
        if(config[0]!=d || (d>=3 && (config[4]!=1 || config[5])))return 0;
    }
    return 0x3F000000;
}
}
#endif
