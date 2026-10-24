// Yoga ADL-P Type-C transaction core. NO kernel or MMIO implementation.
// This is an offline-tested integration component, NOT an installable repair.
// Ordering reference: Linux v6.12 intel_tc.c adlp_tc_phy_{connect,disconnect}.
#pragma once
#include <stdint.h>

namespace YogaTypec {
enum class Mode : uint8_t { Disconnected, DpAlt, TbtAlt, Ambiguous };
enum class Result : uint8_t {
    Ok, InvalidLanes, Busy, NoSink, Ambiguous, PowerUnavailable,
    OwnershipFailed, NotReady, ColdBlockFailed, ModeChanged, InvalidLaneMask,
    InsufficientLanes
};

struct Route {
    uint8_t tcIndex; // ZERO-based TC index, not Apple's DDI or AUX identifier.
    uint8_t fiaInstance() const { return tcIndex / 2; }
    uint8_t fiaLocalIndex() const { return tcIndex % 2; }
    uint8_t laneShift() const { return fiaLocalIndex() * 8; }
    bool valid() const { return tcIndex < 4; } // ADL-P only; no TC5/TC6.
};

inline Mode liveMode(uint32_t cpuIsr, uint32_t pchIsr,
                     uint32_t dpMask, uint32_t tbtMask, uint32_t legacyMask) {
    // Failed / inaccessible reads cannot become a synthetic connected sink.
    if (cpuIsr == UINT32_MAX || pchIsr == UINT32_MAX) return Mode::Ambiguous;
    const bool dp = (cpuIsr & dpMask) != 0;
    const bool tbt = (cpuIsr & tbtMask) != 0;
    const bool legacy = (pchIsr & legacyMask) != 0;
    if (legacy || (dp && tbt)) return Mode::Ambiguous; // legacy not supported here
    return dp ? Mode::DpAlt : tbt ? Mode::TbtAlt : Mode::Disconnected;
}

inline unsigned laneCount(uint32_t fiaDpsp, Route route) {
    if (!route.valid() || fiaDpsp == UINT32_MAX) return 0;
    switch ((fiaDpsp >> route.laneShift()) & 0xf) {
        case 1: case 2: case 4: case 8: return 1;
        case 3: case 12: return 2;
        case 15: return 4;
        default: return 0; // stricter than Linux's warning + one-lane fallback
    }
}

// Backend contract:
// - External per-port serialization covers every state transition; a retained
//   driver object and reference accounting protect the entire active lifetime.
// - acquireCore / acquirePort / blockCold return owned tokens on true ONLY.
// - release/unblock and setOwnership(false) cannot fail; an adapter unable to
//   guarantee cleanup MUST NOT use this core. No exceptions may cross callbacks.
// - setOwnership uses a powered RMW of DDI_BUF_CTL bit 6, preserving other bits.
//   On false it leaves ownership unchanged (or has already rolled it back).
// - ready() is a bounded powered TCSS_DDI_STATUS bit-2 check; all-ones is false.
// - readMode() uses powered HPD status, never FIA global mode bits.
// - readLaneMask() reads the route's modular FIA only while TC-cold is blocked.
// - blockCold(DpAlt) holds the matching AUX power; TbtAlt holds TC_COLD_OFF.
// - Driver integration owns reference counts, hotplug epochs, link programming,
//   lane selection, AUX transfers, DKL/PLL training and display-engine quiesce.
//   Shared-FIA lane-register RMW needs sibling-port serialization in addition
//   to this core's per-port lock; a lane plan is not a hardware transaction.
//   disconnect() MUST run after link/AUX users stop; it cannot stop them itself.
// - Existing firmware/Apple ownership must be reconciled before constructing
//   this object. This core must not clear somebody else's ownership/holds.
template<class Backend> class Transaction {
    Backend &io;
    Route route;
    Mode activeMode = Mode::Disconnected;
    bool core = false, port = false, owned = false, cold = false;

    void putTransient() {
        if (port) { io.releasePort(route); port = false; }
        if (core) { io.releaseCore(); core = false; }
    }
    Result fail(Result result, Mode attempted) {
        if (cold) { io.unblockCold(route, attempted); cold = false; }
        if (owned) { io.setOwnership(route, false); owned = false; }
        putTransient();
        return result;
    }

public:
    Transaction(Backend &backend, Route r) : io(backend), route(r) {}
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    // No destructor power/MMIO calls: detach must explicitly quiesce users and
    // disconnect under the driver's native lock before releasing the object.
    bool active() const { return activeMode != Mode::Disconnected; }
    Mode mode() const { return activeMode; }

    Result connect(unsigned lanes) {
        if (active()) return Result::Busy;
        if (!route.valid() || (lanes != 1 && lanes != 2 && lanes != 4))
            return Result::InvalidLanes;
        if (!(core = io.acquireCore())) return Result::PowerUnavailable;
        const Mode target = io.readMode(route);
        if (target == Mode::Disconnected) return fail(Result::NoSink, target);
        if (target == Mode::Ambiguous) return fail(Result::Ambiguous, target);
        if (target == Mode::DpAlt) {
            if (!(port = io.acquirePort(route)))
                return fail(Result::PowerUnavailable, target);
            if (!io.setOwnership(route, true))
                return fail(Result::OwnershipFailed, target);
            owned = true;
            if (!io.ready(route)) return fail(Result::NotReady, target);
        }
        if (!(cold = io.blockCold(route, target)))
            return fail(Result::ColdBlockFailed, target);
        // Additional fail-closed check for mode changes during acquisition.
        // TBT deliberately never takes direct DP PHY ownership / lane reads.
        if (io.readMode(route) != target) return fail(Result::ModeChanged, target);
        if (target == Mode::DpAlt) {
            const unsigned available = laneCount(io.readLaneMask(route), route);
            if (!available) return fail(Result::InvalidLaneMask, target);
            if (available < lanes) return fail(Result::InsufficientLanes, target);
        }
        activeMode = target;
        putTransient(); // TC-cold hold + ownership persist across link lifetime
        return Result::Ok;
    }

    Result disconnect() {
        if (!active()) return Result::Ok;
        // Acquire before releasing resources. Failure leaves the active lease
        // intact, so the caller can retry; do not pretend detach succeeded.
        if (!(port = io.acquirePort(route))) return Result::PowerUnavailable;
        io.unblockCold(route, activeMode); cold = false;
        if (owned) { io.setOwnership(route, false); owned = false; }
        io.releasePort(route); port = false;
        activeMode = Mode::Disconnected;
        return Result::Ok;
    }
};
} // namespace YogaTypec
