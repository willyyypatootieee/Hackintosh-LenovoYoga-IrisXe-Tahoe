#include "NativeAuxLease.hpp"
#include <assert.h>
#include <iostream>
using namespace YogaTypec;

struct AuxMock {
    uint32_t refs = 0, ctl = 0;
    int enables = 0, disables = 0;
    bool valid = true, acknowledge = true, clearRequest = true;
    bool countEnable = true, countDisable = true, loseAfterMutation = false;
    AuxSnapshot snapshot() { return {valid, refs, ctl}; }
    void nativeEnable() {
        ++enables;
        if (countEnable) ++refs;
        ctl |= 1u << 11;
        if (acknowledge) ctl |= 1u << 10;
        if (loseAfterMutation) valid = false;
    }
    void nativeDisable() {
        ++disables;
        if (countDisable) --refs;
        if (!refs && clearRequest) ctl &= ~(1u << 11);
        // State may remain on due to another agent's request. Not our resource.
        if (loseAfterMutation) valid = false;
    }
};

int main() {
    for (uint32_t existing : {0u, 1u, 7u, 1024u}) {
        AuxMock m; m.refs = existing;
        if (existing) m.ctl = 0xc00;
        m.ctl |= 0x80000001; // unrelated state must survive
        NativeAuxLease<AuxMock> lease(m);
        for (unsigned cycle = 0; cycle < 50; ++cycle) {
            assert(lease.acquire() == AuxLeaseResult::Ok && lease.ready());
            assert(m.refs == existing + 1);
            assert(lease.acquire() == AuxLeaseResult::Busy);
            assert(lease.release() == AuxLeaseResult::Ok);
            assert(!lease.needsCleanup() && m.refs == existing);
            assert((m.ctl & 0x80000001) == 0x80000001);
            assert(lease.release() == AuxLeaseResult::Ok);
        }
        assert(m.enables == 50 && m.disables == 50);
    }
    {
        AuxMock m; m.acknowledge = false;
        NativeAuxLease<AuxMock> lease(m);
        assert(lease.acquire() == AuxLeaseResult::Unverified);
        assert(!lease.ready() && lease.needsCleanup() && m.refs == 1);
        assert(lease.verify() == AuxLeaseResult::Unverified && m.enables == 1);
        m.ctl |= 1u << 10;
        assert(lease.verify() == AuxLeaseResult::Ok);
        m.ctl = UINT32_MAX;
        assert(lease.verify() == AuxLeaseResult::Unverified && !lease.ready());
        m.ctl = 0xc00;
        assert(lease.release() == AuxLeaseResult::Ok && m.refs == 0);
    }
    {
        AuxMock m; m.refs = 4; m.ctl = 0xc00; m.acknowledge = false;
        m.ctl &= ~(1u << 10); // preexisting users do NOT prove hardware is on
        NativeAuxLease<AuxMock> lease(m);
        assert(lease.acquire() == AuxLeaseResult::Unverified);
        assert(lease.release() == AuxLeaseResult::Ok && m.refs == 4);
    }
    {
        AuxMock m; NativeAuxLease<AuxMock> lease(m);
        assert(lease.acquire() == AuxLeaseResult::Ok);
        m.clearRequest = false;
        assert(lease.release() == AuxLeaseResult::Unverified && m.refs == 0);
        assert(lease.currentState() == AuxLeaseState::ReleasePending);
        for (int i = 0; i < 20; ++i)
            assert(lease.release() == AuxLeaseResult::Unverified);
        assert(m.disables == 1); // never decrement twice after failed readback
        m.ctl = UINT32_MAX;
        assert(lease.verify() == AuxLeaseResult::Unverified);
        m.ctl = 1u << 10; // another agent still has global power on
        assert(lease.release() == AuxLeaseResult::Ok && !lease.needsCleanup());
        assert(m.disables == 1);
    }
    for (int which = 0; which < 4; ++which) {
        AuxMock m; NativeAuxLease<AuxMock> lease(m);
        if (which < 2) {
            m.countEnable = which != 0; m.loseAfterMutation = which == 1;
            assert(lease.acquire() == AuxLeaseResult::AccountingMismatch);
        } else {
            assert(lease.acquire() == AuxLeaseResult::Ok);
            m.countDisable = which != 2; m.loseAfterMutation = which == 3;
            assert(lease.release() == AuxLeaseResult::AccountingMismatch);
        }
        assert(lease.currentState() == AuxLeaseState::Quarantined);
        const int enables = m.enables, disables = m.disables;
        m.valid = true;
        assert(lease.release() == AuxLeaseResult::AccountingMismatch);
        assert(lease.verify() == AuxLeaseResult::AccountingMismatch);
        assert(lease.acquire() == AuxLeaseResult::Busy);
        assert(m.enables == enables && m.disables == disables);
    }
    for (int which = 0; which < 4; ++which) {
        AuxMock m; NativeAuxLease<AuxMock> lease(m);
        if (which == 0) m.valid = false;
        if (which == 1) m.refs = UINT32_MAX;
        if (which == 2) m.ctl = UINT32_MAX;
        if (which == 3) m.refs = UINT32_MAX - 1;
        assert(lease.acquire() == AuxLeaseResult::InvalidSnapshot);
        assert(!lease.needsCleanup() && !m.enables && !m.disables);
    }
    {
        AuxMock m; NativeAuxLease<AuxMock> lease(m);
        assert(lease.acquire() == AuxLeaseResult::Ok);
        m.valid = false;
        assert(lease.release() == AuxLeaseResult::InvalidSnapshot && !m.disables);
        m.valid = true;
        assert(lease.release() == AuxLeaseResult::Ok);
    }
    {
        AuxMock m; NativeAuxLease<AuxMock> lease(m);
        assert(lease.acquire() == AuxLeaseResult::Ok);
        m.refs += 3; // unrelated native users arrive during the link lifetime
        assert(lease.release() == AuxLeaseResult::Ok && m.refs == 3);
        assert(m.disables == 1 && !lease.needsCleanup());
    }
    {
        AuxMock m; NativeAuxLease<AuxMock> lease(m);
        assert(lease.acquire() == AuxLeaseResult::Ok);
        m.refs = 0;
        assert(lease.verify() == AuxLeaseResult::AccountingMismatch);
        assert(!m.disables);
    }
    std::cout << "PASS: verified native AUX lease, preexisting refs, timeout retention, single decrement, quarantine and repeated lifetime\n";
}
