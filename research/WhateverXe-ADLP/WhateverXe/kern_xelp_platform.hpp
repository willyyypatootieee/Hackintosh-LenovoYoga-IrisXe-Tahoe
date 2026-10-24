// Shared WhateverXe/provider CPU contract. Recognition is not runtime qualification.
// Keep hardware capability, donor ABI and board transport as separate inputs.
#ifndef KERN_XELP_PLATFORM_HPP
#define KERN_XELP_PLATFORM_HPP
#include <stdint.h>
#include <stddef.h>
#include "kern_xelp_devices.hpp"
namespace XELP {
struct Profile {
    uint32_t identity {}, revision {}; // Physical config-space identity, never spoofed.
    Family family {Family::Unknown};
    Display display {Display::Unknown};
    GT gt {GT::Unknown};
    Donor donor {Donor::Unknown};
    uint32_t effectiveIdentity {}; // Table selector; may come from device-id.
    bool overridden() const {return identity!=effectiveIdentity;}
    bool admitted() const {return (identity&0xFFFF)==0x8086 && revision<=255 &&
        donor==Donor::C004FC1A && gt!=GT::Unknown &&
        (family!=Family::AlderLakeN || revision==0) &&
        // The only ADL-P bring-up profile is the measured 46A6 D0 device.
        // Other ADL-P rows remain catalogue-only until their topology and
        // display ownership rules have separate qualification.
        (family!=Family::AlderLakeP || (gt==GT::ADLPGT2 && identity==0x46A68086 &&
         effectiveIdentity==identity && revision==0x0C));}
};
inline Profile identify(uint32_t physical,uint32_t revision,uint32_t effective) {
    Profile p;p.identity=physical;p.revision=revision;p.effectiveIdentity=effective;
    if((physical&0xFFFF)!=0x8086 || revision>255)return p;
    const auto entry=deviceEntry(effective);if(!entry)return p;
    p.family=entry->family;p.display=entry->display;p.gt=entry->backend;
    if(p.gt!=GT::Unknown)p.donor=Donor::C004FC1A;
    return p;
}
inline Profile identify(uint32_t pci,uint32_t revision) {return identify(pci,revision,pci);}
// IOPCIFamily also publishes device-id when no override is injected. Equal
// values are ordinary physical selection. An invalid explicit value never
// silently falls back to a different backend.
inline Profile select(uint32_t pci,uint32_t revision,bool present,const void *data,size_t size) {
    if(!present)return identify(pci,revision);
    if(!data || size!=4)return identify(pci,revision,0);
    const auto b=static_cast<const uint8_t *>(data);
    const uint32_t id=uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);
    if(!id || id>=0xFFFF)return identify(pci,revision,0);
    return identify(pci,revision,(id<<16)|(pci&0xFFFF));
}
inline bool same(const Profile &a,const Profile &b) {
    return a.identity==b.identity && a.revision==b.revision && a.family==b.family &&
        a.display==b.display && a.gt==b.gt && a.donor==b.donor && a.effectiveIdentity==b.effectiveIdentity;
}
inline const char *model(const Profile &p) {
    // Prefer the actual known product name even when trying another backend.
    auto entry=deviceEntry(p.identity);if(!entry)entry=deviceEntry(p.effectiveIdentity);
    return entry ? entry->model : nullptr;
}
constexpr uint32_t PortCount=9,PortMask=(1U<<PortCount)-1,PipeCount=3;
// A validated native association. The adapter validates transport-specific
// record contents before construction; the core does not infer HDMI or DP.
struct NativePorts {
    const void *platform {};
    uint32_t selected {};
    bool valid() const {return platform && selected && !(selected&~PortMask);}
    bool bound(const void *config,uint32_t ddi) const {
        return valid() && ddi<PortCount && (selected&(1U<<ddi)) &&
            config==static_cast<const uint8_t *>(platform)+0x20+24*ddi;
    }
};
// ABI4 includes physical/effective identity and binds a profile and immutable probe policy, not mutable connection state.
// legacy permits probing for mode4; it does not force mode4 or classify a
// USB-C-capable connector as physically fixed DP. The provider still checks FIA.
// Type-C intent is represented but not admitted by either current backend.
struct ProbeConfig {
    uint32_t abi {4},bytes {sizeof(ProbeConfig)};
    Profile profile;
    uint32_t combo {},legacy {},typeC {},excluded {},comboBackend {},legacyBackend {},internal {};
    bool valid() const {
        if(abi!=4 || bytes!=sizeof(ProbeConfig) || !profile.admitted() ||
           !same(profile,identify(profile.identity,profile.revision,profile.effectiveIdentity)))return false;
        if((combo&~7U) || ((legacy|typeC|excluded|legacyBackend)&~0x1F8U) ||
           (comboBackend&~7U) || (legacy&typeC) || (legacy&excluded) || (typeC&excluded))return false;
        if(profile.display==Display::XeD)return comboBackend==7 && !internal;
        // The Display-13 panel path is a physical DDI-A/eDP policy, not a
        // synthetic Combo HDMI record. It is mutually exclusive with Combo.
        if(profile.display!=Display::XeLPD || legacy || typeC || legacyBackend)return false;
        if(internal)return internal==1 && !combo && !comboBackend && !excluded;
        return combo && !(combo&~3U) && comboBackend && !(comboBackend&~3U);
    }
};
inline bool same(const ProbeConfig &a,const ProbeConfig &b) {
    return a.abi==b.abi && a.bytes==b.bytes && same(a.profile,b.profile) && a.combo==b.combo &&
        a.legacy==b.legacy && a.typeC==b.typeC && a.excluded==b.excluded &&
        a.comboBackend==b.comboBackend && a.legacyBackend==b.legacyBackend && a.internal==b.internal;
}
using ConfigureProbe = bool (*)(void *,const ProbeConfig *);
using ProbePort = uint32_t (*)(void *);
}
#endif
