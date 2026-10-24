#include "DpLinkSequence.hpp"
#include "NativeDssmRead.hpp"
#include "DpTrainingSequence.hpp"
#include <assert.h>
#include <iostream>
#include <map>
#include <string>
#include <vector>
using namespace YogaTypec;

struct LinkMock {
    int tokens[4] = {}, releaseCalls[4] = {};
    Acquisition acquisition[4] = {Acquisition::Verified, Acquisition::Verified,
                                 Acquisition::Verified, Acquisition::Verified};
    Cleanup release[4] = {}, drainResult = Cleanup::Complete;
    Cleanup restoreResult = Cleanup::Complete, unmapResult = Cleanup::Complete;
    bool allowed = true, cancel = false, admissionClosed = false, trained = false;
    bool mapped = false, phyDirty = false, training = false, published = false;
    bool powerAck = true, lockAck = true, unlockAck = true, offAck = true;
    bool snapshotOk = true, invalidDrive = false, pendingTraining = false;
    bool sinkReadOk = true;
    size_t sinkCount = 6;
    uint8_t sinkStatus[6] = {0x77, 0x77, 1, 0, 0, 0};
    Mode mode = Mode::DpAlt;
    uint32_t control = 0x100, pins = 3, laneMask = 15, selector = 0x123, gate = 0x4001;
    bool clockHeld = false;
    uint32_t dssm = 0, nativeReference = 0;
    bool dssmReadOk = true, inputsBusy = false;
    bool mappingHeld = false, mappingRetained = false;
    int mappingRetains = 0, mappingReleases = 0;
    NativeMmioSnapshot mmio = {1, 2, 3, 4, 5, 0xffff800010000000ull,
        0x6000000000ull, 0x1000000, 0x6000000000ull, 0x1000000,
        0xffff800010000000ull, 0xffff800010000000ull, 0x1000000, 0x1000000,
        9, true, true, true, false, false, true, true, true};
    std::vector<uint8_t> vbt;
    LinkMock() : vbt(79, 0) {
        memcpy(vbt.data(), "$VBT", 4); vbt[20] = 100; vbt[22] = 48;
        vbt[24] = 79; vbt[28] = 48;
        memcpy(vbt.data() + 48, "BIOS_DATA_BLOCK ", 16);
        vbt[64] = 247; vbt[66] = 22; vbt[68] = 31; vbt[70] = 1; vbt[71] = 6;
        uint8_t sum = 0; for (auto byte : vbt) sum = uint8_t(sum + byte);
        vbt[26] = uint8_t(0 - sum);
    }
    uint64_t time = 0;
    int mutation = 0, failMutation = -1, trainingSteps = 0, signalCalls = 0;
    int modeCalls = 0, trainingStarts = 0, publishCalls = 0;
    std::vector<std::string> events;
    std::map<uint32_t, uint32_t> indexed;

    bool authorized(Route r, uint64_t e) { assert(r.tcIndex == 2 && e == 9); return allowed; }
    bool cancelled(Route, uint64_t) { return cancel; }
    void powered() { for (int t : tokens) assert(t == 1); }
    bool mutate(const std::string &s) {
        events.push_back(s); return mutation++ != failMutation;
    }
    Acquisition get(int i) {
        assert(admissionClosed && !tokens[i]);
        if (i) assert(tokens[0]);
        if (acquisition[i] != Acquisition::Unavailable) tokens[i] = 1;
        events.push_back("get" + std::to_string(i)); return acquisition[i];
    }
    Cleanup put(int i) {
        assert(tokens[i] == 1 && admissionClosed && !training && !mapped && !phyDirty);
        assert(!(control & 0xcc000000u));
        for (int j = i + 1; j < 4; ++j) assert(!tokens[j]);
        ++releaseCalls[i]; events.push_back("put" + std::to_string(i));
        if (release[i] == Cleanup::Complete) tokens[i] = 0;
        return release[i];
    }
    Acquisition acquireCore() { return get(0); }
    Acquisition acquirePort(Route) { return get(1); }
    Acquisition takeOwnership(Route) { return get(2); }
    Acquisition blockCold(Route, Mode) { return get(3); }
    Cleanup releaseCore() { return put(0); }
    Cleanup releasePort(Route) { return put(1); }
    Cleanup dropOwnership(Route) { return put(2); }
    Cleanup unblockCold(Route, Mode) { return put(3); }
    Mode readMode(Route) { assert(tokens[0]); return mode; }
    bool ready(Route) { return true; }
    uint32_t readLaneMask(Route) { powered(); return laneMask; }
    bool quiesced(Route) { return admissionClosed && !training && !mapped; }
    bool quiesced(Route r, uint64_t) { return quiesced(r); }
    bool lockClock() { assert(!clockHeld); clockHeld = true; return true; }
    void unlockClock() { assert(clockHeld); clockHeld = false; }
    bool lockInputs() { return !inputsBusy && lockClock(); }
    void unlockInputs() { unlockClock(); }
    bool retainedVbt(Route, uint64_t, const uint8_t *&p, size_t &size) {
        powered(); assert(clockHeld); p = vbt.data(); size = vbt.size(); return true;
    }
    bool lockMapping() { powered(); assert(clockHeld && !mappingHeld); mappingHeld = true; return true; }
    void unlockMapping() { assert(mappingHeld && !mappingRetained); mappingHeld = false; }
    bool retainMapping(Route, uint64_t) {
        assert(clockHeld && mappingHeld && !mappingRetained);
        mappingRetained = true; ++mappingRetains; return true;
    }
    void releaseMapping() { assert(mappingHeld && mappingRetained); mappingRetained = false; ++mappingReleases; }
    bool snapshotRetained(NativeMmioSnapshot &out) {
        assert(mappingHeld && mappingRetained); out = mmio; return true;
    }
    bool readRetained32(uint32_t offset, uint32_t &v) {
        powered(); assert(clockHeld && mappingHeld && mappingRetained && offset == 0x51004);
        v = dssm; return dssmReadOk;
    }
    bool readDssmActual(uint32_t &v) {
        return readNativeDssmActualLocked(*this, Route{2}, 9, v) == DssmReadResult::Ok;
    }
    bool readNativeReferenceEncoding(uint32_t &v) { powered(); assert(clockHeld); v = nativeReference; return true; }
    bool mappingQuiesced(Route, uint64_t) { return admissionClosed && !training; }
    bool clockReady(Route, uint64_t) { return (control & 0xcc000000u) == 0xcc000000u; }
    uint64_t nowMicros() { return time; }
    bool readDirect(uint32_t a, uint32_t &v) {
        powered();
        assert(a == 0x46048 || (clockHeld && (a == 0x46114 || a == 0x164280)));
        v = a == 0x46048 ? control : a == 0x46114 ? selector : gate; return true;
    }
    bool writeDirect(uint32_t a, uint32_t v) {
        powered(); assert(admissionClosed && !training);
        if (a != 0x46048) {
            assert(clockHeld && (a == 0x46114 || a == 0x164280));
            if (a == 0x164280 && (v & 0x4000u)) {
                assert(!phyDirty);
                if (unmapResult != Cleanup::Complete) return false;
            }
            if (a == 0x46114) selector = v; else gate = v;
            mapped = (selector & 0xf0000000u) != 0 || !(gate & 0x4000u);
            return mutate(a == 0x46114 ? "selector" : "gate");
        }
        assert(!mapped);
        control = v;
        if (powerAck && (v & (1u << 27))) control |= 1u << 26;
        if (lockAck && (v & (1u << 31))) control |= 1u << 30;
        if (unlockAck && !(v & (1u << 31))) control &= ~(1u << 30);
        if (offAck && !(v & (1u << 27))) control &= ~(1u << 26);
        assert(control & 0x100);
        return mutate("pll-direct"); // false AFTER mutation, deliberately
    }
    bool updateIndexed(Route, uint64_t, uint32_t o, uint32_t mask, uint32_t bits) {
        powered(); assert(clockHeld && (control & 0xcc000000u) == 0x0c000000u);
        indexed[o] = (indexed[o] & ~mask) | bits; return mutate("pll-indexed");
    }
    bool readFiaSnapshot(Route, uint64_t, uint32_t &d, uint32_t &l, uint32_t &p) {
        powered(); d = laneMask; l = 0; p = pins; return snapshotOk;
    }
    bool programLanes(Route, uint64_t, const LanePlan &p) {
        powered(); assert(admissionClosed && p.mask); phyDirty = true; return mutate("lanes");
    }
    void clocked() { powered(); assert((control & 0xcc000000u) == 0xcc000000u); }
    bool updateMode(Route, uint64_t, uint32_t o, uint32_t m, uint32_t b) {
        clocked(); assert(mapped && (o == 0xa0 || o == 0x10a0) && m == 0xc0 && !(b & ~m));
        ++modeCalls; return mutate("mode");
    }
    bool updateSignal(Route, uint64_t, DklSignalWrite w) {
        clocked(); assert(mapped && !(w.bits & ~w.mask)); ++signalCalls; return mutate("signal");
    }
    bool trainingStart(Route, uint64_t, unsigned l, uint32_t r) {
        clocked(); assert(mapped && l == 4 && r == 270000 && admissionClosed);
        training = true; ++trainingStarts; return mutate("training-start");
    }
    TrainingResult trainingStep(Route, uint64_t, DpDriveLevel (&levels)[4]) {
        clocked(); assert(training && admissionClosed); events.push_back("training-step");
        if (pendingTraining) return TrainingResult::Pending;
        if (trainingSteps++ == 0) {
            levels[0] = {1, 2}; levels[1] = {2, 1}; levels[2] = {3, 0}; levels[3] = {0, 3};
            if (invalidDrive) levels[2] = {3, 1};
            return TrainingResult::NeedDrive;
        }
        trained = true; return TrainingResult::SinkVerified;
    }
    bool publishReady(Route, uint64_t) {
        clocked(); assert(trained && training && mapped); ++publishCalls;
        published = true; admissionClosed = false; return mutate("publish");
    }
    bool readSinkStatus(Route, uint64_t, uint8_t (&status)[6], size_t &transferred) {
        powered(); assert(training && trained && admissionClosed);
        for (unsigned i = 0; i < 6; ++i) status[i] = sinkStatus[i];
        transferred = sinkCount; events.push_back("sink-status"); return sinkReadOk;
    }
    Cleanup closeAdmissionAndDrain(Route, uint64_t) {
        admissionClosed = true; events.push_back("drain");
        if (drainResult == Cleanup::Complete) { training = published = false; }
        return drainResult;
    }
    Cleanup restorePhy(Route, uint64_t) {
        powered(); assert(admissionClosed && !training && phyDirty); events.push_back("restore");
        if (restoreResult == Cleanup::Complete) phyDirty = false;
        return restoreResult;
    }
    void empty() {
        assert(!mappingHeld && !mappingRetained && mappingRetains == mappingReleases);
        for (int t : tokens) assert(!t);
        assert(!mapped && !phyDirty && !training && !published && control == 0x100);
    }
};
using Sequence = DpLinkSequence<LinkMock>;
static void advance(Sequence &s, DpStage target) {
    for (int i = 0; i < 200 && s.currentStage() != target; ++i) {
        const auto r = s.step();
        assert(r == DpResult::Progress || r == DpResult::Active);
    }
    assert(s.currentStage() == target);
}
static void start(Sequence &s) { assert(s.start(4, 270000, 24000) == DpResult::Progress); }
static void drain(Sequence &s, LinkMock &m) {
    s.stop();
    for (int i = 0; i < 50 && s.currentStage() != DpStage::Idle; ++i) s.step();
    assert(s.currentStage() == DpStage::Idle && !s.needsCleanup()); m.empty();
}

// Execute the actual trainer inside the actual acquisition/clock/link sequence,
// not a second hand-written training return-value model. Hardware remains mock.
struct ProtocolLinkMock : LinkMock {
    DpTrainingSequence<ProtocolLinkMock> trainer;
    unsigned statusReads = 0;
    uint8_t sourcePattern = 0xff, sinkPattern = 0xff;
    bool shortAux = false, sourceDisableOk = true, sinkDisableReadback = true;
    ProtocolLinkMock() : trainer(*this, Route{2}, 9) {}
    bool updateSignal(Route r, uint64_t e, DklSignalWrite w) {
        indexed[w.offset] = w.fullWrite ? w.bits : (indexed[w.offset] & ~w.mask) | w.bits;
        return LinkMock::updateSignal(r,e,w);
    }
    bool linkParametersReady(Route, uint64_t, unsigned width, uint32_t rate) {
        clocked(); return admissionClosed && mapped && training && width == 4 && rate == 270000;
    }
    bool sourceSupportsPattern(Route, uint64_t, uint8_t) { return true; }
    bool sourceDriveMatches(Route r, uint64_t, const DpDriveLevel (&levels)[4]) {
        clocked(); DklSignalPlan plan{};
        if (!planDklDpSignal(r,Mode::DpAlt,4,270000,levels,plan)) return false;
        for (const auto &w : plan.writes)
            if ((indexed[w.offset] & w.mask) != w.bits) return false;
        return true;
    }
    bool setSourcePattern(Route, uint64_t, uint8_t pattern) {
        clocked(); assert(admissionClosed && training);
        sourcePattern = pattern; return mutate("source-pattern");
    }
    bool sourceTrainingDisabled(Route, uint64_t) { return sourceDisableOk && sourcePattern == 0; }
    bool writeDpcd(Route, uint64_t, uint32_t address, const uint8_t *data, size_t count, size_t &actual) {
        clocked(); assert(admissionClosed && training && (address == 0x102 || address == 0x103));
        if (address == 0x102) sinkPattern = data[0];
        actual = shortAux ? count - 1 : count; return mutate("aux-write");
    }
    bool readDpcd(Route, uint64_t, uint32_t address, uint8_t *data, size_t count, size_t &actual) {
        clocked(); assert(admissionClosed && training);
        for (size_t i = 0; i < count; ++i) data[i] = 0;
        if (address == 0) {
            assert(count == 16); data[0] = 0x14; data[1] = 30; data[2] = 0xc4; data[3] = 0x80;
        } else if (address == 0x102) {
            assert(count == 1); data[0] = sinkDisableReadback ? sinkPattern : 0x21;
        } else {
            assert(address == 0x202 && count == 6);
            if (statusReads++ == 0) { data[4] = 0x55; data[5] = 0x55; }
            else { data[0] = data[1] = 0x77; data[2] = 1; }
        }
        actual = shortAux ? count - 1 : count; return true;
    }
    bool trainingStart(Route, uint64_t, unsigned width, uint32_t rate) {
        training = true; ++trainingStarts;
        return trainer.start(width,rate) == DpTrainResult::Progress;
    }
    TrainingResult trainingStep(Route, uint64_t, DpDriveLevel (&levels)[4]) {
        const auto result = trainer.step(levels);
        if (result == DpTrainResult::NeedDrive) return TrainingResult::NeedDrive;
        if (result == DpTrainResult::Ready) { trained = true; return TrainingResult::SinkVerified; }
        return result == DpTrainResult::Progress ? TrainingResult::Pending : TrainingResult::Failed;
    }
};

int main() {
    {
        int mutations = 0;
        for (int fault = -1; fault < mutations; ++fault) {
            ProtocolLinkMock m; m.failMutation = fault;
            DpLinkSequence<ProtocolLinkMock> s(m,Route{2},9);
            assert(s.start(4,270000,24000) == DpResult::Progress);
            for (unsigned step = 0; step < 400 && s.currentStage() < DpStage::Active; ++step) {
                s.step(); m.time += 1000;
            }
            if (fault < 0) {
                assert(s.active() && m.sourcePattern == 0 && m.sinkPattern == 0);
                assert(m.signalCalls == 20 && m.statusReads == 4);
                mutations = m.mutation;
            } else {
                assert(!s.active() && s.currentStage() == DpStage::Drain);
                assert(m.mutation == fault + 1);
            }
            s.stop();
            for (unsigned step = 0; step < 60 && s.currentStage() != DpStage::Idle; ++step) s.step();
            assert(s.currentStage() == DpStage::Idle); m.empty();
        }
        for (int fault = 0; fault < 3; ++fault) {
            ProtocolLinkMock m; DpLinkSequence<ProtocolLinkMock> s(m,Route{2},9);
            assert(s.start(4,270000,24000) == DpResult::Progress);
            if (fault == 0) m.shortAux = true;
            if (fault == 1) m.sourceDisableOk = false;
            if (fault == 2) m.sinkDisableReadback = false;
            for (unsigned step = 0; step < 400 && s.currentStage() < DpStage::Drain; ++step) {
                s.step(); m.time += 1000;
            }
            assert(!s.active() && s.failureCause() == DpResult::TrainingFailed && !m.publishCalls);
            s.stop();
            for (unsigned step = 0; step < 60 && s.currentStage() != DpStage::Idle; ++step) s.step();
            assert(s.currentStage() == DpStage::Idle); m.empty();
        }
    }
    // Exhaustive lane-status nibbles for every legal active width. Inactive
    // lanes must not accidentally become prerequisites for one/two-lane links.
    for (unsigned width : {1u, 2u, 4u}) for (unsigned packed = 0; packed <= 0xffff; ++packed) {
        uint8_t status[6] = {uint8_t(packed), uint8_t(packed >> 8), 1, 0, 0, 0};
        bool expected = true;
        for (unsigned lane = 0; lane < width; ++lane)
            expected = expected && ((packed >> (lane * 4)) & 7) == 7;
        assert(dpSinkLinkVerified(width, status, 6) == expected);
        status[2] = 0; assert(!dpSinkLinkVerified(width, status, 6));
    }
    assert(!dpSinkLinkVerified(4, nullptr, 6));
    int bringupMutations;
    {
        LinkMock m; Sequence s(m, Route{2}, 9); start(s); advance(s, DpStage::Active);
        assert(m.signalCalls == 20 && m.trainingStarts == 1 && m.publishCalls == 1);
        assert(s.active() && s.needsCleanup()); bringupMutations = m.mutation;
        assert(s.start(4, 270000, 24000) == DpResult::Busy);
        drain(s, m);
        for (int calls : m.releaseCalls) assert(calls == 1);
        start(s); advance(s, DpStage::Active); drain(s, m);
    }
    // Every mutating bring-up callback may fail AFTER changing hardware/state.
    for (int fail = 0; fail < bringupMutations; ++fail) {
        LinkMock m; m.failMutation = fail; Sequence s(m, Route{2}, 9); start(s);
        for (int i = 0; i < 200 && s.currentStage() < DpStage::Drain; ++i) s.step();
        assert(s.currentStage() == DpStage::Drain && !s.active());
        assert(m.mutation == fail + 1); // no later programming after failure
        drain(s, m);
    }
    // Lock lost at each consumer boundary: no mapping/mode/drive/training/publication.
    for (auto target : {DpStage::MapClock, DpStage::Mode, DpStage::Signal,
                        DpStage::TrainingStart, DpStage::Training, DpStage::Publish, DpStage::Active}) {
        LinkMock m; Sequence s(m, Route{2}, 9); start(s); advance(s, target);
        const auto writes = m.mutation; const auto steps = m.trainingSteps;
        m.control &= ~(1u << 30);
        assert(s.step() == DpResult::ClockFailed && m.mutation == writes && m.trainingSteps == steps);
        drain(s, m);
    }
    for (int index = 0; index < 4; ++index)
    for (auto failure : {Acquisition::Unavailable, Acquisition::CleanupRequired}) {
        LinkMock m; m.acquisition[index] = failure; Sequence s(m, Route{2}, 9); start(s);
        s.step(); assert(s.step() == DpResult::AcquisitionFailed);
        assert(m.mutation == 0); drain(s, m);
    }
    for (int bad = 0; bad < 5; ++bad) {
        LinkMock m; Sequence s(m, Route{2}, 9); start(s); advance(s, DpStage::ClockStart);
        if (bad == 0) m.dssmReadOk = false;
        if (bad == 1) m.dssm = 0x60000000; // reserved clock encoding
        if (bad == 2) m.nativeReference = 1; // actual/native mismatch
        if (bad == 3) m.dssm = 0x20000000, m.nativeReference = 1; // plan mismatch
        if (bad == 4) m.vbt[0] = 0;
        const auto writes = m.mutation;
        assert(s.step() == DpResult::ClockFailed && m.mutation == writes && m.control == 0x100);
        drain(s, m);
    }
    for (auto target : {DpStage::Mode, DpStage::Signal, DpStage::TrainingStart,
                        DpStage::Training, DpStage::Publish, DpStage::Active}) {
        LinkMock m; Sequence s(m, Route{2}, 9); start(s); advance(s, target);
        m.gate |= 0x4000; const auto mutations = m.mutation;
        assert(s.step() == DpResult::ClockFailed && m.mutation == mutations);
        drain(s, m);
    }
    // Clock inputs remain authoritative after startup, including every indexed
    // PLL write and the final consumers. Busy exclusion never advances state or
    // publishes; changed/invalid inputs drain without another forward mutation.
    for (auto target : {DpStage::ClockStart, DpStage::ClockWait, DpStage::MapClock,
                        DpStage::Mode, DpStage::Signal, DpStage::TrainingStart,
                        DpStage::Training, DpStage::Publish, DpStage::Active}) {
        for (int bad = 0; bad < 3; ++bad) {
            LinkMock m; Sequence s(m, Route{2}, 9); start(s); advance(s, target);
            if (bad == 0) m.dssm = 0x20000000, m.nativeReference = 1;
            if (bad == 1) m.dssmReadOk = false;
            if (bad == 2) m.inputsBusy = true;
            const auto writes = m.mutation, training = m.trainingSteps, publishes = m.publishCalls;
            assert(s.step() == (bad == 2 ? DpResult::Progress : DpResult::ClockFailed));
            assert(m.mutation == writes && m.trainingSteps == training && m.publishCalls == publishes);
            if (bad == 2) assert(s.currentStage() == target);
            assert(!m.clockHeld);
            drain(s, m); // failed input validation must NOT strand the lease
        }
    }
    for (auto target : {DpStage::ClockStart, DpStage::ClockWait, DpStage::Mode,
                        DpStage::Training, DpStage::Publish, DpStage::Active}) {
        for (int bad = 0; bad < 4; ++bad) {
            LinkMock m; Sequence s(m, Route{2}, 9); start(s); advance(s, target);
            if (bad == 0) m.mmio.mapping = 0;
            if (bad == 1) m.mmio.managerAddress += 4;
            if (bad == 2) m.mmio.stopping = true;
            if (bad == 3) m.mmio.managerLength = 0x51004;
            const auto writes = m.mutation;
            assert(s.step() == DpResult::ClockFailed && m.mutation == writes);
            assert(!m.mappingHeld && !m.mappingRetained && m.mappingRetains == m.mappingReleases);
            // These fixtures represent rejected DSSM provenance, not lost link
            // identity. Cleanup still uses its own retained powered MMIO lease.
            drain(s, m);
        }
    }
    // Failed acquisition quarantines before any outer cleanup callback.
    {
        LinkMock m; m.acquisition[3] = Acquisition::CleanupRequired; m.release[3] = Cleanup::Quarantined;
        Sequence s(m, Route{2}, 9); start(s); s.step();
        assert(s.step() == DpResult::Quarantined);
        const auto events = m.events.size();
        assert(s.stop() == DpResult::Quarantined && s.step() == DpResult::Quarantined);
        assert(m.events.size() == events && s.needsCleanup()); m.powered();
    }
    // Admission is established before resource acquisition; retries cannot leak through.
    {
        LinkMock m; m.drainResult = Cleanup::Retry; Sequence s(m, Route{2}, 9); start(s);
        for (int i = 0; i < 10; ++i) assert(s.step() == DpResult::CleanupPending);
        for (int t : m.tokens) assert(!t);
        m.drainResult = Cleanup::Complete; advance(s, DpStage::Active); drain(s, m);
    }
    // Unplug/sleep on the retained identity drains; reused identity never mutates.
    for (auto target : {DpStage::CloseAdmission, DpStage::Acquire, DpStage::Lanes,
                        DpStage::ClockStart, DpStage::ClockWait, DpStage::MapClock,
                        DpStage::Mode, DpStage::Signal, DpStage::Training, DpStage::Active}) {
        for (bool lostEpoch : {false, true}) {
            LinkMock m; Sequence s(m, Route{2}, 9); start(s); advance(s, target);
            const auto events = m.events.size();
            if (lostEpoch) {
                m.allowed = false; assert(s.step() == DpResult::Quarantined);
                m.allowed = true; assert(s.stop() == DpResult::Quarantined);
                assert(m.events.size() == events);
            } else {
                m.cancel = true; assert(s.step() == DpResult::Changed); drain(s, m);
            }
        }
    }
    // Real component cleanup retries stop BEFORE dependent power is released.
    for (int which = 0; which < 7; ++which) {
        LinkMock m; Sequence s(m, Route{2}, 9); start(s); advance(s, DpStage::Active);
        Cleanup &result = which == 0 ? m.drainResult : which == 1 ? m.restoreResult :
                          which == 2 ? m.unmapResult : m.release[6 - which];
        result = Cleanup::Retry; s.stop();
        for (int i = 0; i < 30; ++i) s.step();
        assert(s.needsCleanup() && !s.active() && m.tokens[0]);
        if (which <= 2) { m.powered(); assert(m.control & (1u << 27)); }
        result = Cleanup::Complete; drain(s, m);
        for (int t : m.tokens) assert(!t);
    }
    for (int which = 0; which < 2; ++which) {
        LinkMock m; Sequence s(m, Route{2}, 9); start(s); advance(s, DpStage::Active);
        if (which == 0) m.unlockAck = false; else m.offAck = false;
        s.stop(); for (int i = 0; i < 10; ++i) s.step(); m.time = 1000;
        for (int i = 0; i < 10; ++i) s.step();
        assert(s.needsCleanup()); m.powered();
        m.unlockAck = m.offAck = true;
        m.control &= ~(1u << 30);
        if (!(m.control & (1u << 27))) m.control &= ~(1u << 26);
        drain(s, m);
    }
    for (int which = 0; which < 7; ++which) {
        LinkMock m; Sequence s(m, Route{2}, 9); start(s); advance(s, DpStage::Active);
        // Mapping has its own accounting/epoch quarantine tests; this injects
        // every callback-based quarantine in the composed cleanup path.
        if (which == 2) continue;
        Cleanup &result = which == 0 ? m.drainResult : which == 1 ? m.restoreResult : m.release[6 - which];
        result = Cleanup::Quarantined; s.stop();
        for (int i = 0; i < 30 && s.currentStage() != DpStage::Quarantined; ++i) s.step();
        assert(s.currentStage() == DpStage::Quarantined && s.needsCleanup());
        const auto events = m.events.size(); result = Cleanup::Complete;
        assert(s.stop() == DpResult::Quarantined && s.step() == DpResult::Quarantined);
        assert(m.events.size() == events);
    }
    {
        LinkMock m; m.invalidDrive = true; Sequence s(m, Route{2}, 9); start(s);
        advance(s, DpStage::Training);
        assert(s.step() == DpResult::TrainingFailed && m.signalCalls == 10 && !m.publishCalls);
        drain(s, m);
    }
    {
        LinkMock m; m.pendingTraining = true; Sequence s(m, Route{2}, 9); start(s);
        advance(s, DpStage::Training); for (int i = 0; i < 20; ++i) s.step();
        assert(!m.publishCalls && !s.active()); m.time = 1000000;
        assert(s.step() == DpResult::TrainingTimedOut); drain(s, m);
    }
    for (auto target : {DpStage::Training, DpStage::Publish}) for (int bad = 0; bad < 7; ++bad) {
        LinkMock m; Sequence s(m, Route{2}, 9); start(s); advance(s, target);
        if (target == DpStage::Training) {
            s.step(); // drive request
            advance(s, DpStage::Training);
        }
        if (bad == 0) m.sinkReadOk = false;
        if (bad == 1) m.sinkCount = 5;
        if (bad >= 2 && bad <= 4) m.sinkStatus[1] &= ~(1u << (bad - 2));
        if (bad == 5) m.sinkStatus[2] = 0;
        if (bad == 6) for (auto &byte : m.sinkStatus) byte = 0xff;
        assert(s.step() == DpResult::TrainingFailed && !m.publishCalls); drain(s, m);
    }
    for (auto target : {DpStage::Signal, DpStage::Publish}) {
        LinkMock m; Sequence s(m, Route{2}, 9); start(s); advance(s, DpStage::Training);
        s.step(); advance(s, target); m.time = 1000000;
        assert(s.step() == DpResult::TrainingTimedOut && !m.publishCalls); drain(s, m);
    }
    std::cout << "PASS: real acquisition/PLL/link composition, all mutation failures, lock loss, drive requests, admission, cancellation and cleanup\n";
}
