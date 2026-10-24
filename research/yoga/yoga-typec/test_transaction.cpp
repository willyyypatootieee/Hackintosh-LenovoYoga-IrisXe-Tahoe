#include "AdlpTypecTransaction.hpp"
#include <assert.h>
#include <string>
#include <vector>
#include <iostream>
using namespace YogaTypec;

struct Mock {
    std::vector<std::string> events;
    int core = 0, port = 0, cold = 0, owner = 0, modeReads = 0;
    bool coreOk = true, portOk = true, ownershipOk = true, readyOk = true, coldOk = true;
    Mode initial = Mode::DpAlt, later = Mode::DpAlt;
    uint32_t mask = 15;
    void event(const char *s) { events.emplace_back(s); }
    bool acquireCore() { event("core+"); if (coreOk) ++core; return coreOk; }
    void releaseCore() { assert(core == 1); --core; event("core-"); }
    bool acquirePort(Route) { event("port+"); if (portOk) ++port; return portOk; }
    void releasePort(Route) { assert(port == 1); --port; event("port-"); }
    Mode readMode(Route) { assert(core); event("hpd"); return modeReads++ ? later : initial; }
    bool setOwnership(Route, bool on) {
        assert(port); event(on ? "owner+" : "owner-");
        if (on && !ownershipOk) return false;
        assert(on ? !owner : owner == 1); owner = on; return true;
    }
    bool ready(Route) { assert(core && port && owner); event("ready"); return readyOk; }
    bool blockCold(Route, Mode m) {
        assert(core && (m == Mode::TbtAlt || (port && owner)));
        event(m == Mode::TbtAlt ? "tbt-cold+" : "aux-cold+");
        if (coldOk) ++cold;
        return coldOk;
    }
    void unblockCold(Route, Mode m) {
        assert((port || (core && m == Mode::TbtAlt)) && cold == 1); --cold;
        event(m == Mode::TbtAlt ? "tbt-cold-" : "aux-cold-");
    }
    uint32_t readLaneMask(Route r) {
        assert(core && port && owner && cold);
        assert(r.fiaInstance() == 1 && r.fiaLocalIndex() == 0);
        event("lanes"); return mask;
    }
    void empty() { assert(!core && !port && !cold && !owner); }
};

int main() {
    const Route tc3{2};
    assert(tc3.fiaInstance() == 1 && tc3.fiaLocalIndex() == 0);
    for (unsigned i = 0; i < 4; ++i) {
        Route r{static_cast<uint8_t>(i)};
        assert(r.fiaInstance() == i / 2 && r.fiaLocalIndex() == i % 2);
        for (unsigned mask = 0; mask < 16; ++mask) {
            const unsigned want = mask == 15 ? 4 : mask == 3 || mask == 12 ? 2 :
                mask == 1 || mask == 2 || mask == 4 || mask == 8 ? 1 : 0;
            assert(laneCount(mask << r.laneShift(), r) == want);
        }
    }
    assert(!laneCount(UINT32_MAX, tc3));
    assert(!laneCount(15, Route{4}));
    const uint32_t dp = 1u << 18, tbt = 1u << 2, legacy = 1u << 26;
    assert(liveMode(dp, 0, dp, tbt, legacy) == Mode::DpAlt);
    assert(liveMode(tbt, 0, dp, tbt, legacy) == Mode::TbtAlt);
    assert(liveMode(0, 0, dp, tbt, legacy) == Mode::Disconnected);
    assert(liveMode(dp | tbt, 0, dp, tbt, legacy) == Mode::Ambiguous);
    assert(liveMode(0, legacy, dp, tbt, legacy) == Mode::Ambiguous);
    assert(liveMode(UINT32_MAX, 0, dp, tbt, legacy) == Mode::Ambiguous);
    assert(liveMode(0, UINT32_MAX, dp, tbt, legacy) == Mode::Ambiguous);
    {
        Mock m; Transaction<Mock> tx(m, tc3);
        assert(tx.connect(4) == Result::Ok);
        assert(m.events == (std::vector<std::string>{"core+", "hpd", "port+", "owner+", "ready", "aux-cold+", "hpd", "lanes", "port-", "core-"}));
        assert(tx.active() && m.cold == 1 && m.owner == 1 && !m.core && !m.port);
        auto n = m.events.size(); assert(tx.connect(2) == Result::Busy); assert(m.events.size() == n);
        m.portOk = false; assert(tx.disconnect() == Result::PowerUnavailable);
        assert(tx.active() && m.cold && m.owner);
        m.portOk = true; assert(tx.disconnect() == Result::Ok); m.empty();
        assert(!tx.active());
        n = m.events.size(); assert(tx.disconnect() == Result::Ok); assert(m.events.size() == n);
        assert(std::vector<std::string>(m.events.end()-4, m.events.end()) ==
               (std::vector<std::string>{"port+", "aux-cold-", "owner-", "port-"}));
    }
    // Every acquisition/verification failure must unwind exactly what it owns.
    for (int fail = 0; fail < 10; ++fail) {
        Mock m;
        const Result expected[] = {Result::PowerUnavailable, Result::NoSink, Result::Ambiguous,
            Result::PowerUnavailable, Result::OwnershipFailed, Result::NotReady,
            Result::ColdBlockFailed, Result::ModeChanged, Result::InvalidLaneMask,
            Result::InsufficientLanes};
        switch (fail) {
            case 0: m.coreOk = false; break;
            case 1: m.initial = Mode::Disconnected; break;
            case 2: m.initial = Mode::Ambiguous; break;
            case 3: m.portOk = false; break;
            case 4: m.ownershipOk = false; break;
            case 5: m.readyOk = false; break;
            case 6: m.coldOk = false; break;
            case 7: m.later = Mode::Disconnected; break;
            case 8: m.mask = UINT32_MAX; break;
            case 9: m.mask = 3; break;
        }
        Transaction<Mock> tx(m, tc3);
        assert(tx.connect(4) == expected[fail]); assert(!tx.active()); m.empty();
        if (fail >= 7) assert(std::vector<std::string>(m.events.end()-4, m.events.end()) ==
            (std::vector<std::string>{"aux-cold-", "owner-", "port-", "core-"}));
    }
    for (bool modeChange : {false, true}) {
        Mock m; m.initial = Mode::TbtAlt; m.later = modeChange ? Mode::DpAlt : Mode::TbtAlt;
        Transaction<Mock> tx(m, tc3);
        if (!modeChange) {
            assert(tx.connect(4) == Result::Ok); assert(!m.owner && m.cold);
            assert(m.events == (std::vector<std::string>{"core+", "hpd", "tbt-cold+", "hpd", "core-"}));
            assert(tx.disconnect() == Result::Ok); m.empty();
        } else {
            assert(tx.connect(4) == Result::ModeChanged); m.empty();
            assert(m.events == (std::vector<std::string>{"core+", "hpd", "tbt-cold+", "hpd", "tbt-cold-", "core-"}));
        }
    }
    { Mock m; m.initial = Mode::TbtAlt; m.coldOk = false;
      Transaction<Mock> tx(m, tc3); assert(tx.connect(4) == Result::ColdBlockFailed); m.empty(); }
    for (unsigned lanes : {0u, 3u, 8u}) {
        Mock m; Transaction<Mock> tx(m, tc3);
        assert(tx.connect(lanes) == Result::InvalidLanes); assert(m.events.empty());
    }
    { Mock m; Transaction<Mock> tx(m, Route{4}); assert(tx.connect(4) == Result::InvalidLanes); assert(m.events.empty()); }
    // All valid lane requests and both orientations; repeated hotplug cycles.
    for (uint32_t mask : {1u, 2u, 4u, 8u, 3u, 12u, 15u}) {
        for (unsigned lanes : {1u, 2u, 4u}) {
            Mock m; m.mask = mask; Transaction<Mock> tx(m, tc3);
            for (int cycle = 0; cycle < 20; ++cycle) {
                assert(tx.connect(lanes) == (laneCount(mask, tc3) >= lanes ? Result::Ok : Result::InsufficientLanes));
                assert(tx.disconnect() == Result::Ok); m.empty();
            }
        }
    }
    std::cout << "PASS: route/lane decode, power/ownership ordering, failure unwind, TBT isolation, detach retry, repeated hotplug\n";
}
