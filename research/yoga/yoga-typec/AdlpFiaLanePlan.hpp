// Offline ADL-P DP-alt lane programming plan. No register access or live hooks.
// Reference: Linux v6.12 intel_tc_port_set_fia_lane_count(), non-legacy path.
#pragma once
#include "AdlpTypecTransaction.hpp"

namespace YogaTypec {
enum class LanePlanResult : uint8_t {
    Ok, InvalidRoute, WrongMode, InvalidLanes, InvalidRead,
    InvalidAssignment, InsufficientLanes
};

struct LanePlan {
    uint32_t address;
    uint32_t mask;
    uint32_t bits;
    uint32_t value;
};

// Caller supplies snapshots read under verified power, TC-cold hold, ownership,
// and per-port PLUS shared-FIA serialization. Local ports share a register.
// This function neither acquires those resources nor authorizes an MMIO write.
// DP-alt does not infer lane reversal from the raw assignment nibble: Linux's
// non-legacy programming chooses ML0 / ML1_0 / ML3_0, independently of it.
// Output is untouched on every error. TBT and legacy are intentionally excluded.
inline LanePlanResult planDpAltLanes(Route route, Mode mode, unsigned lanes,
                                    uint32_t dpsp, uint32_t dpmle,
                                    LanePlan &output) {
    if (!route.valid()) return LanePlanResult::InvalidRoute;
    if (mode != Mode::DpAlt) return LanePlanResult::WrongMode;
    if (lanes != 1 && lanes != 2 && lanes != 4)
        return LanePlanResult::InvalidLanes;
    if (dpsp == UINT32_MAX || dpmle == UINT32_MAX)
        return LanePlanResult::InvalidRead;
    const unsigned available = laneCount(dpsp, route);
    if (!available) return LanePlanResult::InvalidAssignment;
    if (available < lanes) return LanePlanResult::InsufficientLanes;
    const unsigned shift = 4 * route.fiaLocalIndex(); // NOT DPSP's 8-bit stride
    const uint32_t mask = uint32_t(15) << shift;
    const uint32_t bits = uint32_t(lanes == 4 ? 15 : lanes == 2 ? 3 : 1) << shift;
    const uint32_t base = route.fiaInstance() == 0 ? 0x163000 : 0x16e000;
    output = LanePlan{base + 0x8c0, mask, bits, (dpmle & ~mask) | bits};
    return LanePlanResult::Ok;
}
} // namespace YogaTypec
