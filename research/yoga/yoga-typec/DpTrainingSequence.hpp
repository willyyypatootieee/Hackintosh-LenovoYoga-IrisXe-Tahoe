// Bounded DP 1.x 8b/10b DPRX training protocol; NO native AUX/MMIO backend.
// Algorithm reference: pinned Linux v6.12 intel_dp_link_training.c and
// drm_dp_helper.c. This is intentionally narrower: no UHBR, nontransparent
// LTTPR, eDP rate-select, extended receiver caps, or guessed source settings.
#pragma once
#include "AdlpDklSignalPlan.hpp"
#include "DpSinkStatus.hpp"

namespace YogaTypec {
enum class DpTrainStage : uint8_t {
    Idle, SourceCr, SinkCr, WaitCr, RequestDrive, VerifyDrive, WriteDrive,
    SourceEq, SinkEq, WaitEq, DisableSink, DisableSource, VerifySinkDisabled, FinalStatus,
    Ready, Failed, Quarantined
};
enum class DpTrainResult : uint8_t {
    Progress, NeedDrive, Ready, InvalidInput, Failed, TimedOut, Quarantined
};

inline bool dpCompleteStatus(const uint8_t *s, size_t count) {
    if (!s || count != 6) return false;
    for (unsigned i = 0; i < 6; ++i) if (s[i] != 0xff) return true;
    return false;
}
inline bool dpClockRecoveryVerified(unsigned lanes, const uint8_t *s, size_t count) {
    if ((lanes != 1 && lanes != 2 && lanes != 4) || !dpCompleteStatus(s, count)) return false;
    for (unsigned lane = 0; lane < lanes; ++lane)
        if (!(s[lane / 2] & (1u << (4 * (lane % 2))))) return false;
    return true;
}

// ADL-P's ten-entry DP tables support levels0..3 and v+p<=3. Clamp sink
// requests like Linux, with DP max flags, rather than index impossible pairs.
inline uint8_t dpAdlpTrainingByte(unsigned request, DpDriveLevel &out) {
    const auto p = uint8_t((request >> 2) & 3);
    const auto requestedV = uint8_t(request & 3);
    const auto v = uint8_t(requestedV < 3 - p ? requestedV : 3 - p);
    out = {v, p};
    return uint8_t(v | (v == 3 ? 4 : 0) | (p << 3) | (p == 3 ? 0x20 : 0));
}

// Backend MUST retain powered same-route/epoch identity for the entire protocol.
// Every callback atomically revalidates identity, admission and negotiated link
// parameters under the relevant native locks. No private-lock substitute.
// linkParametersReady proves transparent DPRX, source+sink rate/lane/coding,
// enhanced framing and PHY/source-ready agreement, not cached desired settings.
// read/writeDpcd use real AUX and actual counts; no emulated completion or cache.
// setSourcePattern consumes DPCD pattern encoding (0,0x21,0x22,0x23,7), translates
// to reviewed source fields, and verifies readback. It MUST NOT write these raw
// bytes into source registers. ADL-P source TPS3/TPS4 support is backend-proven.
// sourceDriveMatches validates complete applied DKL coefficients/readback, NOT
// just requested levels or a software flag. Caller applies NeedDrive via the
// existing signal-plan executor, then re-enters step; no sleep under locks.
// Each callback is bounded and accounts for the outer1s monotonic deadline.
// On ANY failure after touched(), outer retained journal/drain cleanup remains
// mandatory, including uncertain source/sink writes and failed pattern disable.
template<class Backend> class DpTrainingSequence {
    Backend &io;
    Route route;
    uint64_t epoch;
    DpTrainStage stage = DpTrainStage::Idle;
    DpTrainStage afterDrive = DpTrainStage::WaitCr;
    DpTrainResult failure = DpTrainResult::Failed;
    unsigned lanes = 0, crReads = 0, eqReads = 0, sameRequests = 1, maxCr = 0;
    uint32_t rate = 0, crDelay = 0, eqDelay = 0;
    uint64_t since = 0, lastNow = 0, waitSince = 0;
    uint8_t eqPattern = 0, training[4] = {}, oldRequests[4] = {};
    DpDriveLevel levels[4] = {};
    bool dirty = false, maxSwing = false;

    DpTrainResult fail(DpTrainResult why = DpTrainResult::Failed) {
        if (!validIdentity()) return quarantine();
        if (stage == DpTrainStage::Failed) return failure;
        stage = DpTrainStage::Failed; return failure = why;
    }
    DpTrainResult quarantine() { stage = DpTrainStage::Quarantined; return DpTrainResult::Quarantined; }
    bool validIdentity() { return io.authorized(route, epoch); }
    bool forward() {
        if (!validIdentity()) { quarantine(); return false; }
        if (io.cancelled(route, epoch)) { fail(); return false; }
        const auto now = io.nowMicros();
        if (now < lastNow || now < since ||
            (stage != DpTrainStage::Ready && now - since >= 1000000)) {
            fail(DpTrainResult::TimedOut); return false;
        }
        lastNow = now;
        return true;
    }
    bool write(uint32_t address, const uint8_t *bytes, size_t count) {
        if (!forward()) return false;
        size_t actual = 0; dirty = true;
        const auto ok = io.writeDpcd(route, epoch, address, bytes, count, actual);
        return forward() && ok && actual == count;
    }
    bool readStatus(uint8_t (&status)[6]) {
        if (!forward()) return false;
        size_t actual = 0;
        const auto ok = io.readDpcd(route, epoch, 0x202, status, 6, actual);
        return forward() && ok && dpCompleteStatus(status, actual);
    }
    bool source(uint8_t pattern) {
        if (!forward()) return false;
        dirty = true;
        const auto ok = io.setSourcePattern(route, epoch, pattern);
        return forward() && ok;
    }
    void wait(DpTrainStage next) { waitSince = io.nowMicros(); stage = next; }
    void requestDrive(const uint8_t (&s)[6], bool cr) {
        bool changed = false; maxSwing = true;
        for (unsigned lane = 0; lane < lanes; ++lane) {
            const auto request = uint8_t((s[4 + lane / 2] >> (4 * (lane % 2))) & 15);
            changed = changed || request != oldRequests[lane];
            oldRequests[lane] = request;
            training[lane] = dpAdlpTrainingByte(request, levels[lane]);
            maxSwing = maxSwing && (training[lane] & 4) &&
                levels[lane].voltageSwing + levels[lane].preEmphasis == 3;
        }
        if (cr) sameRequests = changed ? 1 : sameRequests + 1;
        afterDrive = cr ? DpTrainStage::WaitCr : DpTrainStage::WaitEq;
        stage = DpTrainStage::RequestDrive;
    }

public:
    DpTrainingSequence(Backend &backend, Route r, uint64_t e) : io(backend), route(r), epoch(e) {}
    DpTrainingSequence(const DpTrainingSequence &) = delete;
    DpTrainingSequence &operator=(const DpTrainingSequence &) = delete;
    DpTrainStage currentStage() const { return stage; }
    bool touched() const { return dirty; }

    DpTrainResult start(unsigned width, uint32_t rateKHz) {
        if (stage != DpTrainStage::Idle || !route.valid() || route.tcIndex != 2 || !epoch ||
            (width != 1 && width != 2 && width != 4) ||
            (rateKHz != 162000 && rateKHz != 270000 && rateKHz != 540000 && rateKHz != 810000))
            return DpTrainResult::InvalidInput;
        if (!validIdentity()) return quarantine();
        since = lastNow = io.nowMicros();
        if (!forward()) return fail();
        if (!io.linkParametersReady(route, epoch, width, rateKHz)) return fail();
        if (!forward()) return fail();
        uint8_t caps[16] = {}; size_t actual = 0;
        if (!io.readDpcd(route, epoch, 0, caps, sizeof(caps), actual) || actual != sizeof(caps)) return fail();
        if (!forward()) return fail();
        const unsigned maxLanes = caps[2] & 31;
        const uint8_t rateCode = uint8_t(rateKHz / 27000);
        if (caps[0] < 0x10 || caps[0] > 0x14 ||
            (caps[1] != 6 && caps[1] != 10 && caps[1] != 20 && caps[1] != 30) ||
            rateCode > caps[1] || (maxLanes != 1 && maxLanes != 2 && maxLanes != 4) ||
            width > maxLanes || (caps[14] & 0x80) || (caps[14] & 0x7f) > 4)
            return fail();
        lanes = width; rate = rateKHz;
        const auto interval = caps[14] & 0x7f;
        crDelay = caps[0] >= 0x14 || interval == 0 ? 100 : interval * 4000;
        eqDelay = interval == 0 ? 400 : interval * 4000;
        maxCr = caps[0] >= 0x14 ? 10 : 80;
        // Do not guess source support from spoofed PCI/display generation.
        const bool tps4 = caps[0] >= 0x14 && (caps[3] & 0x80) && io.sourceSupportsPattern(route, epoch, 7);
        if (!forward()) return fail();
        const bool tps3 = !tps4 && caps[0] >= 0x12 && (caps[2] & 0x40) && io.sourceSupportsPattern(route, epoch, 3);
        if (!forward()) return fail();
        eqPattern = tps4 ? 7 : tps3 ? 0x23 : 0x22;
        if (rate == 810000 && eqPattern != 7) return fail();
        if (!io.sourceDriveMatches(route, epoch, levels)) return fail();
        if (!forward()) return fail();
        stage = DpTrainStage::SourceCr;
        return DpTrainResult::Progress;
    }

    DpTrainResult step(DpDriveLevel (&requested)[4]) {
        if (stage == DpTrainStage::Idle) return DpTrainResult::InvalidInput;
        if (stage == DpTrainStage::Quarantined) return DpTrainResult::Quarantined;
        if (stage == DpTrainStage::Failed) return failure;
        if (!forward()) return fail();
        const auto now = io.nowMicros();
        if (!io.linkParametersReady(route, epoch, lanes, rate)) return fail();
        if (!forward()) return fail();
        if (stage == DpTrainStage::SourceCr) {
            if (!source(0x21)) return fail();
            stage = DpTrainStage::SinkCr;
        } else if (stage == DpTrainStage::SinkCr || stage == DpTrainStage::SinkEq) {
            const bool cr = stage == DpTrainStage::SinkCr;
            uint8_t payload[5] = {uint8_t(cr ? 0x21 : eqPattern)};
            for (unsigned lane = 0; lane < lanes; ++lane) payload[lane + 1] = training[lane];
            if (!write(0x102, payload, lanes + 1)) return fail();
            wait(cr ? DpTrainStage::WaitCr : DpTrainStage::WaitEq);
        } else if (stage == DpTrainStage::WaitCr || stage == DpTrainStage::WaitEq) {
            const bool cr = stage == DpTrainStage::WaitCr;
            if (now < waitSince) return fail(DpTrainResult::TimedOut);
            if (now - waitSince < (cr ? crDelay : eqDelay)) return DpTrainResult::Progress;
            uint8_t status[6] = {};
            if (!readStatus(status)) return fail();
            if (cr) {
                ++crReads;
                if (dpClockRecoveryVerified(lanes, status, 6)) stage = DpTrainStage::SourceEq;
                else {
                    if (sameRequests == 5 || maxSwing || crReads >= maxCr) return fail();
                    requestDrive(status, true);
                }
            } else {
                ++eqReads;
                if (!dpClockRecoveryVerified(lanes, status, 6)) return fail();
                if (dpSinkLinkVerified(lanes, status, 6)) stage = DpTrainStage::DisableSink;
                else {
                    if (eqReads >= 5) return fail();
                    requestDrive(status, false);
                }
            }
        } else if (stage == DpTrainStage::RequestDrive) {
            for (unsigned lane = 0; lane < 4; ++lane) requested[lane] = levels[lane];
            stage = DpTrainStage::VerifyDrive; return DpTrainResult::NeedDrive;
        } else if (stage == DpTrainStage::VerifyDrive) {
            if (!io.sourceDriveMatches(route, epoch, levels)) return fail();
            if (!forward()) return fail();
            stage = DpTrainStage::WriteDrive;
        } else if (stage == DpTrainStage::WriteDrive) {
            if (!write(0x103, training, lanes)) return fail();
            wait(afterDrive);
        } else if (stage == DpTrainStage::SourceEq) {
            if (!source(eqPattern)) return fail();
            stage = DpTrainStage::SinkEq;
        } else if (stage == DpTrainStage::DisableSink) {
            const uint8_t off = 0;
            if (!write(0x102, &off, 1)) return fail();
            stage = DpTrainStage::DisableSource;
        } else if (stage == DpTrainStage::DisableSource) {
            if (!source(0)) return fail();
            stage = DpTrainStage::VerifySinkDisabled;
        } else if (stage == DpTrainStage::VerifySinkDisabled) {
            uint8_t pattern = 0xff; size_t actual = 0;
            const auto ok = io.readDpcd(route, epoch, 0x102, &pattern, 1, actual);
            if (!forward() || !ok || actual != 1 || pattern != 0) return fail();
            stage = DpTrainStage::FinalStatus;
        } else if (stage == DpTrainStage::FinalStatus || stage == DpTrainStage::Ready) {
            uint8_t status[6] = {};
            if (!readStatus(status) || !dpSinkLinkVerified(lanes, status, 6)) return fail();
            if (!io.sourceTrainingDisabled(route, epoch)) return fail();
            if (!forward()) return fail();
            stage = DpTrainStage::Ready;
            if (!validIdentity()) return quarantine();
            return DpTrainResult::Ready;
        }
        if (!forward()) return fail();
        return DpTrainResult::Progress;
    }
};
} // namespace YogaTypec
