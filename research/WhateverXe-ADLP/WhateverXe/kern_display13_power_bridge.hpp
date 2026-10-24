// Exact c004fc1a TGL framebuffer PowerWell reference scopes on Display 13.
// Native allocateDisplayPathResources selects PG1 for A, PG3 for B, PG4 for C;
// enableController/hotplug also use a temporary, coarse PG3 reference. These
// are ABI scopes, not physical well indices. Caller serializes this object.
// Routing native init/engine/restore and verifying IRQ/clock ownership remain
// required; never install just get/put over the original TGL PowerWell init.
#ifndef kern_display13_power_bridge_hpp
#define kern_display13_power_bridge_hpp
#include "kern_display13_power.hpp"
#include <stddef.h>

namespace Display13Power {
inline uint32_t nativeScope(unsigned pg) {
    uint32_t domains = bit(Domain::PipeA);
    if (pg >= 2) domains |= bit(Domain::PW2);
    if (pg >= 3) domains |= bit(Domain::PipeB);
    if (pg >= 4) domains |= bit(Domain::PipeC);
    return pg >= 1 && pg <= 4 ? domains : 0;
}

// Combo A/B/C and ADL-N TC1..4 use the native five PG scopes, but
// expose DDI/AUX power only for explicitly selected connectors.
inline uint32_t ddiDomain(unsigned port) {
    return port<3 ? bit(port==2 ? Domain::DDIC : port ? Domain::DDIB : Domain::DDIA) : port<7 ? 1U<<(11+2*(port-3)) : 0;
}
inline uint32_t auxDomain(unsigned port) {
    return port<3 ? bit(port==2 ? Domain::AUXC : port ? Domain::AUXB : Domain::AUXA) : port<7 ? 1U<<(12+2*(port-3)) : 0;
}
inline uint32_t nativeDomains(uint32_t ports) {
    if (!ports || (ports & ~0x7FU)) return 0;
    uint32_t domains=31;
    for(unsigned port=0;port<7;++port)if(ports&(1U<<port))domains|=ddiDomain(port)|auxDomain(port);
    return domains;
}

struct NativeBridge {
    State state;
    Lease pgLease[4], ddiLease[7], auxLease[7];
    uint32_t pgRefs[4] {}, ddiRefs[7] {}, auxRefs[7] {};
    uint32_t selectedPorts {2};
    unsigned bootPort {1}; // Physical DDI selected by the measured boot pipe.
    bool begun {};
    bool bootPGProtected {}, bootDDIProtected {}, bootAUXProtected {};
    uintptr_t bootPath {};
    bool bootAdopted {}; // Handoff history survives removal of the boot path identity.

    // Preserve the native 5-slot accounting ABI: slot0 is always-on; the
    // other slots represent hierarchical *native scopes*. Hardware requests
    // and the independent PW_A/B/C refcounts live in state, never these slots.
    uint32_t nativeRefs(unsigned slot) const {
        if (!begun || slot > 4) return 0;
        if (!slot) return 1;
        uint64_t refs = 0;
        for (unsigned pg = slot; pg <= 4; ++pg) refs += pgRefs[pg-1];
        return refs <= UINT32_MAX ? static_cast<uint32_t>(refs) : UINT32_MAX;
    }
    bool acceptsPort(unsigned port) const { return port < 7 && (selectedPorts & (1U << port)); }
    uint32_t nativeDDIRefs(unsigned port) const { return port < 7 ? ddiRefs[port] : 0; }
    uint32_t nativeAUXRefs(unsigned port) const { return port < 7 ? auxRefs[port] : 0; }
    uint32_t nativeDDIBRefs() const { return nativeDDIRefs(1); }
    uint32_t nativeAUXBRefs() const { return nativeAUXRefs(1); }

    template<class IO> Result begin(IO &io, unsigned port=1, uint32_t ports=2,bool retainBootAUX=false) {
        Result r;
        if (begun || state.ready) return r;
        const auto allowed = nativeDomains(ports);
        if (!allowed || port >= 7 || !(ports & (1U << port))) {
            r.status = Status::InvalidDomains; return r;
        }
        if (!initialize(io,state,allowed)) return r;
        selectedPorts = ports; bootPort = port;
        r = acquire(io,state,pgLease[0],nativeScope(1));
        if (r.status != Status::Complete) return r;
        const auto pgWrites = r.writes;
        pgRefs[0] = 1; bootPGProtected = true;
        r = acquire(io,state,ddiLease[port],ddiDomain(port));
        r.writes += pgWrites;
        if (r.status != Status::Complete) return r; // Retain the A lease on failure.
        ddiRefs[port] = 1; bootDDIProtected = true;
        if(retainBootAUX) {
            const auto ddiWrites=r.writes;
            r=acquire(io,state,auxLease[port],auxDomain(port));
            r.writes+=ddiWrites;
            if(r.status!=Status::Complete)return r; // Retain boot DDI on failure.
            auxRefs[port]=1;bootAUXProtected=true;
        }
        begun = true;
        return r;
    }
    template<class IO> Result getPG(IO &io, unsigned pg) {
        Result r;
        if (!begun || state.faulted || state.suspended) return r;
        // The donor's PG0 entry returns without changing a reference. It is
        // the always-on scope, not an independent hardware power well.
        if (!pg) { r.status = Status::Complete; return r; }
        const auto domains = nativeScope(pg);
        if (!domains) { r.status = Status::InvalidDomains; return r; }
        // One get increments every lower native slot. Check the aggregate,
        // not just the requested scope's own counter, before any MMIO.
        for (unsigned slot = 1; slot <= pg; ++slot) if (nativeRefs(slot) == UINT32_MAX) {
            r.status = Status::Overflow; r.domain = slot; return r;
        }
        if (!pgRefs[pg-1]) {
            r = acquire(io,state,pgLease[pg-1],domains);
            if (r.status != Status::Complete) return r;
        }
        ++pgRefs[pg-1]; r.status = Status::Complete;
        return r;
    }
    template<class IO> Result putPG(IO &io, unsigned pg) {
        Result r;
        if (!begun || state.faulted || state.suspended) return r;
        if (!pg) { r.status = Status::Complete; return r; }
        if (!nativeScope(pg)) { r.status = Status::InvalidDomains; return r; }
        if (!pgRefs[pg-1]) { r.status = Status::Unbalanced; return r; }
        if (pg == 1 && pgRefs[0] == 1 && bootPGProtected) { r.status = Status::Unbalanced; return r; }
        if (pgRefs[pg-1] == 1) {
            r = release(io,state,pgLease[pg-1]);
            if (r.status != Status::Complete) return r;
        }
        --pgRefs[pg-1]; r.status = Status::Complete;
        return r;
    }
    template<class IO> Result getDDI(IO &io, unsigned port) {
        Result r;
        if (!begun || state.faulted || state.suspended) return r;
        if (!acceptsPort(port)) { r.status = Status::InvalidDomains; return r; }
        if (ddiRefs[port] == UINT32_MAX) { r.status = Status::Overflow; return r; }
        if (!ddiRefs[port]) {
            r = acquire(io,state,ddiLease[port],ddiDomain(port));
            if (r.status != Status::Complete) return r;
        }
        ++ddiRefs[port]; r.status = Status::Complete;
        return r;
    }
    template<class IO> Result putDDI(IO &io, unsigned port) {
        Result r;
        if (!begun || state.faulted || state.suspended) return r;
        if (!acceptsPort(port)) { r.status = Status::InvalidDomains; return r; }
        if (!ddiRefs[port]) { r.status = Status::Unbalanced; return r; }
        if (port == bootPort && ddiRefs[port] == 1 && bootDDIProtected) { r.status = Status::Unbalanced; return r; }
        if (ddiRefs[port] == 1) {
            r = release(io,state,ddiLease[port]);
            if (r.status != Status::Complete) return r;
        }
        --ddiRefs[port]; r.status = Status::Complete;
        return r;
    }
    // Native port discovery brackets AUX operations even for the selected
    // HDMI combo port. Hold its real Display13 AUX well and PW1 dependency;
    // this does not change the connector type or emulate an AUX response.
    template<class IO> Result getAUX(IO &io, unsigned port) {
        Result r;
        if (!begun || state.faulted || state.suspended) return r;
        if (!acceptsPort(port)) { r.status = Status::InvalidDomains; return r; }
        if (auxRefs[port] == UINT32_MAX) { r.status = Status::Overflow; return r; }
        if (!auxRefs[port]) {
            r = acquire(io,state,auxLease[port],auxDomain(port));
            if (r.status != Status::Complete) return r;
        }
        ++auxRefs[port]; r.status = Status::Complete;
        return r;
    }
    template<class IO> Result putAUX(IO &io, unsigned port) {
        Result r;
        if (!begun || state.faulted || state.suspended) return r;
        if (!acceptsPort(port)) { r.status = Status::InvalidDomains; return r; }
        if (!auxRefs[port]) { r.status = Status::Unbalanced; return r; }
        if(port==bootPort && auxRefs[port]==1 && bootAUXProtected) {r.status=Status::Unbalanced;return r;}
        if (auxRefs[port] == 1) {
            r = release(io,state,auxLease[port]);
            if (r.status != Status::Complete) return r;
        }
        --auxRefs[port]; r.status = Status::Complete;
        return r;
    }
    // Native boot allocation skips getPG, while free always puts PG. Transfer
    // the initial references to that exact boot path instead of requiring a
    // duplicate get and subsequently leaking or underflowing its reference.
    // Caller proves path/controller/port associations, surface and IRQ handoff.
    // No physical request is cleared or predecessor ownership adopted here.
    Result adoptBootPath(uintptr_t path, unsigned pipe, unsigned port) {
        Result r;
        if (!begun || state.faulted) return r;
        if (!path || pipe >= 3 || port != bootPort || !acceptsPort(port)) { r.status = Status::InvalidDomains; return r; }
        if (bootAdopted || bootPath || !bootPGProtected || !bootDDIProtected ||
            (bootAUXProtected && !auxRefs[port]) || !pgRefs[0] || !ddiRefs[port]) {
            r.status = Status::Unbalanced; return r;
        }
        bootPath = path; bootAdopted = true; bootPGProtected = bootDDIProtected = bootAUXProtected = false;
        r.status = Status::Complete;
        return r;
    }
    bool forgetBootPath(uintptr_t path) {
        if(!bootAdopted || !path || bootPath!=path)return false;
        bootPath=0;return true; // No power-reference mutation or second handoff.
    }

};

// Native object layout for this donor only. The kernel route additionally
// checks exact image/prologues and the controller's physical PCI identity.
// Validate CPU associations before any bridge begin/MMIO operation.
inline bool objectAssociations(void *power, size_t powerBytes, void *controller, size_t controllerBytes) {
    if (!power || !controller || powerBytes < 0x80 || controllerBytes < 0xC60) return false;
    auto c = static_cast<uint8_t *>(controller);
    return *reinterpret_cast<void **>(c+0xC8) == power &&
        *reinterpret_cast<void **>(c+0xC40) && !c[0x918] && !(c[0xC59]&1);
}
inline bool publishObject(const NativeBridge &b, void *power, size_t powerBytes,
                          void *controller, size_t controllerBytes) {
    if (!b.begun || b.state.faulted || !objectAssociations(power,powerBytes,controller,controllerBytes)) return false;
    auto p = static_cast<uint8_t *>(power); auto c = static_cast<uint8_t *>(controller);
    *reinterpret_cast<void **>(p+0x10) = controller;
    *reinterpret_cast<void **>(p+0x78) = *reinterpret_cast<void **>(c+0xC40);
    p[0x18] = 0; // Never select the simulator PowerWell backend.
    for (unsigned slot=0; slot<5; ++slot)
        *reinterpret_cast<uint32_t *>(p+0x1C+4*slot) = b.nativeRefs(slot);
    for (unsigned port=0; port<9; ++port) {
        *reinterpret_cast<uint32_t *>(p+0x30+4*port) = b.nativeDDIRefs(port);
        *reinterpret_cast<uint32_t *>(p+0x54+4*port) = b.nativeAUXRefs(port);
    }
    return true;
}
}
#endif
