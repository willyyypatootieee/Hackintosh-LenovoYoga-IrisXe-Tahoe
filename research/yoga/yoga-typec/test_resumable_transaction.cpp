#include "ResumableTypecTransaction.hpp"
#include "NativeAuxLease.hpp"
#include <assert.h>
#include <iostream>
#include <string>
#include <vector>
using namespace YogaTypec;

struct ResumableMock {
    int tokens[4] = {}; // core, port, owner, cold
    Acquisition acquire[4] = {Acquisition::Verified, Acquisition::Verified,
                              Acquisition::Verified, Acquisition::Verified};
    Cleanup cleanup[4] = {};
    bool hardwareReady = true, usersStopped = true;
    Mode initial = Mode::DpAlt, later = Mode::DpAlt;
    uint32_t mask = 15;
    int modeReads = 0;
    std::vector<std::string> events;
    Acquisition get(int index) {
        if (index) assert(tokens[0]);
        if (index == 2) assert(tokens[1]);
        assert(!tokens[index]);
        events.push_back("+" + std::to_string(index));
        if (acquire[index] != Acquisition::Unavailable) ++tokens[index];
        return acquire[index];
    }
    Cleanup put(int index) {
        assert(tokens[index] == 1);
        // Prerequisites cannot disappear before their dependent token is gone.
        if (index < 3) assert(!tokens[3]);
        if (index < 2) assert(!tokens[2]);
        if (index == 0) assert(!tokens[1]);
        events.push_back("-" + std::to_string(index));
        if (cleanup[index] == Cleanup::Complete) --tokens[index];
        return cleanup[index];
    }
    Acquisition acquireCore() { return get(0); }
    Acquisition acquirePort(Route) { return get(1); }
    Acquisition takeOwnership(Route) { return get(2); }
    Acquisition blockCold(Route, Mode m) {
        if (m == Mode::DpAlt) assert(tokens[1] && tokens[2]);
        else assert(m == Mode::TbtAlt && !tokens[1] && !tokens[2]);
        return get(3);
    }
    Cleanup releaseCore() { return put(0); }
    Cleanup releasePort(Route) { return put(1); }
    Cleanup dropOwnership(Route) { return put(2); }
    Cleanup unblockCold(Route, Mode) { return put(3); }
    Mode readMode(Route) { assert(tokens[0]); return modeReads++ ? later : initial; }
    bool ready(Route) { assert(tokens[0] && tokens[1] && tokens[2]); return hardwareReady; }
    uint32_t readLaneMask(Route) {
        for (int token : tokens) assert(token);
        return mask;
    }
    bool quiesced(Route) { return usersStopped; }
    void empty() { for (int token : tokens) assert(!token); }
};

struct NativePowerMock {
    uint32_t refs = 3, ctl = 0xc00;
    int disables = 0;
    bool clearOnRelease = true;
    AuxSnapshot snapshot() { return {true, refs, ctl}; }
    void nativeEnable() { ++refs; ctl |= 0xc00; }
    void nativeDisable() {
        ++disables; --refs;
        // Existing users may leave: the test sets refs=1 before disconnect.
        if (!refs && clearOnRelease) ctl &= ~0x800u;
    }
};

struct LeaseBackend : ResumableMock {
    NativePowerMock power;
    NativeAuxLease<NativePowerMock> lease;
    LeaseBackend() : lease(power) {}
    Acquisition blockCold(Route, Mode m) {
        assert(m == Mode::DpAlt && tokens[0] && tokens[1] && tokens[2]);
        const auto result = lease.acquire();
        if (lease.needsCleanup()) tokens[3] = 1;
        if (result == AuxLeaseResult::Ok) return Acquisition::Verified;
        return lease.needsCleanup() ? Acquisition::CleanupRequired : Acquisition::Unavailable;
    }
    Cleanup unblockCold(Route, Mode m) {
        assert(m == Mode::DpAlt && tokens[0] && tokens[1] && tokens[2]);
        const auto result = lease.release();
        if (result == AuxLeaseResult::Ok) { tokens[3] = 0; return Cleanup::Complete; }
        return result == AuxLeaseResult::AccountingMismatch ? Cleanup::Quarantined : Cleanup::Retry;
    }
};

int main() {
    const Route tc3{2};
    {
        ResumableMock m; ResumableTransaction<ResumableMock> tx(m, tc3);
        assert(tx.connect(4) == LinkResult::Ok && tx.ready());
        assert(tx.mode() == Mode::DpAlt && tx.needsCleanup());
        for (int token : m.tokens) assert(token == 1);
        assert(tx.connect(4) == LinkResult::Busy);
        m.usersStopped = false;
        assert(tx.disconnect() == LinkResult::UsersActive && tx.ready());
        assert(m.events.size() == 4);
        m.usersStopped = true;
        assert(tx.disconnect() == LinkResult::Ok && !tx.ready()); m.empty();
        assert(m.events == (std::vector<std::string>{"+0", "+1", "+2", "+3", "-3", "-2", "-1", "-0"}));
    }
    // Failed acquisitions may retain a partial native reference/ownership.
    for (int index = 0; index < 4; ++index) {
        for (Acquisition fail : {Acquisition::Unavailable, Acquisition::CleanupRequired}) {
            ResumableMock m; m.acquire[index] = fail;
            ResumableTransaction<ResumableMock> tx(m, tc3);
            const LinkResult expected[] = {LinkResult::PowerUnavailable, LinkResult::PowerUnavailable,
                                           LinkResult::OwnershipFailed, LinkResult::ColdBlockFailed};
            assert(tx.connect(4) == expected[index]);
            assert(tx.failureCause() == expected[index]);
            assert(!tx.ready() && !tx.needsCleanup()); m.empty();
        }
    }
    // Retry/quarantine at every reverse cleanup step, both active disconnect
    // and failed acquisition. No dependent power is released past that step.
    for (bool aborted : {false, true}) for (int index = 0; index < 4; ++index) {
        for (Cleanup fail : {Cleanup::Retry, Cleanup::Quarantined}) {
            ResumableMock m; m.cleanup[index] = fail;
            if (aborted) m.later = Mode::Disconnected;
            ResumableTransaction<ResumableMock> tx(m, tc3);
            if (!aborted) assert(tx.connect(4) == LinkResult::Ok);
            const LinkResult expected = fail == Cleanup::Retry ? LinkResult::CleanupPending : LinkResult::Quarantined;
            assert((aborted ? tx.connect(4) : tx.disconnect()) == expected);
            assert(!tx.ready() && tx.mode() == Mode::Disconnected && tx.needsCleanup());
            for (int i = 0; i <= index; ++i) assert(m.tokens[i]);
            for (int i = index + 1; i < 4; ++i) assert(!m.tokens[i]);
            if (aborted) assert(tx.failureCause() == LinkResult::ModeChanged);
            if (fail == Cleanup::Quarantined) {
                const auto count = m.events.size();
                m.cleanup[index] = Cleanup::Complete;
                assert(tx.disconnect() == LinkResult::Quarantined);
                assert(tx.connect(4) == LinkResult::Quarantined);
                assert(count == m.events.size()); // external reconciliation required
            } else {
                assert(tx.connect(4) == LinkResult::Busy);
                assert(tx.disconnect() == LinkResult::CleanupPending);
                m.cleanup[index] = Cleanup::Complete;
                assert(tx.disconnect() == LinkResult::Ok && !tx.needsCleanup()); m.empty();
            }
        }
    }
    for (int bad = 0; bad < 6; ++bad) {
        ResumableMock m;
        const LinkResult expected[] = {LinkResult::NoSink, LinkResult::Ambiguous,
            LinkResult::NotReady, LinkResult::ModeChanged, LinkResult::InvalidLaneMask,
            LinkResult::InsufficientLanes};
        if (bad == 0) m.initial = Mode::Disconnected;
        if (bad == 1) m.initial = Mode::Ambiguous;
        if (bad == 2) m.hardwareReady = false;
        if (bad == 3) m.later = Mode::TbtAlt;
        if (bad == 4) m.mask = UINT32_MAX;
        if (bad == 5) m.mask = 3;
        ResumableTransaction<ResumableMock> tx(m, tc3);
        assert(tx.connect(4) == expected[bad]); m.empty();
    }
    {
        ResumableMock m; m.initial = m.later = Mode::TbtAlt;
        ResumableTransaction<ResumableMock> tx(m, tc3);
        for (int cycle = 0; cycle < 50; ++cycle) {
            assert(tx.connect(4) == LinkResult::Ok && tx.mode() == Mode::TbtAlt);
            assert(!m.tokens[1] && !m.tokens[2]);
            assert(tx.disconnect() == LinkResult::Ok); m.empty();
        }
    }
    for (unsigned lanes : {0u, 3u, 8u}) {
        ResumableMock m; ResumableTransaction<ResumableMock> tx(m, tc3);
        assert(tx.connect(lanes) == LinkResult::InvalidLanes && m.events.empty());
    }
    {
        // Compose the two REAL local components, not just generic mock tokens.
        LeaseBackend m; ResumableTransaction<LeaseBackend> tx(m, tc3);
        assert(tx.connect(4) == LinkResult::Ok && m.power.refs == 4);
        m.power.refs = 1; // the three preexisting native users have now left
        m.power.clearOnRelease = false;
        assert(tx.disconnect() == LinkResult::CleanupPending);
        assert(m.power.refs == 0 && m.power.disables == 1);
        for (int token : m.tokens) assert(token); // dependencies stay powered
        for (int retry = 0; retry < 10; ++retry)
            assert(tx.disconnect() == LinkResult::CleanupPending);
        assert(m.power.disables == 1);
        m.power.ctl = 0x400; // driver request cleared; global well may stay on
        assert(tx.disconnect() == LinkResult::Ok);
        assert(m.power.disables == 1 && !m.lease.needsCleanup()); m.empty();
    }
    {
        ResumableMock m; ResumableTransaction<ResumableMock> tx(m, Route{4});
        assert(tx.connect(4) == LinkResult::InvalidLanes && m.events.empty());
    }
    std::cout << "PASS: fallible transaction, partial holds, reverse cleanup retry/quarantine, retained prerequisites and quiescence\n";
}
