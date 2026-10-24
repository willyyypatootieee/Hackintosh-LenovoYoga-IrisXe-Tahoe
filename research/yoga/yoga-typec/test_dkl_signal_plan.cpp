#include "AdlpDklSignalPlan.hpp"
#include <assert.h>
#include <iostream>
using namespace YogaTypec;

int main() {
    unsigned valid = 0;
    for (unsigned vs = 0; vs < 256; ++vs)
    for (unsigned pe = 0; pe < 256; ++pe) {
        unsigned index = 99;
        bool ok = dklDriveIndex({uint8_t(vs), uint8_t(pe)}, index);
        assert(ok == (vs <= 3 && pe <= 3 && vs + pe <= 3));
        if (ok) { assert(index == valid); ++valid; }
        else assert(index == 99);
    }
    assert(valid == 10);
    for (unsigned tc = 0; tc < 4; ++tc)
    for (unsigned lanes : {1u, 2u, 4u})
    for (uint32_t rate : {162000u, 270000u, 540000u, 810000u})
    for (unsigned vs = 0; vs <= 3; ++vs)
    for (unsigned pe = 0; pe + vs <= 3; ++pe) {
        DpDriveLevel levels[4] = {{uint8_t(vs), uint8_t(pe)}, {1, 2}, {2, 1}, {3, 0}};
        DklSignalPlan plan{};
        assert(planDklDpSignal(Route{uint8_t(tc)}, Mode::DpAlt, lanes, rate, levels, plan));
        for (unsigned group = 0; group < 2; ++group) {
            const auto sus = plan.writes[group * 5];
            assert(sus.offset == group * 0x1000 + 0xd00);
            assert(sus.fullWrite && sus.mask == UINT32_MAX && sus.bits == 0);
            for (unsigned tx = 0; tx < 2; ++tx) {
                unsigned lane = 2 * group + tx, idx = 0;
                if (lane < lanes) assert(dklDriveIndex(levels[lane], idx));
                auto c = (rate > 270000 ? adlpDpHbr2Hbr3 : adlpDpHbr)[idx];
                auto w = plan.writes[group * 5 + 1 + tx];
                assert(w.offset == group * 0x1000 + 0x2c0 + 4 * tx && !w.fullWrite);
                assert(w.mask == 0x3ff07 && w.bits == unsigned(c.vswing | (c.deEmphasis << 8)));
                assert(!(w.bits & ~w.mask));
                for (uint32_t old : {0u, 0xdeadbeefu, 0x12345678u}) {
                    uint32_t after = (old & ~w.mask) | w.bits;
                    assert((after & ~w.mask) == (old & ~w.mask));
                }
                DklPlan access{};
                assert(planDklAccess(Route{uint8_t(tc)}, w.offset, access));
                assert(access.selectorValue == (group << (8 * tc)));
                assert(access.apertureRegister == 0x168000 + tc * 0x1000 + 0x2c0 + 4 * tx);
            }
            auto ctl2 = plan.writes[group * 5 + 3];
            assert(ctl2.offset == group * 0x1000 + 0x2c8 && !ctl2.fullWrite);
            assert(ctl2.mask == 0x04 && ctl2.bits == 0);
            auto loadgen = plan.writes[group * 5 + 4];
            assert(loadgen.offset == ctl2.offset && !loadgen.fullWrite);
            assert(loadgen.mask == 0x78 && loadgen.bits == 0); // DP, never HDMI loadgen
        }
    }
    DpDriveLevel levels[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    for (int fail = 0; fail < 8; ++fail) {
        DklSignalPlan plan{}; plan.writes[0].offset = 0xdeadbeef;
        Route route{uint8_t(fail == 0 ? 4 : 2)};
        Mode mode = fail == 1 ? Mode::TbtAlt : fail == 2 ? Mode::Disconnected : Mode::DpAlt;
        unsigned lanes = fail == 3 ? 3 : 4;
        uint32_t rate = fail == 4 ? 0 : fail == 5 ? 1080000 : 270000;
        levels[3] = fail == 6 ? DpDriveLevel{3, 1} : fail == 7 ? DpDriveLevel{255, 0} : DpDriveLevel{0, 0};
        assert(!planDklDpSignal(route, mode, lanes, rate, levels, plan));
        assert(plan.writes[0].offset == 0xdeadbeef);
    }
    // Bad active lane refuses; inactive values aren't consumed.
    DklSignalPlan plan{};
    assert(planDklDpSignal(Route{2}, Mode::DpAlt, 1, 270000, levels, plan));
    std::cout << "PASS: ADL-P DP drive tables, independent lanes, rate selection, bounded masks, DP-only loadgen, invalid-input refusal\n";
}
