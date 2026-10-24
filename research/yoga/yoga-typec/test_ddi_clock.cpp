#include "DdiClockSequence.hpp"
#include <assert.h>
#include <iostream>
#include <vector>
using namespace YogaTypec;

struct ClockMock {
    uint32_t sel = 0x123, gate = 0x55aa4001;
    bool held = false, busy = false, allowed = true, quiet = true, locked = true;
    int writes = 0, failWrite = -1, reads = 0, failRead = -1;
    bool partialFailure = true;
    std::vector<uint32_t> order;
    bool lockClock() { assert(!held); if (busy) return false; held = true; return true; }
    void unlockClock() { assert(held); held = false; }
    bool authorized(Route r, uint64_t e) { assert(held && r.tcIndex == 2 && e == 9); return allowed; }
    bool mappingQuiesced(Route, uint64_t) { assert(held); return quiet; }
    bool clockReady(Route, uint64_t) { assert(held); return locked; }
    bool readDirect(uint32_t a, uint32_t &v) {
        assert(held && (a == 0x46114 || a == 0x164280));
        v = a == 0x46114 ? sel : gate; return reads++ != failRead;
    }
    bool writeDirect(uint32_t a, uint32_t v) {
        assert(held && allowed && quiet); order.push_back(a);
        const bool ok = writes++ != failWrite;
        if (ok || partialFailure) {
            if (a == 0x46114) { assert((v & 0xfffffff) == 0x123); sel = v; }
            else { assert(a == 0x164280 && (v & ~0x4000u) == (gate & ~0x4000u)); gate = v; }
        }
        return ok;
    }
};
using Map = DdiClockSequence<ClockMock>;
static void up(Map &s) {
    assert(s.start() == ClockMapResult::Progress);
    assert(s.step() == ClockMapResult::Progress);
    assert(s.step() == ClockMapResult::Ready);
}
static void down(Map &s) {
    auto r = s.stop();
    for (int i = 0; i < 5 && r != ClockMapResult::Complete; ++i) r = s.stop();
    assert(r == ClockMapResult::Complete && !s.needsCleanup());
}
int main() {
    {
        ClockMock m; Map s(m, Route{2}, 9); up(s);
        assert(m.sel == 0x80000123 && !(m.gate & 0x4000));
        assert(s.step() == ClockMapResult::Ready && s.needsCleanup());
        down(s); assert(m.sel == 0x123 && m.gate == 0x55aa4001);
        assert(m.order == (std::vector<uint32_t>{0x46114,0x164280,0x164280,0x46114}));
    }
    // Every write, including teardown, can fail before OR after mutation.
    for (int fail = 0; fail < 4; ++fail) for (bool partial : {false,true}) {
        ClockMock m; m.failWrite = fail; m.partialFailure = partial; Map s(m, Route{2}, 9);
        assert(s.start() == ClockMapResult::Progress);
        if (fail == 0) assert(s.step() == ClockMapResult::WriteUnverified);
        else {
            assert(s.step() == ClockMapResult::Progress);
            if (fail == 1) assert(s.step() == ClockMapResult::WriteUnverified);
            else {
                assert(s.step() == ClockMapResult::Ready);
                if (fail == 2) assert(s.stop() == ClockMapResult::WriteUnverified);
                else { assert(s.stop() == ClockMapResult::Progress); assert(s.step() == ClockMapResult::WriteUnverified); }
            }
        }
        assert(s.needsCleanup()); down(s); assert(m.sel == 0x123 && (m.gate & 0x4000));
    }
    for (int read = 0; read < 14; ++read) {
        ClockMock m; m.failRead = read; Map s(m, Route{2}, 9);
        const auto r = s.start();
        if (r == ClockMapResult::Progress) { s.step(); s.step(); }
        m.failRead = -1; down(s);
        assert(m.sel == 0x123 && (m.gate & 0x4000));
    }
    for (int which = 0; which < 4; ++which) {
        ClockMock m;
        if (which == 0) m.sel |= 0x80000000;
        if (which == 1) m.gate &= ~0x4000u;
        if (which == 2) m.sel |= 0xc0000000;
        if (which == 3) m.sel = UINT32_MAX;
        Map s(m, Route{2}, 9);
        const auto r = s.start();
        assert(r == ClockMapResult::ExistingOwner || r == ClockMapResult::ReadFailed);
        assert(!m.writes && !s.needsCleanup());
    }
    {
        ClockMock m; Map s(m, Route{2}, 9); up(s);
        m.quiet = false; const auto writes = m.writes;
        assert(s.stop() == ClockMapResult::UsersActive && m.writes == writes);
        m.quiet = true; m.locked = false; down(s); // cleanup permits lost PLL lock
    }
    {
        ClockMock m; Map s(m, Route{2}, 9); up(s);
        m.allowed = false; const auto writes = m.writes;
        assert(s.stop() == ClockMapResult::Quarantined);
        m.allowed = true; assert(s.stop() == ClockMapResult::Quarantined);
        assert(m.writes == writes && s.needsCleanup());
    }
    for (auto boundary : {ClockMapState::Select, ClockMapState::Ungate, ClockMapState::Ready}) {
        ClockMock m; Map s(m, Route{2}, 9); s.start();
        if (boundary != ClockMapState::Select) s.step();
        if (boundary == ClockMapState::Ready) s.step();
        m.locked = false; const auto writes = m.writes;
        assert(s.step() == ClockMapResult::ClockLost && m.writes == writes); down(s);
    }
    {
        ClockMock m; Map s(m, Route{2}, 9); up(s);
        m.sel = 0xd0000123; const auto writes = m.writes;
        assert(s.stop() == ClockMapResult::Quarantined && m.writes == writes);
    }
    {
        ClockMock m; Map s(m, Route{2}, 9); up(s);
        m.gate ^= 1u << 15; // sibling TC4 changes; TC3 teardown must preserve it
        const auto sibling = m.gate & (1u << 15); down(s);
        assert((m.gate & (1u << 15)) == sibling);
    }
    std::cout << "PASS: DDI select/ungate and gate/clear ordering, shared-bit preservation, ownership refusal, partial-write cleanup and epoch quarantine\n";
}
