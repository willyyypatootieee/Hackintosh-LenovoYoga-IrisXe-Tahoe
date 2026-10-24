#include "AdlpFiaLanePlan.hpp"
#include <assert.h>
#include <iostream>
using namespace YogaTypec;

static bool same(const LanePlan &a, const LanePlan &b) {
    return a.address == b.address && a.mask == b.mask &&
           a.bits == b.bits && a.value == b.value;
}

int main() {
    const LanePlan sentinel{0x12345678, 0x98765432, 0x55aa55aa, 0xaa55aa55};
    for (unsigned tc = 0; tc < 4; ++tc) {
        const Route route{static_cast<uint8_t>(tc)};
        for (unsigned assignment = 0; assignment < 16; ++assignment) {
            const unsigned available = assignment == 15 ? 4 :
                assignment == 3 || assignment == 12 ? 2 :
                assignment == 1 || assignment == 2 || assignment == 4 || assignment == 8 ? 1 : 0;
            for (unsigned lanes : {0u, 1u, 2u, 3u, 4u, 8u}) {
                for (uint32_t old : {0u, 0x12345678u, 0xa5a5a5a5u, 0xfffffffeu}) {
                    // Deliberately nonzero sibling and reserved status fields.
                    const uint32_t dpsp = (0xa5a55a5au & ~(15u << route.laneShift())) |
                        (assignment << route.laneShift());
                    LanePlan p = sentinel;
                    const auto result = planDpAltLanes(route, Mode::DpAlt, lanes, dpsp, old, p);
                    const auto expected = lanes != 1 && lanes != 2 && lanes != 4 ? LanePlanResult::InvalidLanes :
                        !available ? LanePlanResult::InvalidAssignment :
                        available < lanes ? LanePlanResult::InsufficientLanes : LanePlanResult::Ok;
                    assert(result == expected);
                    if (result != LanePlanResult::Ok) { assert(same(p, sentinel)); continue; }
                    const unsigned shift = (tc % 2) * 4;
                    assert(p.address == (tc < 2 ? 0x1638c0u : 0x16e8c0u));
                    assert(p.mask == (15u << shift));
                    assert(p.bits == ((lanes == 4 ? 15u : lanes == 2 ? 3u : 1u) << shift));
                    assert((p.value & p.mask) == p.bits);
                    assert((p.value & ~p.mask) == (old & ~p.mask));
                    for (unsigned bit = 0; bit < 32; ++bit) {
                        const uint32_t toggled = old ^ (1u << bit);
                        LanePlan q = sentinel;
                        if (toggled == UINT32_MAX) continue;
                        assert(planDpAltLanes(route, Mode::DpAlt, lanes, dpsp, toggled, q) == LanePlanResult::Ok);
                        assert((q.value ^ p.value) == ((1u << bit) & ~p.mask));
                    }
                }
            }
        }
    }
    for (Mode mode : {Mode::Disconnected, Mode::TbtAlt, Mode::Ambiguous}) {
        LanePlan p = sentinel;
        assert(planDpAltLanes(Route{2}, mode, 4, 15, 0, p) == LanePlanResult::WrongMode);
        assert(same(p, sentinel));
    }
    for (unsigned tc = 4; tc < 256; ++tc) {
        LanePlan p = sentinel;
        assert(planDpAltLanes(Route{static_cast<uint8_t>(tc)}, Mode::DpAlt, 4, 15, 0, p) == LanePlanResult::InvalidRoute);
        assert(same(p, sentinel));
    }
    for (bool badStatus : {false, true}) {
        LanePlan p = sentinel;
        assert(planDpAltLanes(Route{2}, Mode::DpAlt, 4,
            badStatus ? UINT32_MAX : 15, badStatus ? 0 : UINT32_MAX, p) == LanePlanResult::InvalidRead);
        assert(same(p, sentinel));
    }
    // TC3 uses FIA2 LOW nibble, not the global register's bits 11:8.
    LanePlan p = sentinel;
    assert(planDpAltLanes(Route{2}, Mode::DpAlt, 2, 12, 0xaabbccddu, p) == LanePlanResult::Ok);
    assert(p.address == 0x16e8c0 && p.mask == 15 && p.bits == 3 && p.value == 0xaabbccd3);
    std::cout << "PASS: all ADL-P routes, assignments, lane requests, preserved bits, DP-only gating, fail-closed output\n";
}
