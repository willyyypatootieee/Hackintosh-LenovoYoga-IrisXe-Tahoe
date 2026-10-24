// ADL-P DP-alt DDI mapping. Host-only until native shared locking is established.
#pragma once
#include "AdlpTypecTransaction.hpp"

namespace YogaTypec {
enum class ClockMapState : uint8_t { Idle, Select, Ungate, Ready, Failed, Gate, Clear, Quarantined };
enum class ClockMapResult : uint8_t {
    Progress, Ready, Complete, Busy, Unauthorized, ExistingOwner,
    UsersActive, ReadFailed, WriteUnverified, ClockLost, Quarantined
};

// Backend: lockClock/unlockClock serialize both registers against ALL native
// clock writers, acquired after native controller/per-port lifetime locks.
// authorized(route, epoch) proves retained identity, core/port power, ownership
// and TC-cold holds. These stay valid until unlock. clockReady verifies actual
// PLL power/request/enable/lock under this same exclusion, not a cached boolean.
// mappingQuiesced proves link/DDI buffer disabled and training users drained;
// unlike PLL quiescence it does NOT require the mapping already removed.
// readDirect/writeDirect are ordered actual MMIO, never synthesized state.
// No polling/sleep/native callbacks or allocation under lock. A failed write
// may have mutated: both pre/post masked values are reconciled on cleanup.
// Only own a previously NONE/gated mapping. Never disable/adopt a firmware or
// native mapping, even if its selector equals MG. Preserve all unrelated bits.
// Linux v6.12: select then ungate; teardown gates BEFORE clearing selector.
template<class Backend> class DdiClockSequence {
    Backend &io;
    Route route;
    uint64_t epoch;
    ClockMapState state = ClockMapState::Idle;
    bool touched = false;
    static constexpr uint32_t shared = 0x164280, selectMask = 0xf0000000, mg = 0x80000000;
    uint32_t selector() const { return 0x4610cu + 4 * route.tcIndex; }
    uint32_t off() const { return 1u << (12 + route.tcIndex); }
    struct Lock {
        Backend &io; bool held;
        explicit Lock(Backend &b) : io(b), held(io.lockClock()) {}
        ~Lock() { if (held) io.unlockClock(); }
    };
    ClockMapResult quarantine() { state = ClockMapState::Quarantined; return ClockMapResult::Quarantined; }
    ClockMapResult fail(ClockMapResult why) { state = ClockMapState::Failed; return why; }
    bool read(uint32_t address, uint32_t &out) {
        return io.readDirect(address, out) && out != UINT32_MAX;
    }
    bool snapshot(uint32_t &s, uint32_t &g) { return read(selector(), s) && read(shared, g); }
    bool update(uint32_t address, uint32_t before, uint32_t mask, uint32_t bits) {
        if ((before & mask) == bits) return true;
        uint32_t actual;
        return io.writeDirect(address, (before & ~mask) | bits) &&
            read(address, actual) && (actual & mask) == bits;
    }

public:
    DdiClockSequence(Backend &b, Route r, uint64_t e) : io(b), route(r), epoch(e) {}
    DdiClockSequence(const DdiClockSequence &) = delete;
    DdiClockSequence &operator=(const DdiClockSequence &) = delete;
    ClockMapState currentState() const { return state; }
    bool needsCleanup() const { return touched; }
    ClockMapResult start() {
        if (state == ClockMapState::Quarantined) return ClockMapResult::Quarantined;
        if (state != ClockMapState::Idle || !route.valid()) return ClockMapResult::Busy;
        Lock guard(io);
        if (!guard.held) return ClockMapResult::Busy;
        if (!io.authorized(route, epoch)) return ClockMapResult::Unauthorized;
        if (!io.mappingQuiesced(route, epoch)) return ClockMapResult::UsersActive;
        if (!io.clockReady(route, epoch)) return ClockMapResult::ClockLost;
        uint32_t s, g;
        if (!snapshot(s, g)) return ClockMapResult::ReadFailed;
        if ((s & selectMask) || !(g & off())) return ClockMapResult::ExistingOwner;
        state = ClockMapState::Select; return ClockMapResult::Progress;
    }
    ClockMapResult step() {
        if (state == ClockMapState::Quarantined) return ClockMapResult::Quarantined;
        if (state == ClockMapState::Idle) return ClockMapResult::Complete;
        if (state == ClockMapState::Failed) return ClockMapResult::WriteUnverified;
        Lock guard(io);
        if (!guard.held) return ClockMapResult::Busy;
        if (!io.authorized(route, epoch)) return quarantine();
        const bool cleaning = state == ClockMapState::Gate || state == ClockMapState::Clear;
        if (state != ClockMapState::Ready && !io.mappingQuiesced(route, epoch))
            return ClockMapResult::UsersActive;
        if (!cleaning && !io.clockReady(route, epoch)) return fail(ClockMapResult::ClockLost);
        uint32_t s, g;
        if (!snapshot(s, g)) return fail(ClockMapResult::ReadFailed);
        if (state == ClockMapState::Select) {
            // Revalidate after start: no blind stale snapshot across scheduling.
            if ((s & selectMask) || !(g & off())) return quarantine();
            touched = true; // BEFORE possibly mutating selector write
            if (!update(selector(), s, selectMask, mg)) return fail(ClockMapResult::WriteUnverified);
            state = ClockMapState::Ungate;
        } else if (state == ClockMapState::Ungate) {
            if ((s & selectMask) != mg || !(g & off())) return quarantine();
            if (!update(shared, g, off(), 0)) return fail(ClockMapResult::WriteUnverified);
            state = ClockMapState::Ready; return ClockMapResult::Ready;
        } else if (state == ClockMapState::Ready) {
            if ((s & selectMask) != mg || (g & off())) return fail(ClockMapResult::WriteUnverified);
            return ClockMapResult::Ready;
        } else if (state == ClockMapState::Gate) {
            // After an unverified write the selector may be pre-state NONE or
            // our MG. A third value belongs to an unaccounted writer.
            if ((s & selectMask) != 0 && (s & selectMask) != mg) return quarantine();
            if (!update(shared, g, off(), off())) return fail(ClockMapResult::WriteUnverified);
            state = ClockMapState::Clear;
        } else if (state == ClockMapState::Clear) {
            if (!(g & off()) || ((s & selectMask) != 0 && (s & selectMask) != mg)) return quarantine();
            if (!update(selector(), s, selectMask, 0)) return fail(ClockMapResult::WriteUnverified);
            touched = false; state = ClockMapState::Idle; return ClockMapResult::Complete;
        }
        return ClockMapResult::Progress;
    }
    ClockMapResult stop() {
        if (state == ClockMapState::Quarantined) return ClockMapResult::Quarantined;
        if (!touched) { state = ClockMapState::Idle; return ClockMapResult::Complete; }
        if (state != ClockMapState::Gate && state != ClockMapState::Clear) state = ClockMapState::Gate;
        return step();
    }
};
} // namespace YogaTypec
