#include "AdlpDklAccess.hpp"
#include <assert.h>
#include <iostream>
#include <string>
#include <vector>
using namespace YogaTypec;

struct DklMock {
    bool locked = false, lockOk = true, allowed = true;
    bool selectorOk = true, readOk = true, writeOk = true, afterReadOk = true;
    bool latch = true;
    uint32_t value = 0x12340008, index = UINT32_MAX, expectedAperture = 0x16a0a0;
    int apertureWrites = 0;
    std::vector<std::string> events;
    bool lock() {
        assert(!locked); events.emplace_back("lock"); locked = lockOk; return locked;
    }
    void unlock() { assert(locked); locked = false; events.emplace_back("unlock"); }
    bool authorized(Route) { assert(locked); events.emplace_back("authorized"); return allowed; }
    bool write32(uint32_t reg, uint32_t data) {
        assert(locked);
        if (reg == 0x1010a0) {
            events.emplace_back("selector"); index = data; return selectorOk;
        }
        assert(reg == expectedAperture && index != UINT32_MAX);
        events.emplace_back("write"); ++apertureWrites;
        if (latch) value = data;
        return writeOk;
    }
    bool read32(uint32_t reg, uint32_t &data) {
        assert(locked && reg == expectedAperture && index != UINT32_MAX);
        events.emplace_back("read"); data = value;
        return apertureWrites ? afterReadOk : readOk;
    }
};

int main() {
    const Route tc3{2};
    const uint32_t offsets[] = {0x14, 0x1014, 0xa0, 0x10a0, 0x2c0, 0x12c0,
        0x2c4, 0x12c4, 0x2c8, 0x12c8, 0x2f8, 0x12f8, 0xd00, 0x1d00,
        0xdc4, 0x1dc4, 0xdc8, 0x1dc8, 0x20d4, 0x20d8, 0x212c,
        0x2200, 0x2204, 0x2210, 0x2214, 0x2218, 0x236c};
    for (unsigned tc = 0; tc < 4; ++tc) {
        Route route{static_cast<uint8_t>(tc)};
        for (uint32_t off = 0; off < 0x10000; ++off) {
            bool reviewed = false;
            for (uint32_t allowed : offsets) if (off == allowed) reviewed = true;
            DklPlan plan{1, 2, 3};
            assert(planDklAccess(route, off, plan) == reviewed);
            if (!reviewed) {
                assert(plan.selectorRegister == 1 && plan.selectorValue == 2 && plan.apertureRegister == 3);
                continue;
            }
            assert(plan.selectorRegister == 0x1010a0);
            assert(plan.selectorValue == ((off / 0x1000) << (tc * 8)));
            assert(plan.apertureRegister == 0x168000 + tc * 0x1000 + off % 0x1000);
        }
    }
    DklPlan plan{};
    assert(planDklAccess(tc3, 0x10a0, plan));
    assert(plan.apertureRegister == 0x16a0a0 && plan.selectorValue == 0x10000);
    assert(planDklAccess(tc3, 0x2210, plan));
    assert(plan.apertureRegister == 0x16a210 && plan.selectorValue == 0x20000);
    for (uint32_t off : {0x168210u, UINT32_MAX, 0x4000u})
        assert(!planDklAccess(tc3, off, plan));
    assert(!planDklAccess(Route{4}, 0xa0, plan));
    {
        DklMock m; DklAccess<DklMock> access(m); uint32_t out = 0xdeadbeef;
        assert(access.read(tc3, 0xa0, out) == DklResult::Ok && out == m.value);
        assert(m.events == (std::vector<std::string>{"lock", "authorized", "selector", "read", "unlock"}));
        assert(access.update(tc3, 0xa0, 0xc0, 0x80) == DklResult::Ok);
        assert(m.value == 0x12340088 && !m.locked && m.apertureWrites == 1);
        assert(access.update(tc3, 0xa0, 0xc0, 0x80) == DklResult::Ok);
        assert(m.apertureWrites == 1); // no redundant aperture write
    }
    for (int fail = 0; fail < 8; ++fail) {
        DklMock m; DklAccess<DklMock> access(m);
        const DklResult expected[] = {DklResult::Busy, DklResult::Unauthorized,
            DklResult::SelectorFailed, DklResult::ReadFailed, DklResult::ReadFailed,
            DklResult::WriteUnverified, DklResult::WriteUnverified, DklResult::ReadbackMismatch};
        if (fail == 0) m.lockOk = false;
        if (fail == 1) m.allowed = false;
        if (fail == 2) m.selectorOk = false;
        if (fail == 3) m.readOk = false;
        if (fail == 4) m.value = UINT32_MAX;
        if (fail == 5) m.writeOk = false;
        if (fail == 6) m.afterReadOk = false;
        if (fail == 7) m.latch = false;
        assert(access.update(tc3, 0xa0, 0xc0, 0x80) == expected[fail]);
        assert(!m.locked);
        assert(m.apertureWrites == (fail >= 5 ? 1 : 0)); // no auto retry or rollback
        if (fail <= 1) assert(m.index == UINT32_MAX);
    }
    for (int fail = 0; fail < 4; ++fail) {
        DklMock m; DklAccess<DklMock> access(m); uint32_t out = 0xdeadbeef;
        if (fail == 0) m.allowed = false;
        if (fail == 1) m.selectorOk = false;
        if (fail == 2) m.readOk = false;
        if (fail == 3) m.value = UINT32_MAX;
        assert(access.read(tc3, 0xa0, out) != DklResult::Ok);
        assert(out == 0xdeadbeef && !m.locked && !m.apertureWrites);
    }
    for (uint32_t off : {0xa1u, 0x168210u, UINT32_MAX}) {
        DklMock m; DklAccess<DklMock> access(m); uint32_t out = 7;
        assert(access.read(tc3, off, out) == DklResult::InvalidRegister);
        assert(access.update(tc3, off, 0xc0, 0x80) == DklResult::InvalidRegister);
        assert(m.events.empty() && out == 7);
    }
    for (uint32_t mask : {0u, 0x100u, 0xc1u}) {
        DklMock m; DklAccess<DklMock> access(m);
        assert(access.update(tc3, 0xa0, mask, 0) == DklResult::InvalidMask && m.events.empty());
    }
    {
        DklMock m; DklAccess<DklMock> access(m);
        assert(access.update(tc3, 0xa0, 0x40, 0x80) == DklResult::InvalidMask);
        assert(access.update(tc3, 0x2200, 0xff, 1) == DklResult::InvalidMask);
        assert(m.events.empty()); // no unreviewed PLL programming
    }
    {
        DklMock m; m.expectedAperture = 0x16a014; m.value = 0x8800;
        DklAccess<DklMock> access(m);
        assert(access.update(tc3, 0x1014, 0x800, 0) == DklResult::Ok);
        assert(m.index == 0x10000 && m.value == 0x8000);
    }
    std::cout << "PASS: DKL canonical bank/aperture addressing, selector lock scope, fail-closed RMW and readback, PLL write refusal\n";
}
