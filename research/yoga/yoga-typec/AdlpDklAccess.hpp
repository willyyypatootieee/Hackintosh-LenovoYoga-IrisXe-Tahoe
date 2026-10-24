// Reviewed Dekel register addressing/access component; NO native MMIO backend.
// Linux v6.12 intel_dkl_phy{,_regs.h}: 4-KiB aperture + HIP bank selector.
#pragma once
#include "AdlpTypecTransaction.hpp"

namespace YogaTypec {
struct DklPlan {
    uint32_t selectorRegister;
    uint32_t selectorValue;
    uint32_t apertureRegister;
};

inline bool reviewedDklOffset(uint32_t offset) {
    // Only canonical internal offsets from the pinned Linux reference. Never
    // accept Apple's unchecked table indices or absolute MMIO table entries.
    switch (offset) {
        case 0x0014: case 0x1014: // PCS_DW5 lane groups 0/1
        case 0x00a0: case 0x10a0: // DP_MODE lane groups 0/1
        case 0x02c0: case 0x12c0: // TX_DPCNTL0
        case 0x02c4: case 0x12c4: // TX_DPCNTL1
        case 0x02c8: case 0x12c8: // TX_DPCNTL2
        case 0x02f8: case 0x12f8: // TX_FW_CALIB
        case 0x0d00: case 0x1d00: // TX_PMD_LANE_SUS
        case 0x0dc4: case 0x1dc4: // TX_DW17
        case 0x0dc8: case 0x1dc8: // TX_DW18
        case 0x20d4: case 0x20d8: // HSCLKCTL / CORECLKCTL1
        case 0x212c: // REFCLKIN_CTL
        case 0x2200: case 0x2204: // PLL_DIV0/1
        case 0x2210: case 0x2214: case 0x2218: // SSC / BIAS / TDC_COLDST
        case 0x236c: // CMN_UC_DW27 health (read only here)
            return true;
        default: return false;
    }
}

inline bool planDklAccess(Route route, uint32_t internalOffset, DklPlan &out) {
    if (!route.valid() || !reviewedDklOffset(internalOffset)) return false;
    const DklPlan plan = {
        0x1010a0u, // ADL-P TC1..TC4 all share HIP_INDEX_REG0
        ((internalOffset >> 12) & 0xfu) << (8 * route.tcIndex),
        0x168000u + 0x1000u * route.tcIndex + (internalOffset & 0xfffu)
    };
    out = plan;
    return true;
}

enum class DklResult : uint8_t {
    Ok, InvalidRegister, InvalidMask, Busy, Unauthorized, SelectorFailed,
    ReadFailed, WriteUnverified, ReadbackMismatch
};

// Backend contract:
// - lock/unlock serialize the ENTIRE selector + aperture read/write/RMW sequence
//   against every native DKL user on ALL ports sharing HIP_INDEX_REG0.
//   A private lock unused by Apple's original helpers is insufficient.
// - Native controller/per-port lock is acquired BEFORE this shared DKL lock;
//   lock-order coverage and native caller replacement remain integration work.
// - authorized(route) checks retained identity, DP-alt lease, epoch, power and
//   PHY ownership; those conditions cannot change before unlock.
// - read32/write32 are bounded, ordered backend operations. false on a write
//   means it MAY have reached hardware: no automatic rollback or blind retry.
// - No native calls, sleeps, polling, allocation or exceptions under the lock.
// - HIP_INDEX is written just as in Linux, not RMW: unselected port bank fields
//   may change. Global selector serialization, not preservation, prevents races.
//
// This component only mutates DP_MODE bits6/7 and PCS softreset bit11, whose
// semantics are pinned below. PLL and signal registers are read-only until
// their platform programming policy has separately been reviewed.
template<class Backend> class DklAccess {
    Backend &io;
    struct Lock {
        Backend &io;
        bool held;
        explicit Lock(Backend &backend) : io(backend), held(io.lock()) {}
        ~Lock() { if (held) io.unlock(); }
        Lock(const Lock &) = delete;
        Lock &operator=(const Lock &) = delete;
    };
    static uint32_t writableMask(uint32_t offset) {
        switch (offset) {
            case 0x00a0: case 0x10a0: return 0xc0u;
            case 0x0014: case 0x1014: return 1u << 11;
            default: return 0;
        }
    }

public:
    explicit DklAccess(Backend &backend) : io(backend) {}
    DklAccess(const DklAccess &) = delete;
    DklAccess &operator=(const DklAccess &) = delete;

    DklResult read(Route route, uint32_t offset, uint32_t &out) {
        DklPlan plan{};
        if (!planDklAccess(route, offset, plan)) return DklResult::InvalidRegister;
        Lock guard(io);
        if (!guard.held) return DklResult::Busy;
        if (!io.authorized(route)) return DklResult::Unauthorized;
        if (!io.write32(plan.selectorRegister, plan.selectorValue)) return DklResult::SelectorFailed;
        uint32_t value;
        if (!io.read32(plan.apertureRegister, value) || value == UINT32_MAX)
            return DklResult::ReadFailed;
        out = value;
        return DklResult::Ok;
    }

    DklResult update(Route route, uint32_t offset, uint32_t mask, uint32_t bits) {
        DklPlan plan{};
        if (!planDklAccess(route, offset, plan)) return DklResult::InvalidRegister;
        if (!mask || (mask & ~writableMask(offset)) || (bits & ~mask))
            return DklResult::InvalidMask;
        Lock guard(io);
        if (!guard.held) return DklResult::Busy;
        if (!io.authorized(route)) return DklResult::Unauthorized;
        if (!io.write32(plan.selectorRegister, plan.selectorValue)) return DklResult::SelectorFailed;
        uint32_t before;
        if (!io.read32(plan.apertureRegister, before) || before == UINT32_MAX)
            return DklResult::ReadFailed;
        const uint32_t after = (before & ~mask) | bits;
        if (after == before) return DklResult::Ok;
        if (!io.write32(plan.apertureRegister, after)) return DklResult::WriteUnverified;
        uint32_t actual;
        if (!io.read32(plan.apertureRegister, actual) || actual == UINT32_MAX)
            return DklResult::WriteUnverified;
        // Other status bits can legitimately change between reads. Only the
        // explicitly programmed writable bits are acknowledgement of this RMW.
        return (actual & mask) == bits ? DklResult::Ok : DklResult::ReadbackMismatch;
    }
};
} // namespace YogaTypec
