#include "NativeDssmRead.hpp"
#include "NativeClockInputs.hpp"
#include <assert.h>
#include <iostream>
#include <vector>
using namespace YogaTypec;

static NativeMmioSnapshot fixture() {
    return {1, 2, 3, 4, 5, 0xffff800010000000ull, 0x6000000000ull, 0x1000000,
        0x6000000000ull, 0x1000000, 0xffff800010000000ull, 0xffff800010000000ull,
        0x1000000, 0x1000000, 9, true, true, true, false, false, true, true, true};
}
struct Mock {
    NativeMmioSnapshot first = fixture(), second = fixture();
    bool held = false, retained = false, busy = false, allowed = true;
    bool retainOk = true, snapshotOk = true, readOk = true, revoke = false;
    unsigned reads = 0, snapshots = 0, releases = 0;
    uint32_t value = 0;
    bool lockMapping() { assert(!held); if (busy) return false; held = true; return true; }
    void unlockMapping() { assert(held && !retained); held = false; }
    bool authorized(Route r, uint64_t e) { assert(held && r.tcIndex == 2 && e == 9); return allowed; }
    bool retainMapping(Route, uint64_t) { assert(held && !retained); retained = retainOk; return retained; }
    void releaseMapping() { assert(held && retained); retained = false; ++releases; }
    bool snapshotRetained(NativeMmioSnapshot &s) {
        assert(held && retained); s = snapshots++ ? second : first; return snapshotOk;
    }
    bool readRetained32(uint32_t offset, uint32_t &v) {
        assert(held && retained && offset == 0x51004 && validNativeDssmMapping(first, 9));
        ++reads; v = value; if (revoke) allowed = false; return readOk;
    }
};
static void corrupt(NativeMmioSnapshot &s, int bad) {
    switch (bad) {
        case 0: s.controller = 0; break; case 1: s.provider = 0; break;
        case 2: s.manager = 0; break; case 3: s.mapping = 0; break;
        case 4: s.descriptor = 0; break; case 5: s.virtualAddress = 0; break;
        case 6: s.physicalAddress = 0; break; case 7: s.mappingLength = 0; break;
        case 8: s.barAddress += 4096; break; case 9: s.barLength -= 4; break;
        case 10: s.controllerAddress += 4; break; case 11: s.managerAddress += 4; break;
        case 12: s.controllerLength -= 4; break; case 13: s.managerLength -= 4; break;
        case 14: s.epoch++; break; case 15: s.exactBuild = false; break;
        case 16: s.yogaIgpu = false; break; case 17: s.started = false; break;
        case 18: s.stopping = true; break; case 19: s.emulated = true; break;
        case 20: s.virtualAddress |= 1; break;
        case 21: s.mappingLength = s.barLength = uint64_t(UINT32_MAX) + 1; break;
        case 22: s.mappingLength = s.barLength = s.controllerLength = s.managerLength = 0x51004; break;
        case 23: s.virtualAddress = s.controllerAddress = s.managerAddress = UINT64_MAX - 3; break;
        case 24: s.physicalAddress = s.barAddress = UINT64_MAX - 3; break;
        case 25: s.kernelTask = false; break;
        case 26: s.mmioAttributesVerified = false; break;
        case 27: s.descriptorMatchesBar = false; break;
    }
}
int main() {
    assert(validNativeDssmMapping(fixture(), 9));
    for (int bad = 0; bad < 28; ++bad) for (bool afterRead : {false, true}) {
        Mock m; corrupt(afterRead ? m.second : m.first, bad);
        uint32_t out = 0x123;
        const auto result = readNativeDssmActualLocked(m, Route{2}, 9, out);
        assert(result == (afterRead ? DssmReadResult::Changed : DssmReadResult::InvalidMapping));
        assert(out == 0x123 && m.reads == unsigned(afterRead) && m.releases == 1);
        assert(!m.held && !m.retained);
    }
    // Individually valid replacement objects/addresses still do not authorize
    // the result of a read performed on the preceding mapping.
    for (int changed = 0; changed < 6; ++changed) {
        Mock m;
        if (changed == 0) ++m.second.mapping;
        if (changed == 1) ++m.second.descriptor;
        if (changed == 2) ++m.second.controller;
        if (changed == 3) ++m.second.provider;
        if (changed == 4) ++m.second.manager;
        if (changed == 5) m.second.virtualAddress = m.second.controllerAddress = m.second.managerAddress += 4096;
        uint32_t out = 0x123;
        assert(validNativeDssmMapping(m.second, 9));
        assert(readNativeDssmActualLocked(m, Route{2}, 9, out) == DssmReadResult::Changed);
        assert(out == 0x123 && m.releases == 1 && !m.held);
    }
    for (int bad = 0; bad < 7; ++bad) {
        Mock m;
        if (bad == 0) m.busy = true;
        if (bad == 1) m.allowed = false;
        if (bad == 2) m.retainOk = false;
        if (bad == 3) m.snapshotOk = false;
        if (bad == 4) m.readOk = false;
        if (bad == 5) m.value = UINT32_MAX;
        if (bad == 6) m.revoke = true;
        uint32_t out = 0x123;
        assert(readNativeDssmActualLocked(m, Route{2}, 9, out) != DssmReadResult::Ok);
        assert(out == 0x123 && !m.held && !m.retained);
        assert(m.releases == unsigned(bad >= 3));
    }
    for (uint32_t value : {0u, 0x20000000u, 0x40000000u}) {
        Mock m; m.value = value; uint32_t out = 123;
        assert(readNativeDssmActualLocked(m, Route{2}, 9, out) == DssmReadResult::Ok);
        assert(out == value && m.reads == 1 && m.releases == 1 && !m.held);
    }
    for (unsigned tc : {0u, 1u, 3u, 4u}) {
        Mock m; uint32_t out = 123;
        assert(readNativeDssmActualLocked(m, Route{uint8_t(tc)}, 9, out) == DssmReadResult::Unauthorized);
        assert(!m.held && !m.retained && !m.reads && out == 123);
    }
    // Compose actual guarded input parser with the mapping reader: this tests
    // lock order and reference balancing for BOTH DSSM reads, not a duplicate
    // parser/formula or a native-return-value substitute.
    struct Inputs : Mock {
        bool inputsHeld = false;
        uint32_t native = 0;
        std::vector<uint8_t> vbt;
        Inputs() : vbt(79, 0) {
            memcpy(vbt.data(), "$VBT", 4); vbt[20] = 100; vbt[22] = 48;
            vbt[24] = 79; vbt[28] = 48;
            memcpy(vbt.data() + 48, "BIOS_DATA_BLOCK ", 16);
            vbt[64] = 247; vbt[66] = 22; vbt[68] = 31; vbt[70] = 1; vbt[71] = 6;
            uint8_t sum = 0; for (auto byte : vbt) sum = uint8_t(sum + byte);
            vbt[26] = uint8_t(0 - sum);
        }
        bool lockInputs() { assert(!inputsHeld && !held); inputsHeld = true; return true; }
        void unlockInputs() { assert(inputsHeld && !held && !retained); inputsHeld = false; }
        bool lockMapping() { assert(inputsHeld); return Mock::lockMapping(); }
        bool authorized(Route r, uint64_t e) { assert(inputsHeld && r.tcIndex == 2 && e == 9); return allowed; }
        bool retainedVbt(Route, uint64_t, const uint8_t *&p, size_t &n) {
            assert(inputsHeld && !held); p = vbt.data(); n = vbt.size(); return true;
        }
        bool readDssmActual(uint32_t &v) {
            assert(inputsHeld); return readNativeDssmActualLocked(*this, Route{2}, 9, v) == DssmReadResult::Ok;
        }
        bool readNativeReferenceEncoding(uint32_t &v) { assert(inputsHeld && !held); v = native; return true; }
    };
    for (int bad = -1; bad < 28; ++bad) {
        Inputs m; if (bad >= 0) corrupt(m.first, bad);
        NativeClockInputs out{123, {123, true, 4}};
        const auto result = readNativeClockInputs(m, Route{2}, 9, out);
        if (bad < 0) {
            assert(result == ClockInputResult::Ok && out.referenceKHz == 24000);
            assert(!out.afc.overrideAfc && m.reads == 2 && m.releases == 2);
        } else {
            assert(result == ClockInputResult::InvalidRead && out.referenceKHz == 123);
            assert(!m.reads && m.releases == 1);
        }
        assert(!m.inputsHeld && !m.held && !m.retained);
    }
    std::cout << "PASS: retained native DSSM mapping, BAR/alias/bounds agreement, stop/remap rejection, output preservation and balanced references\n";
}
