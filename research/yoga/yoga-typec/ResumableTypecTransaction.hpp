// Fallible-cleanup acquisition core, host-only until a reviewed backend exists.
#pragma once
#include "AdlpTypecTransaction.hpp"

namespace YogaTypec {
enum class Acquisition : uint8_t { Unavailable, Verified, CleanupRequired };
enum class Cleanup : uint8_t { Complete, Retry, Quarantined };
enum class LinkState : uint8_t { Idle, Ready, CleanupPending, Quarantined };
enum class LinkResult : uint8_t {
    Ok, Busy, InvalidLanes, NoSink, Ambiguous, PowerUnavailable,
    OwnershipFailed, NotReady, ColdBlockFailed, ModeChanged,
    InvalidLaneMask, InsufficientLanes, UsersActive,
    CleanupPending, Quarantined
};

// Integration contract (not provided by this template):
// - Same native per-port/controller serialization and retained identity/lifetime
//   requirements as AdlpTypecTransaction, plus sibling-FIA serialization for RMW.
// - acquire* / takeOwnership / blockCold return Unavailable ONLY if nothing is
//   left to clean. CleanupRequired retains a token even after partial failure.
// - release* / dropOwnership / unblockCold return Complete ONLY after consuming
//   their token and verifying driver request cleanup. Retry must be idempotent,
//   e.g. NativeAuxLease ReleasePending must not decrement twice. Quarantined
//   means accounting/identity is uncertain: no automatic mutation thereafter.
// - Preexisting ownership must be reconciled, not silently stolen or cleared.
// - quiesced() proves all native AUX/link users are stopped and admission is
//   closed under the same serialization. It is not merely a cached port mode.
// - Native modes/epochs must not change which power well a cleanup token names.
// - No exceptions across callbacks; bounded checks, no unreviewed lock sleeps.
//
// Unlike the earlier core this deliberately RETAINS core and DP port power for
// the entire lease, including failed cleanup. Power optimization is deferred:
// no ownership write may follow a failed/unverified power reacquisition.
template<class Backend> class ResumableTransaction {
    Backend &io;
    Route route;
    LinkState status = LinkState::Idle;
    Mode acquiredMode = Mode::Disconnected;
    LinkResult cause = LinkResult::Ok;
    bool core = false, port = false, owner = false, cold = false;

    static bool take(Acquisition result, bool &token) {
        token = result != Acquisition::Unavailable;
        return result == Acquisition::Verified;
    }
    bool drop(Cleanup result, bool &token) {
        if (result == Cleanup::Complete) { token = false; return true; }
        status = result == Cleanup::Quarantined ? LinkState::Quarantined
                                                : LinkState::CleanupPending;
        return false;
    }
    LinkResult incomplete() const {
        return status == LinkState::Quarantined ? LinkResult::Quarantined
                                                : LinkResult::CleanupPending;
    }
    LinkResult unwind() {
        // Reverse only OUR resources; stop at the first uncertain cleanup.
        // Power prerequisites remain retained while cold/ownership is uncertain.
        if (cold && !drop(io.unblockCold(route, acquiredMode), cold)) return incomplete();
        if (owner && !drop(io.dropOwnership(route), owner)) return incomplete();
        if (port && !drop(io.releasePort(route), port)) return incomplete();
        if (core && !drop(io.releaseCore(), core)) return incomplete();
        status = LinkState::Idle;
        acquiredMode = Mode::Disconnected;
        return LinkResult::Ok;
    }
    LinkResult abort(LinkResult reason) {
        cause = reason;
        status = LinkState::CleanupPending;
        const LinkResult cleaned = unwind();
        return cleaned == LinkResult::Ok ? reason : cleaned;
    }

public:
    ResumableTransaction(Backend &backend, Route r) : io(backend), route(r) {}
    ResumableTransaction(const ResumableTransaction &) = delete;
    ResumableTransaction &operator=(const ResumableTransaction &) = delete;
    // No automatic teardown: caller must quiesce and explicitly disconnect.
    LinkState currentState() const { return status; }
    bool ready() const { return status == LinkState::Ready; }
    bool needsCleanup() const { return core || port || owner || cold; }
    LinkResult failureCause() const { return cause; }
    Mode mode() const { return ready() ? acquiredMode : Mode::Disconnected; }

    LinkResult connect(unsigned lanes) {
        if (status == LinkState::Quarantined) return LinkResult::Quarantined;
        if (status != LinkState::Idle) return LinkResult::Busy;
        if (!route.valid() || (lanes != 1 && lanes != 2 && lanes != 4))
            return LinkResult::InvalidLanes;
        cause = LinkResult::Ok;
        if (!take(io.acquireCore(), core)) return abort(LinkResult::PowerUnavailable);
        acquiredMode = io.readMode(route);
        if (acquiredMode == Mode::Disconnected) return abort(LinkResult::NoSink);
        if (acquiredMode == Mode::Ambiguous) return abort(LinkResult::Ambiguous);
        if (acquiredMode == Mode::DpAlt) {
            if (!take(io.acquirePort(route), port)) return abort(LinkResult::PowerUnavailable);
            if (!take(io.takeOwnership(route), owner)) return abort(LinkResult::OwnershipFailed);
            if (!io.ready(route)) return abort(LinkResult::NotReady);
        }
        if (!take(io.blockCold(route, acquiredMode), cold)) return abort(LinkResult::ColdBlockFailed);
        if (io.readMode(route) != acquiredMode) return abort(LinkResult::ModeChanged);
        if (acquiredMode == Mode::DpAlt) {
            const unsigned available = laneCount(io.readLaneMask(route), route);
            if (!available) return abort(LinkResult::InvalidLaneMask);
            if (available < lanes) return abort(LinkResult::InsufficientLanes);
        }
        status = LinkState::Ready;
        return LinkResult::Ok;
    }

    LinkResult disconnect() {
        if (status == LinkState::Quarantined) return LinkResult::Quarantined;
        if (status == LinkState::Idle) return LinkResult::Ok;
        // Includes failed acquisition cleanup: a backend must not admit AUX/link
        // users until Ready, and must close admission before releasing anything.
        if (!io.quiesced(route)) return LinkResult::UsersActive;
        status = LinkState::CleanupPending;
        return unwind();
    }
};
} // namespace YogaTypec
