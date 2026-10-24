// DP-mode planning only. DFLEXPA1 pin assignment != DFLEXDPSP lane capacity.
#pragma once
#include "AdlpDklAccess.hpp"

namespace YogaTypec {
struct DklModePlan {
    uint32_t pinAssignmentRegister;
    uint32_t laneGroupBits[2]; // mask 0xc0, offsets 0x00a0 / 0x10a0
    uint8_t pinAssignment;
};

inline uint32_t dklPinAssignmentRegister(Route route) {
    if (!route.valid()) return 0;
    return (route.fiaInstance() == 0 ? 0x163000u : 0x16e000u) + 0x880;
}

inline bool planDklDpMode(Route route, Mode mode, unsigned lanes,
                          uint32_t fiaLaneStatus, uint32_t fiaPinStatus,
                          DklModePlan &out) {
    if (!route.valid() || mode != Mode::DpAlt || fiaPinStatus == UINT32_MAX ||
        (lanes != 1 && lanes != 2 && lanes != 4) ||
        laneCount(fiaLaneStatus, route) < lanes) return false;
    const unsigned pin = (fiaPinStatus >> (route.fiaLocalIndex() * 4)) & 0xf;
    DklModePlan plan{dklPinAssignmentRegister(route), {0, 0}, static_cast<uint8_t>(pin)};
    // Exact non-legacy cases from Linux v6.12 icl_program_mg_dp_mode.
    // Pin encoding is not the lane mask and must not be derived from it.
    switch (pin) {
        case 1:
            if (lanes == 4) plan.laneGroupBits[0] = plan.laneGroupBits[1] = 0x80;
            break;
        case 2:
            if (lanes == 2) plan.laneGroupBits[0] = plan.laneGroupBits[1] = 0x80;
            break;
        case 3: case 4: case 5: case 6:
            plan.laneGroupBits[0] = plan.laneGroupBits[1] = lanes == 1 ? 0x40 : 0x80;
            break;
        default: return false; // includes legacy0 and reserved7..15
    }
    out = plan;
    return true;
}

// Applying both lane-group RMWs must keep the native link disabled, retain the
// mode/epoch/power lease and stop training if either access is unverified.
// Two individually locked operations are NOT an atomic PHY mode transition.
} // namespace YogaTypec
