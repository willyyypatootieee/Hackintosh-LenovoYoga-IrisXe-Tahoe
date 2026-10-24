#include "DpTrainingSequence.hpp"
#include "NativeAuxCommand.hpp"
#include <assert.h>
#include <string.h>
#include <array>
#include <iostream>
#include <vector>
using namespace YogaTypec;
struct TrainingMock {
    uint64_t now = 0;
    bool allowed = true, cancel = false, parameters = true, disabled = true;
    bool tps3 = true, tps4 = true, ioOk = true, shortRead = false, shortWrite = false;
    bool failDrive = false;
    unsigned shortAt = 0;
    unsigned calls = 0, failAt = 0, revokeAt = 0;
    uint64_t callbackTime = 0;
    uint8_t caps[16] = {0x14, 30, 0xc4, 0x80};
    DpDriveLevel applied[4] = {};
    std::vector<std::array<uint8_t, 6>> statuses = {{{0x11,0x11,0,0,0,0}},
        {{0x77,0x77,1,0,0,0}}, {{0x77,0x77,1,0,0,0}}};
    size_t statusIndex = 0;
    std::vector<uint8_t> patterns;
    struct Write { uint32_t address; std::vector<uint8_t> bytes; };
    std::vector<Write> writes;
    bool authorized(Route r, uint64_t e) { assert(r.tcIndex == 2 && e == 9); return allowed; }
    bool cancelled(Route, uint64_t) { return cancel; }
    uint64_t nowMicros() { return now; }
    bool operation() {
        assert(allowed); ++calls; now += callbackTime;
        if (revokeAt == calls) allowed = false;
        return ioOk && calls != failAt;
    }
    bool linkParametersReady(Route, uint64_t, unsigned, uint32_t) { return parameters; }
    bool sourceSupportsPattern(Route, uint64_t, uint8_t pattern) { return pattern == 7 ? tps4 : tps3; }
    bool sourceDriveMatches(Route, uint64_t, const DpDriveLevel (&levels)[4]) {
        if (failDrive) return false;
        for (unsigned lane = 0; lane < 4; ++lane)
            if (levels[lane].voltageSwing != applied[lane].voltageSwing ||
                levels[lane].preEmphasis != applied[lane].preEmphasis) return false;
        return true;
    }
    bool sourceTrainingDisabled(Route, uint64_t) { return disabled && !patterns.empty() && patterns.back() == 0; }
    bool setSourcePattern(Route, uint64_t, uint8_t value) { patterns.push_back(value); return operation(); }
    bool writeDpcd(Route, uint64_t, uint32_t address, const uint8_t *p, size_t count, size_t &actual) {
        writes.push_back({address, std::vector<uint8_t>(p, p + count)});
        actual = shortWrite || calls + 1 == shortAt ? count - 1 : count; return operation();
    }
    bool readDpcd(Route, uint64_t, uint32_t address, uint8_t *p, size_t count, size_t &actual) {
        if (address == 0) {
            assert(count == 16); for (size_t i = 0; i < count; ++i) p[i] = caps[i];
        } else if (address == 0x102) {
            assert(count == 1); p[0] = writes.back().bytes[0];
        } else {
            assert(address == 0x202 && count == 6 && statusIndex < statuses.size());
            const auto &s = statuses[statusIndex++]; for (size_t i = 0; i < count; ++i) p[i] = s[i];
        }
        actual = shortRead || calls + 1 == shortAt ? count - 1 : count; return operation();
    }
};
using Trainer = DpTrainingSequence<TrainingMock>;
template<class T, class M>
static DpTrainResult run(T &t, M &m, bool apply = true) {
    DpDriveLevel levels[4] = {};
    for (unsigned i = 0; i < 1500; ++i) {
        const auto result = t.step(levels);
        if (result == DpTrainResult::NeedDrive && apply)
            for (unsigned lane = 0; lane < 4; ++lane) m.applied[lane] = levels[lane];
        if (result != DpTrainResult::Progress && result != DpTrainResult::NeedDrive) return result;
        m.now += 1000;
    }
    assert(false); return DpTrainResult::Failed;
}
// Compose the real AUX adapter and trainer, not just two independently mocked
// boolean interfaces. Only the physical transport and source remain fake here.
struct NativeTrainingMock : TrainingMock {
    NativeAuxCommand<NativeTrainingMock> aux;
    bool auxLocked = false, completed = false;
    unsigned commands = 0, shortCommand = 0, nackCommand = 0, changeCommand = 0;
    uint32_t control = 0;
    uint8_t wire[20] = {}, sinkPattern = 0;
    NativeTrainingMock() : aux(*this) {}
    bool tryLockAux() { assert(!auxLocked); return auxLocked = true; }
    void unlockAux() { assert(auxLocked); auxLocked = false; }
    bool readActual32(uint32_t address, uint32_t &value) {
        assert(auxLocked);
        if (address == 0x64510) { value = control; return true; }
        assert(completed && address >= 0x64514 && address <= 0x64524);
        value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value |= uint32_t(wire[address - 0x64514 + i]) << (24 - 8 * i);
        return true;
    }
    uint32_t runCaptured(uint32_t command, uint32_t *words, uint32_t txBytes, bool flag) {
        assert(auxLocked && !flag); ++commands;
        const auto address = (words[0] >> 8) & 0xfffff;
        const size_t count = (words[0] & 255) + 1;
        assert(count <= 16 && command <= 1);
        assert(txBytes == (command ? count + 4 : 4));
        memset(wire,0,sizeof(wire));
        size_t actual = 0;
        if (command) {
            // Hardware may mutate even on uncertain completion: preserve write
            // evidence and let trainer touched() force outer cleanup.
            const auto *payload = reinterpret_cast<uint8_t *>(words + 1);
            TrainingMock::writeDpcd({2},9,address,payload,count,actual);
            if (address == 0x102) sinkPattern = payload[0];
            control = (1u << 30) | (1u << 20);
            if (commands == shortCommand) { control += 1u << 20; wire[1] = uint8_t(count - 1); }
        } else {
            if (address == 0x102) { wire[1] = sinkPattern; actual = 1; }
            else TrainingMock::readDpcd({2},9,address,wire + 1,count,actual);
            control = (1u << 30) | (uint32_t(actual + 1) << 20);
            if (commands == shortCommand) control -= 1u << 20;
        }
        if (commands == changeCommand) allowed = false;
        if (commands == nackCommand) wire[0] = 0x10;
        completed = true;
        memset(words,0xa5,20); // requested/scratch bytes cannot fake completion
        return commands == nackCommand ? 0xe00002f0 : 0;
    }
    bool writeDpcd(Route r, uint64_t e, uint32_t a, const uint8_t *p, size_t n, size_t &actual) {
        const auto result = aux.write(r,e,a,p,n); actual = result.actual;
        return result.result == NativeAuxResult::Ok;
    }
    bool readDpcd(Route r, uint64_t e, uint32_t a, uint8_t *p, size_t n, size_t &actual) {
        const auto result = aux.read(r,e,a,p,n); actual = result.actual;
        return result.result == NativeAuxResult::Ok;
    }
    uint64_t nowMicros() { assert(!auxLocked); return now; }
    bool setSourcePattern(Route r, uint64_t e, uint8_t value) {
        assert(!auxLocked); return TrainingMock::setSourcePattern(r,e,value);
    }
};
static void advance(Trainer &t, TrainingMock &m, DpTrainStage target) {
    DpDriveLevel levels[4] = {};
    for (unsigned i = 0; i < 1500 && t.currentStage() != target; ++i) {
        const auto result = t.step(levels);
        assert(result == DpTrainResult::Progress || result == DpTrainResult::NeedDrive);
        if (result == DpTrainResult::NeedDrive)
            for (unsigned lane = 0; lane < 4; ++lane) m.applied[lane] = levels[lane];
        m.now += 1000;
    }
    assert(t.currentStage() == target);
}
int main() {
    {
        NativeTrainingMock m; DpTrainingSequence<NativeTrainingMock> t(m,{2},9);
        assert(t.start(2,540000) == DpTrainResult::Progress);
        assert(run(t,m) == DpTrainResult::Ready && !m.auxLocked);
        assert(m.commands == 8 && t.touched() && m.sinkPattern == 0);
    }
    // Each AUX read/write independently short/NACK/epoch loss. No case may
    // report trained output; training waits/source updates never hold AUX lock.
    for (unsigned failure = 1; failure <= 8; ++failure) for (unsigned kind = 0; kind < 3; ++kind) {
        NativeTrainingMock m;
        if (kind == 0) m.shortCommand = failure;
        if (kind == 1) m.nackCommand = failure;
        if (kind == 2) m.changeCommand = failure;
        DpTrainingSequence<NativeTrainingMock> t(m,{2},9);
        const auto start = t.start(2,540000);
        const auto result = start == DpTrainResult::Progress ? run(t,m) : start;
        assert(result == (kind == 2 ? DpTrainResult::Quarantined : DpTrainResult::Failed));
        assert(m.commands == failure && !m.auxLocked);
        assert(t.touched() == (failure > 1));
    }
    // Exhaust all request nibbles and the ten-entry signal planner together.
    for (unsigned request = 0; request < 16; ++request) {
        DpDriveLevel levels[4] = {};
        const auto byte = dpAdlpTrainingByte(request, levels[0]);
        assert(levels[0].preEmphasis == request >> 2);
        assert(levels[0].voltageSwing + levels[0].preEmphasis <= 3);
        assert((byte & 3) == levels[0].voltageSwing);
        assert(((byte >> 3) & 3) == levels[0].preEmphasis);
        assert(bool(byte & 4) == (levels[0].voltageSwing == 3));
        assert(bool(byte & 0x20) == (levels[0].preEmphasis == 3));
        DklSignalPlan plan{};
        assert(planDklDpSignal(Route{2}, Mode::DpAlt, 1, 540000, levels, plan));
    }
    for (unsigned lanes : {1u, 2u, 4u}) for (uint32_t rate : {162000u,270000u,540000u,810000u}) {
        TrainingMock m; Trainer t(m, Route{2}, 9);
        assert(t.start(lanes, rate) == DpTrainResult::Progress && !t.touched());
        assert(run(t, m) == DpTrainResult::Ready && t.touched());
        assert((m.patterns == std::vector<uint8_t>{0x21,7,0}));
        assert(m.writes.size() == 3 && m.writes[0].address == 0x102);
        assert(m.writes[0].bytes.size() == lanes + 1 && m.writes[1].bytes.size() == lanes + 1);
        assert(m.writes.back().bytes == std::vector<uint8_t>{0});
        // Ready is not a permanent cached success: another real read is required.
        m.statuses.push_back({{0,0,0,0,0,0}});
        assert(run(t, m) == DpTrainResult::Failed);
    }
    for (unsigned pattern : {2u,3u,4u}) {
        TrainingMock m; m.tps4 = pattern == 4; m.tps3 = pattern == 3;
        Trainer t(m, Route{2}, 9); assert(t.start(4, 540000) == DpTrainResult::Progress);
        assert(run(t, m) == DpTrainResult::Ready);
        assert(m.patterns[1] == (pattern == 4 ? 7 : uint8_t(0x20 | pattern)));
    }
    // Capability refusal, no programming and no guessed fallback.
    for (int bad = 0; bad < 13; ++bad) {
        TrainingMock m;
        if (bad == 0) m.caps[0] = 0xff;
        if (bad == 1) m.caps[1] = 0xff;
        if (bad == 2) m.caps[2] = 3;
        if (bad == 3) m.caps[14] = 0x80; // extended cap block unsupported
        if (bad == 4) m.caps[14] = 5;
        if (bad == 5) m.caps[1] = 10;
        if (bad == 6) m.caps[2] = 2;
        if (bad == 7) m.shortRead = true;
        if (bad == 8) m.parameters = false;
        if (bad == 9) m.failDrive = true;
        if (bad == 10) m.tps4 = false; // HBR3 requires TPS4
        if (bad == 11) m.caps[0] = 0x20;
        if (bad == 12) m.ioOk = false;
        Trainer t(m, Route{2}, 9);
        assert(t.start(4, 810000) == DpTrainResult::Failed && !t.touched());
        assert(m.patterns.empty() && m.writes.empty());
    }
    // No premature status read. DP1.4 CR100us, EQ interval*4ms; DP1.3 uses
    // interval*4ms for both. Zero-interval EQ400us, not a mandatory500ms sleep.
    for (uint8_t rev : {uint8_t(0x13), uint8_t(0x14)}) for (unsigned interval = 0; interval <= 4; ++interval) {
        TrainingMock m; m.caps[0] = rev; m.caps[14] = uint8_t(interval);
        Trainer t(m, Route{2}, 9); assert(t.start(4, 540000) == DpTrainResult::Progress);
        DpDriveLevel levels[4] = {};
        assert(t.step(levels) == DpTrainResult::Progress); // source
        assert(t.step(levels) == DpTrainResult::Progress); // sink, start wait
        const auto delay = rev == 0x14 || interval == 0 ? 100u : interval * 4000;
        m.now = delay - 1; assert(t.step(levels) == DpTrainResult::Progress && m.statusIndex == 0);
        ++m.now; assert(t.step(levels) == DpTrainResult::Progress && m.statusIndex == 1);
        assert(t.step(levels) == DpTrainResult::Progress); // source EQ
        assert(t.step(levels) == DpTrainResult::Progress); // sink EQ
        m.now += interval == 0 ? 399 : interval * 4000 - 1;
        assert(t.step(levels) == DpTrainResult::Progress && m.statusIndex == 1);
        ++m.now; assert(t.step(levels) == DpTrainResult::Progress && m.statusIndex == 2);
        assert(run(t, m) == DpTrainResult::Ready);
    }
    // Actual lane-adjust request → source drive acknowledgment → exact sink
    // lane-set bytes. Inactive logical source lanes stay initialized to zero.
    for (unsigned lanes : {1u,2u,4u}) {
        TrainingMock m; m.statuses.insert(m.statuses.begin(), {{0,0,0,0,0x65,0x13}});
        Trainer t(m, Route{2}, 9); assert(t.start(lanes, 540000) == DpTrainResult::Progress);
        assert(run(t, m) == DpTrainResult::Ready);
        assert(m.writes.size() == 4 && m.writes[1].address == 0x103 && m.writes[1].bytes.size() == lanes);
        assert(m.writes[1].bytes[0] == 9); // v1,p1
        if (lanes >= 2) assert(m.writes[1].bytes[1] == 10); // v2,p1
        if (lanes == 4) assert(m.writes[1].bytes[2] == 7); // v3,max
        for (unsigned lane = lanes; lane < 4; ++lane)
            assert(!m.applied[lane].voltageSwing && !m.applied[lane].preEmphasis);
    }
    {
        TrainingMock m; m.statuses.insert(m.statuses.begin(), {{0,0,0,0,0x11,0x11}});
        Trainer t(m, Route{2}, 9); assert(t.start(4, 540000) == DpTrainResult::Progress);
        assert(run(t, m, false) == DpTrainResult::Failed); // no source applied acknowledgment
        assert(m.writes.size() == 1); // don't tell sink an unapplied level
    }
    // Same request limit, all-lane max swing, CR loss during EQ and EQ retry cap.
    for (int bad = 0; bad < 5; ++bad) {
        TrainingMock m;
        if (bad == 0) m.statuses.assign(6, {{0,0,0,0,0,0}});
        if (bad == 1) m.statuses.assign(3, {{0,0,0,0,0x33,0x33}});
        if (bad == 2) m.statuses[1] = {{0,0,1,0,0,0}};
        if (bad == 3) { m.statuses.assign(6, {{0x11,0x11,0,0,0,0}}); }
        if (bad == 4) m.statuses[0].fill(0xff);
        Trainer t(m, Route{2}, 9); assert(t.start(4, 540000) == DpTrainResult::Progress);
        assert(run(t, m) == DpTrainResult::Failed);
        assert(m.statusIndex == (bad == 0 ? 5u : bad == 1 || bad == 2 ? 2u : bad == 3 ? 6u : 1u));
        assert(t.touched());
    }
    // Fail or revoke each AUX/source callback, including final sink/source
    // disable and independent status read. Dirty state survives failed writes.
    for (bool revoke : {false,true}) for (unsigned call = 1; call <= 11; ++call) {
        TrainingMock m; if (revoke) m.revokeAt = call; else m.failAt = call;
        Trainer t(m, Route{2}, 9); auto result = t.start(4, 540000);
        if (result == DpTrainResult::Progress) result = run(t, m);
        assert(result == (revoke ? DpTrainResult::Quarantined : DpTrainResult::Failed));
        assert(m.calls == call && t.touched() == (call > 1));
    }
    for (unsigned call : {1u,3u,4u,6u,7u,8u,10u,11u}) {
        TrainingMock m; m.shortAt = call;
        Trainer t(m,Route{2},9); auto result = t.start(4,540000);
        if (result == DpTrainResult::Progress) result = run(t,m);
        assert(result == DpTrainResult::Failed && m.calls == call);
    }
    // A callback that consumes the remaining budget cannot authorize the next
    // callback or even publish final Ready from a formerly valid status read.
    for (auto target : {DpTrainStage::SourceCr,DpTrainStage::SinkCr,
                        DpTrainStage::VerifySinkDisabled,DpTrainStage::FinalStatus}) {
        TrainingMock m; Trainer t(m,Route{2},9);
        assert(t.start(4,540000) == DpTrainResult::Progress); advance(t,m,target);
        const auto calls = m.calls; m.callbackTime = 1000000;
        assert(run(t,m) == DpTrainResult::TimedOut && m.calls == calls + 1);
    }
    // Changing requests must not evade the total CR attempt bound.
    for (uint8_t rev : {uint8_t(0x13),uint8_t(0x14)}) {
        TrainingMock m; m.caps[0] = rev;
        const unsigned count = rev == 0x14 ? 10 : 80;
        m.statuses.assign(count, {{0,0,0,0,0,0}});
        for (unsigned i=0; i<count; ++i) m.statuses[i][4] = m.statuses[i][5] = i % 2 ? 0x11 : 0;
        Trainer t(m,Route{2},9); assert(t.start(4,540000) == DpTrainResult::Progress);
        assert(run(t,m) == DpTrainResult::Failed && m.statusIndex == count);
    }
    for (auto target : {DpTrainStage::SourceCr,DpTrainStage::SinkCr,DpTrainStage::WaitCr,
                        DpTrainStage::SourceEq,DpTrainStage::SinkEq,DpTrainStage::WaitEq,
                        DpTrainStage::DisableSink,DpTrainStage::DisableSource,
                        DpTrainStage::VerifySinkDisabled,DpTrainStage::FinalStatus}) {
        for (int bad = 0; bad < 5; ++bad) {
            TrainingMock m; Trainer t(m, Route{2}, 9);
            assert(t.start(4,540000) == DpTrainResult::Progress); advance(t,m,target);
            const auto calls = m.calls;
            if (bad == 0) m.now = 1000000;
            if (bad == 1) m.allowed = false;
            if (bad == 2) m.cancel = true;
            if (bad == 3) m.parameters = false;
            if (bad == 4) { m.shortRead = true; m.shortWrite = true; m.ioOk = false; }
            const auto result = run(t,m);
            assert(result != DpTrainResult::Ready);
            if (bad < 4) assert(m.calls == calls);
        }
    }
    {
        TrainingMock m; Trainer t(m, Route{2},9); DpDriveLevel levels[4] = {};
        assert(t.start(4,540000) == DpTrainResult::Progress);
        t.step(levels); m.now = 50; t.step(levels);
        m.now = 75; assert(t.step(levels) == DpTrainResult::Progress);
        m.now = 74; assert(t.step(levels) == DpTrainResult::TimedOut);
    }
    std::cout << "PASS: bounded real-count DPRX CR/EQ, pattern/drive ordering, capability/delay/retry policy, fresh final status, and uncertain-write obligations\n";
}
