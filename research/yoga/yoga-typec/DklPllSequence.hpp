// Bounded, resumable PLL executor; no native backend is installed by this file.
#pragma once
#include "AdlpDklPllPlan.hpp"
#include "NativeClockInputs.hpp"

namespace YogaTypec {
enum class PllState : uint8_t {
    Idle, PowerWait, Program, LockWait, Ready, Failed,
    UnlockWait, PowerOffWait, Quarantined
};
enum class PllResult : uint8_t {
    Progress, Ready, Complete, Busy, InvalidPlan, Unauthorized,
    ExistingOwner, ReadFailed, WriteUnverified, TimedOut, UsersActive, Quarantined,
    ClockInputsChanged
};

// Backend requirements:
// - caller retains power/ownership from ResumableTransaction until Idle.
// - all methods execute under established native lifetime/controller/per-port
//   serialization; authorized(route, epoch) checks that EXACT retained lease.
//   Losing authorization quarantines; it never authorizes cleanup on a new epoch.
// - readDirect/writeDirect are bounded ordered actual MMIO, never emulated state.
// - updateIndexed does one globally selector-serialized preserved-bit RMW and
//   readback, returning true ONLY on verified success. false may have mutated.
// - native clock writers/admission are excluded for the entire lease; quiesced
//   proves link disabled, DDI clock gated/unmapped, training/AUX users drained.
// - nowMicros is monotonic; step does not sleep or spin. Schedule subsequent
//   steps outside shared selector locks; deadlines are elapsed time, not retries.
// - NativeClockInputGuard holds input/lifetime exclusion through each forward
//   operation, including power request, all indexed writes and enable. Native
//   init/reconfiguration must participate in this exclusion; a private lock is
//   NOT sufficient. Lock order is input/lifetime BEFORE shared selector lock.
//   Cleanup deliberately requires retained identity/power, NOT valid inputs.
// No destructor hardware cleanup, speculative adoption, forced completion or
// second power-reference decrement. Direct PLL bit ownership is not AUX count.
template<class Backend> class DklPllSequence {
    Backend &io;
    Route route;
    uint64_t epoch;
    DklPllPlan plan{};
    uint32_t referenceKHz = 0;
    bool overrideAfc = false;
    unsigned afcStartup = 0;
    PllState state = PllState::Idle;
    PllResult cause = PllResult::Complete;
    unsigned nextWrite = 0;
    uint64_t since = 0;
    bool requestMayExist = false;
    static constexpr uint32_t power = 1u << 27, powered = 1u << 26;
    static constexpr uint32_t enable = 1u << 31, locked = 1u << 30;

    bool authorized() {
        if (io.authorized(route, epoch)) return true;
        if (requestMayExist) state = PllState::Quarantined;
        cause = PllResult::Unauthorized;
        return false;
    }
    PllResult fail(PllResult reason) {
        cause = reason; state = PllState::Failed; return reason;
    }
    bool read(uint32_t &value) {
        return io.readDirect(plan.directEnableRegister, value) && value != UINT32_MAX;
    }
    bool inputsMatch(const NativeClockInputs &inputs) const {
        return inputs.referenceKHz == referenceKHz &&
            inputs.afc.overrideAfc == overrideAfc && inputs.afc.startup == afcStartup;
    }
    bool update(uint32_t mask, uint32_t bits) {
        uint32_t before, actual;
        if (!read(before)) return false;
        if ((before & mask) == bits) return true;
        if (!io.writeDirect(plan.directEnableRegister, (before & ~mask) | bits)) return false;
        return read(actual) && (actual & mask) == bits;
    }
    PllResult wait(uint32_t mask, bool set, PllState next) {
        uint32_t value;
        if (!read(value)) return fail(PllResult::ReadFailed);
        // Request bits must still belong to this sequence, not another writer.
        const uint32_t want = state == PllState::PowerWait ? power :
            state == PllState::LockWait ? power | enable :
            state == PllState::UnlockWait ? power : 0;
        if ((value & (power | enable)) != want) {
            state = PllState::Quarantined; return cause = PllResult::Quarantined;
        }
        if (set ? (value & mask) == mask : !(value & mask)) {
            state = next;
            return PllResult::Progress;
        }
        const uint64_t now = io.nowMicros();
        if (now < since || now - since >= 1000) return fail(PllResult::TimedOut);
        return PllResult::Progress;
    }

public:
    DklPllSequence(Backend &backend, Route r, uint64_t leaseEpoch)
        : io(backend), route(r), epoch(leaseEpoch) {}
    DklPllSequence(const DklPllSequence &) = delete;
    DklPllSequence &operator=(const DklPllSequence &) = delete;
    PllState currentState() const { return state; }
    PllResult failureCause() const { return cause; }
    bool needsCleanup() const { return requestMayExist; }
    bool ready() const { return state == PllState::Ready; }

    PllResult start(uint32_t rate, uint32_t reference, bool afcOverride = false, unsigned afc = 0) {
        if (state == PllState::Quarantined) return PllResult::Quarantined;
        if (state != PllState::Idle) return PllResult::Busy;
        if (!planDklDpPll(route, Mode::DpAlt, rate, reference, afcOverride, afc, plan))
            return PllResult::InvalidPlan;
        if (!authorized()) return PllResult::Unauthorized;
        NativeClockInputGuard<Backend> guard(io);
        NativeClockInputs inputs{};
        const auto inputResult = guard.read(route, epoch, inputs);
        if (inputResult == ClockInputResult::Busy) return PllResult::Busy;
        referenceKHz = reference; overrideAfc = afcOverride; afcStartup = afc;
        if (inputResult != ClockInputResult::Ok || !inputsMatch(inputs))
            return PllResult::ClockInputsChanged;
        if (!io.quiesced(route, epoch)) return PllResult::UsersActive;
        uint32_t before;
        if (!read(before)) return PllResult::ReadFailed;
        // Preexisting requests OR state are not ours. Caller must reconcile
        // them, not clear them or assume a matching frequency means ownership.
        if (before & (power | powered | enable | locked)) return PllResult::ExistingOwner;
        nextWrite = 0;
        cause = PllResult::Complete;
        requestMayExist = true; // set BEFORE a possibly mutating callback
        if (!update(power, power)) return fail(PllResult::WriteUnverified);
        state = PllState::PowerWait; since = io.nowMicros();
        return PllResult::Progress;
    }

    PllResult step() {
        if (state == PllState::Quarantined) return PllResult::Quarantined;
        if (state == PllState::Idle) return PllResult::Complete;
        if (!authorized()) return PllResult::Unauthorized;
        if (state == PllState::Failed) return cause;
        // Cleanup must remain possible after clock-input loss on the SAME
        // retained device. Only forward programming/consumption takes this guard.
        if (state == PllState::UnlockWait || state == PllState::PowerOffWait)
            return stepLocked();
        NativeClockInputGuard<Backend> guard(io);
        NativeClockInputs inputs{};
        const auto result = guard.read(route, epoch, inputs);
        if (result == ClockInputResult::Busy) return PllResult::Busy;
        if (result != ClockInputResult::Ok || !inputsMatch(inputs))
            return fail(PllResult::ClockInputsChanged);
        return stepLocked();
    }

private:
    PllResult stepLocked() {
        if (state == PllState::Ready) {
            uint32_t value;
            if (!read(value)) return fail(PllResult::ReadFailed);
            if ((value & (power | powered | enable | locked)) != (power | powered | enable | locked))
                return fail(PllResult::WriteUnverified);
            return PllResult::Ready;
        }
        // Training must remain closed during configuration AND cleanup.
        if (!io.quiesced(route, epoch)) return PllResult::UsersActive;
        if (state == PllState::PowerWait) return wait(powered, true, PllState::Program);
        if (state == PllState::Program) {
            uint32_t control;
            if (!read(control)) return fail(PllResult::ReadFailed);
            if ((control & (power | powered | enable | locked)) != (power | powered))
                return fail(PllResult::WriteUnverified);
            if (nextWrite < 8) {
                const auto w = plan.writes[nextWrite];
                if (!io.updateIndexed(route, epoch, w.offset, w.mask, w.bits))
                    return fail(PllResult::WriteUnverified);
                ++nextWrite;
                return PllResult::Progress;
            }
            if (!update(enable, enable)) return fail(PllResult::WriteUnverified);
            state = PllState::LockWait; since = io.nowMicros();
            return PllResult::Progress;
        }
        if (state == PllState::LockWait) {
            auto result = wait(powered | locked, true, PllState::Ready);
            return state == PllState::Ready ? PllResult::Ready : result;
        }
        if (state == PllState::UnlockWait) {
            auto result = wait(locked, false, PllState::PowerOffWait);
            if (state != PllState::PowerOffWait) return result;
            if (!update(power, 0)) return fail(PllResult::WriteUnverified);
            since = io.nowMicros();
            return PllResult::Progress;
        }
        if (state == PllState::PowerOffWait) {
            auto result = wait(powered | locked, false, PllState::Idle);
            if (state == PllState::Idle) { requestMayExist = false; return PllResult::Complete; }
            return result;
        }
        return fail(PllResult::WriteUnverified);
    }

public:
    PllResult stop() {
        if (state == PllState::Quarantined) return PllResult::Quarantined;
        if (!requestMayExist) return PllResult::Complete;
        if (!authorized()) return PllResult::Unauthorized;
        if (!io.quiesced(route, epoch)) return PllResult::UsersActive;
        if (state == PllState::UnlockWait || state == PllState::PowerOffWait)
            return step();
        // Verify ownership of request before consuming it. A power request
        // absent after a failed write is safe to drain; another enable is not.
        uint32_t control;
        if (!read(control)) return fail(PllResult::ReadFailed);
        if ((control & enable) && !(control & power)) {
            state = PllState::Quarantined; return cause = PllResult::Quarantined;
        }
        if (!update(enable, 0)) return fail(PllResult::WriteUnverified);
        if (!(control & power)) {
            state = PllState::PowerOffWait;
        } else state = PllState::UnlockWait;
        since = io.nowMicros();
        return PllResult::Progress;
    }
};
} // namespace YogaTypec
