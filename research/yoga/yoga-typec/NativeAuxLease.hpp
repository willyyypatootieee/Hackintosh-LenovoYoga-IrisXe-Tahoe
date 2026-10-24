// Host-tested native AUX reference accounting; NOT a kernel/MMIO backend.
#pragma once
#include <stdint.h>

namespace YogaTypec {
enum class AuxLeaseState : uint8_t {
    Empty, HeldUnverified, Ready, ReleasePending, Quarantined
};
enum class AuxLeaseResult : uint8_t {
    Ok, Busy, InvalidSnapshot, Unverified, AccountingMismatch
};

struct AuxSnapshot {
    bool valid;
    uint32_t references;
    uint32_t control; // Actual AUX2 MMIO read, NEVER Apple's emulated state.
};

// Integration contract:
// - Caller holds the native serialization boundary for EVERY method, including
//   native enable/disable, both count snapshots and MMIO reads. This class does
//   not establish that all Apple callers obey that boundary.
// - Backend is bound to a retained, validated DDI5 port + powerwell object.
// - snapshot() validates object identity and reads the count and actual AUX2.
// - nativeEnable/nativeDisable mutate precisely one native reference. Return
//   values from those wrappers are deliberately ignored: they are not success.
// - No exceptions, polling, delays or allocation inside these methods.
// - Mode must stay DP-alt for the entire lease. Native disable selects its well
//   using the published mode; mode publication/lifetime is adapter work.
// - A mismatched count quarantines the lease: no guessing who owns a reference,
//   no automatic decrement, and no teardown/reuse until external reconciliation.
// - Other ports/firmware may hold hardware power after our driver request ends.
//   Release verifies OUR count/request, never demands a global state-bit clear.
template<class Backend> class NativeAuxLease {
    Backend &io;
    AuxLeaseState status = AuxLeaseState::Empty;
    static constexpr uint32_t request = 1u << 11; // TC3 AUX well index 5
    static constexpr uint32_t state = 1u << 10;

    static bool readable(const AuxSnapshot &s) {
        return s.valid && s.control != UINT32_MAX;
    }
    AuxLeaseResult quarantine() {
        status = AuxLeaseState::Quarantined;
        return AuxLeaseResult::AccountingMismatch;
    }
    AuxLeaseResult finishRelease(const AuxSnapshot &s) {
        // The native decrement is already consumed. A retry MUST NOT call it
        // again, even when the hardware request readback is unavailable.
        if (!readable(s) || s.references == UINT32_MAX)
            return AuxLeaseResult::Unverified;
        const bool requested = (s.control & request) != 0;
        if (requested != (s.references != 0)) return AuxLeaseResult::Unverified;
        status = AuxLeaseState::Empty;
        return AuxLeaseResult::Ok;
    }

public:
    explicit NativeAuxLease(Backend &backend) : io(backend) {}
    NativeAuxLease(const NativeAuxLease &) = delete;
    NativeAuxLease &operator=(const NativeAuxLease &) = delete;
    // Explicit cleanup only; destruction cannot silently release uncertain refs.
    AuxLeaseState currentState() const { return status; }
    bool ready() const { return status == AuxLeaseState::Ready; }
    bool needsCleanup() const { return status != AuxLeaseState::Empty; }

    AuxLeaseResult acquire() {
        if (status != AuxLeaseState::Empty) return AuxLeaseResult::Busy;
        const AuxSnapshot before = io.snapshot();
        if (!readable(before) || before.references >= UINT32_MAX - 1)
            return AuxLeaseResult::InvalidSnapshot;
        io.nativeEnable();
        status = AuxLeaseState::HeldUnverified;
        const AuxSnapshot after = io.snapshot();
        if (!after.valid || after.references != before.references + 1)
            return quarantine();
        return verifyAcquired(after);
    }

    AuxLeaseResult verify() {
        if (status == AuxLeaseState::Quarantined)
            return AuxLeaseResult::AccountingMismatch;
        if (status == AuxLeaseState::Empty) return AuxLeaseResult::Ok;
        const AuxSnapshot s = io.snapshot();
        if (status == AuxLeaseState::ReleasePending) return finishRelease(s);
        return verifyAcquired(s);
    }

    AuxLeaseResult release() {
        if (status == AuxLeaseState::Empty) return AuxLeaseResult::Ok;
        if (status == AuxLeaseState::Quarantined)
            return AuxLeaseResult::AccountingMismatch;
        if (status == AuxLeaseState::ReleasePending) return verify();
        const AuxSnapshot before = io.snapshot();
        // An unreadable MMIO value need not prevent balancing a known ref, but
        // a missing/zero/overflow count cannot authorize a decrement.
        if (!before.valid) return AuxLeaseResult::InvalidSnapshot;
        if (!before.references || before.references == UINT32_MAX)
            return quarantine();
        io.nativeDisable();
        status = AuxLeaseState::ReleasePending;
        const AuxSnapshot after = io.snapshot();
        if (!after.valid || after.references != before.references - 1)
            return quarantine();
        return finishRelease(after);
    }

private:
    AuxLeaseResult verifyAcquired(const AuxSnapshot &s) {
        if (!s.valid) {
            status = AuxLeaseState::HeldUnverified;
            return AuxLeaseResult::Unverified;
        }
        if (!s.references || s.references == UINT32_MAX) return quarantine();
        if (!readable(s) || (s.control & (request | state)) != (request | state)) {
            status = AuxLeaseState::HeldUnverified;
            return AuxLeaseResult::Unverified;
        }
        status = AuxLeaseState::Ready;
        return AuxLeaseResult::Ok;
    }
};
} // namespace YogaTypec
