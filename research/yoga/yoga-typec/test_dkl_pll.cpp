#include "DklPllSequence.hpp"
#include <assert.h>
#include <iostream>
#include <map>
#include <vector>
using namespace YogaTypec;

struct PllMock {
    uint32_t control = 0x100, expectedAddress = 0x46048;
    uint64_t time = 0;
    bool allowed = true, quiet = true, readOk = true, writeOk = true;
    bool powerAck = true, lockAck = true, unlockAck = true, offAck = true;
    bool inputsHeld = false, inputsBusy = false, inputsOk = true;
    uint32_t dssm = 0, nativeReference = 0;
    std::vector<uint8_t> vbt;
    PllMock() : vbt(79, 0) {
        memcpy(vbt.data(), "$VBT", 4); vbt[20] = 100; vbt[22] = 48;
        vbt[24] = 79; vbt[28] = 48;
        memcpy(vbt.data() + 48, "BIOS_DATA_BLOCK ", 16);
        vbt[64] = 247; vbt[66] = 22; vbt[68] = 31; vbt[70] = 1; vbt[71] = 6;
        uint8_t sum = 0; for (auto byte : vbt) sum = uint8_t(sum + byte);
        vbt[26] = uint8_t(0 - sum);
    }
    int indexedFail = -1, indexedCalls = 0, directCalls = 0;
    std::map<uint32_t, uint32_t> regs;
    std::vector<uint32_t> order;
    bool authorized(Route r, uint64_t epoch) { assert(r.tcIndex == 2 && epoch == 9); return allowed; }
    bool quiesced(Route, uint64_t) { return quiet; }
    bool lockInputs() { assert(!inputsHeld); if (inputsBusy) return false; inputsHeld = true; return true; }
    void unlockInputs() { assert(inputsHeld); inputsHeld = false; }
    bool retainedVbt(Route, uint64_t, const uint8_t *&p, size_t &size) {
        assert(inputsHeld); p = vbt.data(); size = vbt.size(); return true;
    }
    bool readDssmActual(uint32_t &v) { assert(inputsHeld); v = dssm; return inputsOk; }
    bool readNativeReferenceEncoding(uint32_t &v) { assert(inputsHeld); v = nativeReference; return true; }
    uint64_t nowMicros() { return time; }
    bool readDirect(uint32_t address, uint32_t &out) {
        assert(address == expectedAddress); out = control; return readOk;
    }
    bool writeDirect(uint32_t address, uint32_t value) {
        assert(address == expectedAddress); ++directCalls;
        if ((value & 0x88000000u) & ~(control & 0x88000000u)) assert(inputsHeld);
        control = value;
        if (powerAck && (value & (1u << 27))) control |= 1u << 26;
        if (lockAck && (value & (1u << 31))) control |= 1u << 30;
        if (unlockAck && !(value & (1u << 31))) control &= ~(1u << 30);
        if (offAck && !(value & (1u << 27))) control &= ~(1u << 26);
        assert(control & 0x100); // unrelated bit preserved
        return writeOk; // failure can still have reached hardware
    }
    bool updateIndexed(Route, uint64_t, uint32_t offset, uint32_t mask, uint32_t bits) {
        assert((control & 0xcc000000u) == 0x0c000000u); // powered, disabled/unlocked
        assert(inputsHeld);
        assert(!(bits & ~mask));
        order.push_back(offset);
        auto &reg = regs.emplace(offset, 0xa5a5a5a5u).first->second;
        const auto before = reg;
        reg = (reg & ~mask) | bits;
        assert((reg & ~mask) == (before & ~mask));
        return indexedCalls++ != indexedFail;
    }
};

static void bringUp(DklPllSequence<PllMock> &seq) {
    assert(seq.start(270000, 24000) == PllResult::Progress);
    for (unsigned n = 0; n < 12 && !seq.ready(); ++n) seq.step();
    assert(seq.ready() && seq.needsCleanup());
}
static void drain(DklPllSequence<PllMock> &seq) {
    assert(seq.stop() == PllResult::Progress);
    for (unsigned n = 0; n < 5 && seq.needsCleanup(); ++n) seq.step();
    assert(seq.currentState() == PllState::Idle && !seq.needsCleanup());
}

int main(int argc, char **argv) {
    // Machine-readable plan output is compared with compiled pinned Linux in
    // the Python test, not only with a duplicate local C++ formula.
    if (argc == 2 && std::string(argv[1]) == "plans") {
        for (uint32_t rate : {162000u, 270000u, 540000u, 810000u})
        for (uint32_t ref : {19200u, 24000u, 38400u})
        for (unsigned afc = 0; afc <= 8; ++afc) {
            DklPllPlan p{};
            assert(planDklDpPll(Route{2}, Mode::DpAlt, rate, ref, afc != 0, afc ? afc-1 : 0, p));
            std::cout << rate << ' ' << ref << ' ' << afc << ' ' << unsigned(p.div1) << ' ' << unsigned(p.div2);
            for (auto w : p.writes) std::cout << ' ' << w.offset << ' ' << w.mask << ' ' << w.bits;
            std::cout << '\n';
        }
        return 0;
    }
    {
        PllMock m; DklPllSequence<PllMock> seq(m, Route{2}, 9);
        bringUp(seq);
        assert(m.order == (std::vector<uint32_t>{0x212c,0x20d8,0x20d4,0x2200,0x2204,0x2210,0x2214,0x2218}));
        assert(seq.start(270000, 24000) == PllResult::Busy);
        m.quiet = false;
        const int writes = m.directCalls;
        assert(seq.stop() == PllResult::UsersActive && m.directCalls == writes);
        m.quiet = true; drain(seq);
        assert(m.control == 0x100 && seq.stop() == PllResult::Complete);
        bringUp(seq); drain(seq);
    }
    for (unsigned bit : {26u,27u,30u,31u}) {
        PllMock m; m.control |= 1u << bit;
        DklPllSequence<PllMock> seq(m, Route{2}, 9);
        assert(seq.start(270000, 24000) == PllResult::ExistingOwner);
        assert(!seq.needsCleanup() && m.directCalls == 0);
    }
    for (int fail = 0; fail < 8; ++fail) {
        PllMock m; m.indexedFail = fail;
        DklPllSequence<PllMock> seq(m, Route{2}, 9);
        assert(seq.start(270000, 24000) == PllResult::Progress);
        for (unsigned n = 0; n < 10 && seq.currentState() != PllState::Failed; ++n) seq.step();
        assert(seq.currentState() == PllState::Failed && !seq.ready());
        const int calls = m.indexedCalls;
        assert(seq.step() == PllResult::WriteUnverified && calls == m.indexedCalls);
        drain(seq);
        assert(m.control == 0x100); // no PLL enable after partial programming
    }
    {
        PllMock m; m.powerAck = false;
        DklPllSequence<PllMock> seq(m, Route{2}, 9);
        seq.start(270000, 24000); m.time = 999;
        assert(seq.step() == PllResult::Progress);
        m.time = 1000; assert(seq.step() == PllResult::TimedOut);
        assert(!seq.ready() && m.indexedCalls == 0 && seq.needsCleanup());
        drain(seq);
    }
    {
        PllMock m; m.lockAck = false;
        DklPllSequence<PllMock> seq(m, Route{2}, 9);
        seq.start(270000, 24000);
        for (unsigned n = 0; n < 10; ++n) seq.step();
        assert(seq.currentState() == PllState::LockWait);
        m.time = 1000; assert(seq.step() == PllResult::TimedOut && !seq.ready());
        drain(seq);
    }
    for (int which = 0; which < 2; ++which) {
        PllMock m; DklPllSequence<PllMock> seq(m, Route{2}, 9); bringUp(seq);
        if (which == 0) m.unlockAck = false; else m.offAck = false;
        seq.stop(); seq.step(); m.time = 1000;
        assert(seq.step() == PllResult::TimedOut && seq.needsCleanup());
        if (which == 0) assert(m.control & (1u << 27)); // power held while lock uncertain
        m.unlockAck = m.offAck = true;
        // Hardware may settle asynchronously before the explicit retry.
        m.control &= ~(1u << 30);
        if (!(m.control & (1u << 27))) m.control &= ~(1u << 26);
        drain(seq);
    }
    {
        PllMock m; m.writeOk = false;
        DklPllSequence<PllMock> seq(m, Route{2}, 9);
        assert(seq.start(270000,24000) == PllResult::WriteUnverified && seq.needsCleanup());
        m.writeOk = true; drain(seq);
    }
    {
        PllMock m; DklPllSequence<PllMock> seq(m, Route{2}, 9); bringUp(seq);
        m.allowed = false; const int before = m.directCalls;
        assert(seq.stop() == PllResult::Unauthorized);
        m.allowed = true;
        assert(seq.step() == PllResult::Quarantined && seq.stop() == PllResult::Quarantined);
        assert(m.directCalls == before && seq.needsCleanup()); // no new-epoch cleanup
    }
    {
        PllMock m; DklPllSequence<PllMock> seq(m, Route{2}, 9);
        assert(seq.start(270000, 0) == PllResult::InvalidPlan);
        m.control = UINT32_MAX;
        assert(seq.start(270000, 24000) == PllResult::ReadFailed && m.directCalls == 0);
    }
    // Each forward transition (power request, wait, eight writes, enable, lock,
    // and Ready consumption) validates inside the same held input exclusion.
    for (unsigned at = 0; at <= 12; ++at) for (int bad = 0; bad < 5; ++bad) {
        PllMock m; DklPllSequence<PllMock> seq(m, Route{2}, 9);
        if (at) {
            assert(seq.start(270000, 24000) == PllResult::Progress);
            for (unsigned n = 1; n < at; ++n) seq.step();
        }
        if (bad == 0) m.dssm = 0x20000000, m.nativeReference = 1;
        if (bad == 1) m.inputsOk = false;
        if (bad == 2) m.nativeReference = 1;
        if (bad == 3) m.vbt[0] = 0;
        if (bad == 4) m.inputsBusy = true;
        const auto writes = m.directCalls + m.indexedCalls;
        const auto result = at ? seq.step() : seq.start(270000, 24000);
        assert(result == (bad == 4 ? PllResult::Busy : PllResult::ClockInputsChanged));
        assert(m.directCalls + m.indexedCalls == writes && !m.inputsHeld);
        if (at) drain(seq); // cleanup works even with failed/busy inputs
        assert(!seq.needsCleanup() && m.control == 0x100);
    }
    std::cout << "PASS: DP PLL planning, power/program/lock ordering, bounded waits, partial-write unwind, ownership and epoch quarantine\n";
}
