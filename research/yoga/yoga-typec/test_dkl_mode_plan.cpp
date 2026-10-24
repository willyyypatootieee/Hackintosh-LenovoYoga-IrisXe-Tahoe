#include "AdlpDklModePlan.hpp"
#include <assert.h>
#include <iostream>
using namespace YogaTypec;

int main() {
    for (unsigned tc = 0; tc < 4; ++tc) {
        const Route route{static_cast<uint8_t>(tc)};
        for (unsigned pin = 0; pin < 16; ++pin)
        for (unsigned laneMask = 0; laneMask < 16; ++laneMask)
        for (unsigned lanes : {1u, 2u, 4u}) {
            const uint32_t pins = (0xa5a50000u | (pin << (4 * route.fiaLocalIndex())));
            const uint32_t status = 0x12340000u | (laneMask << route.laneShift());
            DklModePlan plan{7, {8, 9}, 10};
            const bool valid = pin >= 1 && pin <= 6 && laneCount(status, route) >= lanes;
            assert(planDklDpMode(route, Mode::DpAlt, lanes, status, pins, plan) == valid);
            if (!valid) {
                assert(plan.pinAssignmentRegister == 7 && plan.laneGroupBits[0] == 8 &&
                       plan.laneGroupBits[1] == 9 && plan.pinAssignment == 10);
                continue;
            }
            const unsigned want = pin <= 2 ? ((pin == 1 && lanes == 4) ||
                (pin == 2 && lanes == 2) ? 0x80 : 0) : lanes == 1 ? 0x40 : 0x80;
            assert(plan.pinAssignment == pin && plan.laneGroupBits[0] == want &&
                   plan.laneGroupBits[1] == want);
            assert(plan.pinAssignmentRegister == (tc < 2 ? 0x163880u : 0x16e880u));
        }
    }
    const Route tc3{2};
    // Same four-lane mask with different pin assignments produces different
    // programming. Feeding DFLEXDPSP nibble15 as the pin fails, not pin15 writes.
    DklModePlan a{}, b{};
    assert(planDklDpMode(tc3, Mode::DpAlt, 2, 15, 1, a));
    assert(planDklDpMode(tc3, Mode::DpAlt, 2, 15, 3, b));
    assert(a.laneGroupBits[0] == 0 && b.laneGroupBits[0] == 0x80);
    assert(!planDklDpMode(tc3, Mode::DpAlt, 2, 15, 15, a));
    for (Mode mode : {Mode::Disconnected, Mode::TbtAlt, Mode::Ambiguous})
        assert(!planDklDpMode(tc3, mode, 2, 15, 3, a));
    assert(!planDklDpMode(tc3, Mode::DpAlt, 2, UINT32_MAX, 3, a));
    assert(!planDklDpMode(tc3, Mode::DpAlt, 2, 15, UINT32_MAX, a));
    assert(!planDklDpMode(Route{4}, Mode::DpAlt, 2, 15, 3, a));
    for (unsigned lanes : {0u, 3u, 8u})
        assert(!planDklDpMode(tc3, Mode::DpAlt, lanes, 15, 3, a));
    assert(!dklPinAssignmentRegister(Route{4}));
    std::cout << "PASS: DKL DP mode plan, distinct pin/lane inputs, modular FIA, exhaustive nonlegacy pin and lane cases\n";
}
