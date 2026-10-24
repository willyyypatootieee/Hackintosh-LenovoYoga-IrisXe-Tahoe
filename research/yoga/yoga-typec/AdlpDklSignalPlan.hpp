// SPDX-License-Identifier: MIT
// ADL-P coefficient tables derived from Linux v6.12 intel_ddi_buf_trans.c.
// Copyright (c) 2020 Intel Corporation
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#pragma once
#include "AdlpDklAccess.hpp"

namespace YogaTypec {
struct DklCoefficients { uint8_t vswing, preshoot, deEmphasis; };
// Table order: VS0 PE0..3, VS1 PE0..2, VS2 PE0..1, VS3 PE0.
static constexpr DklCoefficients adlpDpHbr[10] = {
    {7, 0, 0x01}, {5, 0, 0x06}, {2, 0, 0x0b}, {0, 0, 0x17},
    {5, 0, 0x00}, {2, 0, 0x08}, {0, 0, 0x14},
    {2, 0, 0x00}, {0, 0, 0x0b}, {0, 0, 0x00}
};
static constexpr DklCoefficients adlpDpHbr2Hbr3[10] = {
    {7, 0, 0x00}, {5, 0, 0x04}, {2, 0, 0x0a}, {0, 0, 0x18},
    {5, 0, 0x00}, {2, 0, 0x06}, {0, 0, 0x14},
    {2, 0, 0x00}, {0, 0, 0x09}, {0, 0, 0x00}
};
struct DpDriveLevel { uint8_t voltageSwing, preEmphasis; };
struct DklSignalWrite {
    uint32_t offset, mask, bits;
    bool fullWrite; // PMD_LANE_SUS uses an explicit full zero write in Linux
};
struct DklSignalPlan { DklSignalWrite writes[10]; };

inline bool dklDriveIndex(DpDriveLevel level, unsigned &out) {
    if (level.voltageSwing > 3 || level.preEmphasis > 3 ||
        level.voltageSwing + level.preEmphasis > 3) return false;
    const unsigned starts[] = {0, 4, 7, 9};
    out = starts[level.voltageSwing] + level.preEmphasis;
    return true;
}

// Pure DP-alt source-PHY plan: HDMI at the far side of an active USB-C adapter
// is still DP on this link. Do NOT select native-HDMI load generation or tables.
// Rate is DP link clock in kHz, NOT pixel clock or aggregate bit rate.
// levels[] is logical source-lane order as in Linux (2*group + tx). Orientation
// and native Apple lane numbering must be reconciled by the future backend.
// Inactive lanes are deterministically set to level0; no uninitialized input.
inline bool planDklDpSignal(Route route, Mode mode, unsigned lanes,
                            uint32_t rateKHz, const DpDriveLevel (&levels)[4],
                            DklSignalPlan &out) {
    if (!route.valid() || mode != Mode::DpAlt ||
        (lanes != 1 && lanes != 2 && lanes != 4) ||
        (rateKHz != 162000 && rateKHz != 270000 &&
         rateKHz != 540000 && rateKHz != 810000)) return false;
    const DklCoefficients *table = rateKHz > 270000 ? adlpDpHbr2Hbr3 : adlpDpHbr;
    unsigned indices[4] = {};
    for (unsigned lane = 0; lane < lanes; ++lane)
        if (!dklDriveIndex(levels[lane], indices[lane])) return false;
    DklSignalPlan plan{};
    for (unsigned group = 0; group < 2; ++group) {
        const uint32_t base = group * 0x1000;
        plan.writes[group * 5] = {base + 0xd00, UINT32_MAX, 0, true};
        for (unsigned tx = 0; tx < 2; ++tx) {
            const auto c = table[indices[group * 2 + tx]];
            plan.writes[group * 5 + 1 + tx] = {base + 0x2c0 + tx * 4,
                (0x1fu << 13) | (0x1fu << 8) | 7u,
                (uint32_t(c.preshoot) << 13) | (uint32_t(c.deEmphasis) << 8) | c.vswing,
                false};
        }
        // Retain Linux's two separate RMWs and ordering rather than assume
        // that combining them is valid. DP uses zero for both load generators.
        plan.writes[group * 5 + 3] = {base + 0x2c8, 0x04, 0, false};
        plan.writes[group * 5 + 4] = {base + 0x2c8, 0x78, 0, false};
    }
    out = plan;
    return true;
}
// Planning does not authorize MMIO: DklAccess deliberately still refuses these
// signal writes. Applying requires a mode/epoch-bound lease, verified clock,
// native training admission, full-write semantics and failure propagation.
} // namespace YogaTypec
