// Composition of the real local acquisition/PLL/mode/signal components.
// Host-executable integration logic. No Apple ABI casts or live hooks here.
#pragma once
#include "ResumableTypecTransaction.hpp"
#include "DklPllSequence.hpp"
#include "DdiClockSequence.hpp"
#include "DpSinkStatus.hpp"
#include "NativeClockInputs.hpp"
#include "AdlpFiaLanePlan.hpp"
#include "AdlpDklModePlan.hpp"
#include "AdlpDklSignalPlan.hpp"

namespace YogaTypec {
enum class DpStage : uint8_t {
    Idle, CloseAdmission, Acquire, Lanes, ClockStart, ClockWait, MapClock, Mode,
    Signal, TrainingStart, Training, Publish, Active,
    Drain, RestorePhy, UnmapClock, StopClock, Release, Quarantined
};
enum class DpResult : uint8_t {
    Progress, Active, Complete, Busy, InvalidInput, AcquisitionFailed,
    Changed, ProgrammingFailed, ClockFailed, TrainingFailed,
    PublicationFailed, CleanupPending, Quarantined, TrainingTimedOut
};
enum class TrainingResult : uint8_t { Pending, NeedDrive, SinkVerified, Failed };

// Additional Backend contract (alongside acquisition/PLL/DDI clock components):
// - this is ONE DP-alt epoch, never TBT/legacy. Native mode publication for AUX
//   accounting is backend-private and must not grant link/training admission.
// - authorized and powered snapshots are rechecked under serialization; no
//   mutation may be directed to a reallocated port or a different mode/epoch.
// - cancelled distinguishes HPD loss/sleep on a still-retained identity from
//   lost authorization. The former drains; the latter quarantines without MMIO.
// - every mutating callback conservatively carries a cleanup obligation BEFORE
//   entry. Backend journals exact pre-state and consumes cleanup tokens once.
// - programLanes uses the shared FIA lock and recomputes RMW from fresh read,
//   not the stale LanePlan.value. It verifies the requested mask readback.
// - updateMode/updateSignal execute the narrowly typed reviewed operations,
//   globally serialized against ALL native DKL users. Signal full-zero writes
//   need explicit semantics, not arbitrary full-mask RMW permission.
// - DdiClockSequence covers DDI_CLK_SEL plus shared DPCLKA RMW; its native
//   shared-lock and powered identity requirements are not optional.
// - trainingStart/trainingStep are bounded asynchronous native training adapters.
//   NeedDrive supplies FOUR defined logical source-lane levels. SinkVerified
//   means actual DPCD clock-recovery/channel-EQ/symbol-lock/interlane-alignment
//   checks succeeded, not a return value or emulated completion.
// - readSinkStatus performs an actual six-byte DPCD0x202 AUX read on this
//   retained epoch (no stale/cached data), with actual transferred byte count.
//   This composition independently verifies it before publication; backend
//   must also finish source/sink training-pattern disable, verify DDI buffer,
//   and preserve link parameters through the final atomic publishReady check.
// - closeAdmissionAndDrain stops new AUX/link/training users and disables the
//   DDI buffer before restorePhy/clock unmapping. Retry leaves ALL power held.
//   It also runs BEFORE acquisition, and must be safe with no acquisition or
//   journaled hardware state: no unpowered MMIO or release of unowned resources.
//   Admission stays closed across acquisition failure and its internal unwind.
// - restorePhy reconciles only journaled changes under the SAME powered epoch.
//   DDI teardown gates first, then removes only our mapping. Both are retryable.
// - publishReady atomically admits native use ONLY after training; cached
//   acquisition Ready alone must NEVER allow link programming.
// - no sleeps or busy loops here; caller schedules step outside shared locks.
//   Training has a 1-second conservative transaction budget including drive
//   updates, checked using monotonic nowMicros; backend additionally bounds each
//   AUX/native callback and cancels on HPD/sleep/detach.
template<class Backend> class DpLinkSequence {
    Backend &io;
    Route route;
    uint64_t epoch;
    ResumableTransaction<Backend> lease;
    DklPllSequence<Backend> pll;
    DdiClockSequence<Backend> mapping;
    DpStage stage = DpStage::Idle;
    DpResult cause = DpResult::Complete;
    unsigned lanes = 0, modeWrite = 0, signalWrite = 0;
    uint32_t rate = 0, reference = 0;
    bool afcOverride = false;
    unsigned afc = 0;
    bool nativeTouched = false, phyTouched = false, mapStarted = false;
    DklModePlan modePlan{};
    DklSignalPlan signalPlan{};
    DpDriveLevel levels[4] = {};
    bool trainingStarted = false;
    uint64_t trainingSince = 0;

    DpResult quarantine() { stage = DpStage::Quarantined; return cause = DpResult::Quarantined; }
    DpResult abort(DpResult why) { cause = why; stage = DpStage::Drain; return why; }
    bool authorized() { return io.authorized(route, epoch); }
    bool cleanup(Cleanup result, DpStage next) {
        if (result == Cleanup::Quarantined) { quarantine(); return false; }
        if (result == Cleanup::Retry) return false;
        stage = next; return true;
    }
    DpResult cleanupResult() const {
        return stage == DpStage::Quarantined ? DpResult::Quarantined : DpResult::CleanupPending;
    }
    bool planSignal() { return planDklDpSignal(route, Mode::DpAlt, lanes, rate, levels, signalPlan); }
    bool sinkVerified() {
        uint8_t status[6] = {}; size_t transferred = 0;
        return io.readSinkStatus(route, epoch, status, transferred) &&
            dpSinkLinkVerified(lanes, status, transferred);
    }

public:
    DpLinkSequence(Backend &backend, Route r, uint64_t leaseEpoch)
        : io(backend), route(r), epoch(leaseEpoch), lease(io, r), pll(io, r, leaseEpoch),
          mapping(io, r, leaseEpoch) {}
    DpLinkSequence(const DpLinkSequence &) = delete;
    DpLinkSequence &operator=(const DpLinkSequence &) = delete;
    DpStage currentStage() const { return stage; }
    DpResult failureCause() const { return cause; }
    bool active() const { return stage == DpStage::Active; }
    bool needsCleanup() const {
        return lease.needsCleanup() || pll.needsCleanup() || mapping.needsCleanup() || nativeTouched;
    }

    DpResult start(unsigned width, uint32_t rateKHz, uint32_t refKHz,
                   bool overrideAfc = false, unsigned afcStartup = 0) {
        if (stage == DpStage::Quarantined) return DpResult::Quarantined;
        if (stage != DpStage::Idle) return DpResult::Busy;
        DklPllPlan validated{};
        if ((width != 1 && width != 2 && width != 4) ||
            !planDklDpPll(route, Mode::DpAlt, rateKHz, refKHz, overrideAfc, afcStartup, validated))
            return DpResult::InvalidInput;
        lanes = width; rate = rateKHz; reference = refKHz;
        afcOverride = overrideAfc; afc = afcStartup;
        for (auto &level : levels) level = {0, 0};
        modeWrite = signalWrite = 0; trainingStarted = false;
        cause = DpResult::Complete; stage = DpStage::CloseAdmission;
        return DpResult::Progress;
    }

    DpResult stop() {
        if (stage == DpStage::Quarantined) return DpResult::Quarantined;
        if (stage == DpStage::Idle) return DpResult::Complete;
        if (stage < DpStage::Drain) stage = DpStage::Drain;
        return step();
    }

    DpResult step() {
        if (stage == DpStage::Quarantined) return DpResult::Quarantined;
        if (stage == DpStage::Idle) return DpResult::Complete;
        // No mutating callback on a lost/reused identity. Backend owns emergency
        // lifecycle reconciliation; this object must not touch a new epoch.
        if (!authorized()) return quarantine();
        if (stage < DpStage::Drain && io.cancelled(route, epoch)) return abort(DpResult::Changed);
        if (trainingStarted && stage >= DpStage::Signal && stage <= DpStage::Publish) {
            const auto now = io.nowMicros();
            if (now < trainingSince || now - trainingSince >= 1000000)
                return abort(DpResult::TrainingTimedOut);
        }
        if (stage >= DpStage::Mode && stage <= DpStage::Active) {
            const auto pllResult = pll.step();
            if (pllResult == PllResult::Busy) return DpResult::Progress;
            if (pllResult != PllResult::Ready) return abort(DpResult::ClockFailed);
            const auto result = mapping.step();
            if (result == ClockMapResult::Quarantined) return quarantine();
            if (result == ClockMapResult::Busy) return DpResult::Progress;
            if (result != ClockMapResult::Ready) return abort(DpResult::ClockFailed);
        }
        if (stage == DpStage::CloseAdmission) {
            nativeTouched = true;
            if (!cleanup(io.closeAdmissionAndDrain(route, epoch), DpStage::Acquire)) return cleanupResult();
        } else if (stage == DpStage::Acquire) {
            const auto result = lease.connect(lanes);
            if (result == LinkResult::Quarantined) return quarantine();
            if (result != LinkResult::Ok || lease.mode() != Mode::DpAlt)
                return abort(DpResult::AcquisitionFailed);
            stage = DpStage::Lanes;
        } else if (stage == DpStage::Lanes) {
            uint32_t dpsp, dpmle, pins;
            if (!io.readFiaSnapshot(route, epoch, dpsp, dpmle, pins)) return abort(DpResult::Changed);
            LanePlan lanePlan{};
            if (planDpAltLanes(route, Mode::DpAlt, lanes, dpsp, dpmle, lanePlan) != LanePlanResult::Ok ||
                !planDklDpMode(route, Mode::DpAlt, lanes, dpsp, pins, modePlan))
                return abort(DpResult::Changed);
            phyTouched = true;
            if (!io.programLanes(route, epoch, lanePlan)) return abort(DpResult::ProgrammingFailed);
            stage = DpStage::ClockStart;
        } else if (stage == DpStage::ClockStart) {
            const auto result = pll.start(rate, reference, afcOverride, afc);
            if (result == PllResult::Busy) return DpResult::Progress;
            if (result != PllResult::Progress)
                return abort(DpResult::ClockFailed);
            stage = DpStage::ClockWait;
        } else if (stage == DpStage::ClockWait) {
            const auto result = pll.step();
            if (result == PllResult::Busy) return DpResult::Progress;
            if (result == PllResult::Ready) stage = DpStage::MapClock;
            else if (result != PllResult::Progress) return abort(DpResult::ClockFailed);
        } else if (stage == DpStage::MapClock) {
            const auto pllResult = pll.step();
            if (pllResult == PllResult::Busy) return DpResult::Progress;
            if (pllResult != PllResult::Ready) return abort(DpResult::ClockFailed);
            const auto result = mapStarted ? mapping.step() : mapping.start();
            if (result == ClockMapResult::Quarantined) return quarantine();
            if (result == ClockMapResult::Busy) return DpResult::Progress;
            if (result == ClockMapResult::Progress) mapStarted = true;
            else if (result == ClockMapResult::Ready) stage = DpStage::Mode;
            else return abort(DpResult::ProgrammingFailed);
        } else if (stage == DpStage::Mode) {
            if (!io.updateMode(route, epoch, modeWrite ? 0x10a0u : 0xa0u,
                               0xc0u, modePlan.laneGroupBits[modeWrite]))
                return abort(DpResult::ProgrammingFailed);
            if (++modeWrite == 2) {
                if (!planSignal()) return abort(DpResult::ProgrammingFailed);
                stage = DpStage::Signal;
            }
        } else if (stage == DpStage::Signal) {
            if (!io.updateSignal(route, epoch, signalPlan.writes[signalWrite]))
                return abort(DpResult::ProgrammingFailed);
            if (++signalWrite == 10) stage = trainingStarted ? DpStage::Training : DpStage::TrainingStart;
        } else if (stage == DpStage::TrainingStart) {
            trainingSince = io.nowMicros();
            trainingStarted = true;
            if (!io.trainingStart(route, epoch, lanes, rate)) return abort(DpResult::TrainingFailed);
            stage = DpStage::Training;
        } else if (stage == DpStage::Training) {
            const auto result = io.trainingStep(route, epoch, levels);
            if (result == TrainingResult::Failed) return abort(DpResult::TrainingFailed);
            if (result == TrainingResult::SinkVerified) {
                if (!sinkVerified()) return abort(DpResult::TrainingFailed);
                stage = DpStage::Publish;
            }
            if (result == TrainingResult::NeedDrive) {
                if (!planSignal()) return abort(DpResult::TrainingFailed);
                signalWrite = 0; stage = DpStage::Signal;
            }
        } else if (stage == DpStage::Publish) {
            if (!sinkVerified()) return abort(DpResult::TrainingFailed);
            if (!io.publishReady(route, epoch)) return abort(DpResult::PublicationFailed);
            stage = DpStage::Active;
            return DpResult::Active;
        } else if (stage == DpStage::Active) {
            return DpResult::Active;
        } else if (stage == DpStage::Drain) {
            if (!cleanup(io.closeAdmissionAndDrain(route, epoch), DpStage::RestorePhy)) return cleanupResult();
        } else if (stage == DpStage::RestorePhy) {
            if (phyTouched && !cleanup(io.restorePhy(route, epoch), DpStage::UnmapClock)) return cleanupResult();
            phyTouched = false; stage = DpStage::UnmapClock;
        } else if (stage == DpStage::UnmapClock) {
            const auto result = mapping.stop();
            if (result == ClockMapResult::Quarantined) return quarantine();
            if (result != ClockMapResult::Complete) return DpResult::CleanupPending;
            mapStarted = false; stage = DpStage::StopClock;
        } else if (stage == DpStage::StopClock) {
            const auto result = pll.stop();
            if (result == PllResult::Quarantined || pll.currentState() == PllState::Quarantined) return quarantine();
            if (result != PllResult::Complete) return DpResult::CleanupPending;
            stage = DpStage::Release;
        } else if (stage == DpStage::Release) {
            const auto result = lease.disconnect();
            if (result == LinkResult::Quarantined) return quarantine();
            if (result != LinkResult::Ok) return DpResult::CleanupPending;
            nativeTouched = false; stage = DpStage::Idle;
            return DpResult::Complete;
        }
        return DpResult::Progress;
    }
};
} // namespace YogaTypec
