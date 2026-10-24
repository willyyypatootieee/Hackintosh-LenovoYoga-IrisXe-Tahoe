// SPDX-License-Identifier: MIT
// DP-only integer implementation of the Linux v6.12 DKL PLL calculation.
// Algorithm/register definitions: Intel, display/intel_dpll_mgr.c and
// intel_{dkl,mg}_phy_regs.h (MIT). No hardware operations in this header.
#pragma once
#include "AdlpDklAccess.hpp"

namespace YogaTypec {
struct DklPllWrite { uint32_t offset, mask, bits; };
struct DklPllPlan {
    uint32_t directEnableRegister;
    uint32_t rateKHz, referenceKHz, dcoKHz;
    uint8_t div1, div2;
    DklPllWrite writes[8];
};

inline bool planDklDpPll(Route route, Mode mode, uint32_t rateKHz,
                         uint32_t referenceKHz, bool overrideAfc,
                         unsigned afcStartup, DklPllPlan &out) {
    if (!route.valid() || mode != Mode::DpAlt ||
        (rateKHz != 162000 && rateKHz != 270000 && rateKHz != 540000 && rateKHz != 810000) ||
        (referenceKHz != 19200 && referenceKHz != 24000 && referenceKHz != 38400) ||
        afcStartup > 7 || (!overrideAfc && afcStartup != 0)) return false;
    DklPllPlan plan{};
    plan.directEnableRegister = 0x46038u + 8 * route.tcIndex;
    plan.rateKHz = rateKHz;
    plan.referenceKHz = referenceKHz;
    plan.dcoKHz = 8100000;
    const unsigned firstDivisors[] = {7, 5, 3, 2};
    for (unsigned d1 : firstDivisors) {
        for (unsigned d2 = 10; d2 > 0; --d2) {
            if (uint64_t(d1) * d2 * rateKHz * 5 == plan.dcoKHz) {
                plan.div1 = static_cast<uint8_t>(d1);
                plan.div2 = static_cast<uint8_t>(d2);
                break;
            }
        }
        if (plan.div1) break;
    }
    if (!plan.div1) return false;
    const uint32_t m1 = 2;
    const uint32_t m2 = plan.dcoKHz / (referenceKHz * m1);
    const uint32_t rem = plan.dcoKHz % (referenceKHz * m1);
    const uint32_t frac = uint64_t(rem) * (1u << 22) / (referenceKHz * m1);
    const uint32_t tdc = (uint64_t(2) * 1000 * 100000 * 10 / (132 * referenceKHz) + 5) / 10;
    const uint32_t gain = rem ? uint64_t(m1) * 1000000 * 100 / (plan.dcoKHz * 3 / 10) : 0;
    if (m2 > 255 || tdc > 255 || gain > 255 || frac > 0x3fffff) return false;
    const uint32_t trim = referenceKHz == 24000 ? 25 : 28;
    const uint32_t ndiv = referenceKHz == 38400 ? 2 : 1;
    const uint32_t hsdiv = plan.div1 == 7 ? 3 : plan.div1 == 5 ? 2 : plan.div1 == 3 ? 1 : 0;
    const uint32_t tlinedrv = plan.div2 >= 2 ? 1 : 0;
    const uint32_t aRatio = plan.div2 >= 2 ? 10 : 5;
    // Same order/masks as dkl_pll_write. SSC disabled, as in the pinned DP path.
    plan.writes[0] = {0x212c, 0x700, 0x100};
    plan.writes[1] = {0x20d8, 0xff00, aRatio << 8};
    plan.writes[2] = {0x20d4, 0x1ff00, (tlinedrv << 14) | (hsdiv << 12) | (uint32_t(plan.div2) << 8)};
    plan.writes[3] = {0x2200, 0x1fffffu | (overrideAfc ? 0x0e000000u : 0),
        (8u << 16) | (4u << 12) | (m1 << 8) | m2 | (overrideAfc ? afcStartup << 25 : 0)};
    plan.writes[4] = {0x2204, 0x1f00ff, (trim << 16) | tdc};
    plan.writes[5] = {0x2210, 0xe0ff3a00, (ndiv << 29) | (4u << 11)};
    plan.writes[6] = {0x2214, 0x7fffff00, (frac ? 1u << 30 : 0) | (frac << 8)};
    plan.writes[7] = {0x2218, 0xffff, gain};
    for (const auto &write : plan.writes)
        if (write.bits & ~write.mask) return false;
    out = plan;
    return true;
}
} // namespace YogaTypec
