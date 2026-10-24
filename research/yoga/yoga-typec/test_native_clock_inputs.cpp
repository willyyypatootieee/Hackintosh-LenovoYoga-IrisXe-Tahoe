#include "NativeClockInputs.hpp"
#include <assert.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
using namespace YogaTypec;

static void le16(std::vector<uint8_t> &v, size_t at, unsigned n) {
    v[at] = uint8_t(n); v[at + 1] = uint8_t(n >> 8);
}
static void checksum(std::vector<uint8_t> &v) {
    v[26] = 0;
    uint8_t sum = 0; for (auto byte : v) sum = uint8_t(sum + byte);
    v[26] = uint8_t(0 - sum);
}
static std::vector<uint8_t> fixture(unsigned version, unsigned config) {
    std::vector<uint8_t> v(80, 0);
    memcpy(v.data(), "$VBT", 4); le16(v, 20, 100); le16(v, 22, 48);
    le16(v, 24, unsigned(v.size())); v[28] = 48;
    memcpy(v.data() + 48, "BIOS_DATA_BLOCK ", 16);
    le16(v, 64, version); le16(v, 66, 22); le16(v, 68, 32);
    v[70] = 1; le16(v, 71, 7); v[79] = uint8_t(config);
    checksum(v); return v;
}
struct InputMock {
    std::vector<uint8_t> vbt = fixture(249, 2);
    bool held = false, busy = false, allowed = true, vbtOk = true, nativeOk = true;
    uint32_t dssm[2] = {0x40000123, 0x40000123}, native = 2;
    int reads = 0, failRead = -1;
    bool revokeAfterRead = false;
    bool lockInputs() { assert(!held); if (busy) return false; held = true; return true; }
    void unlockInputs() { assert(held); held = false; }
    bool authorized(Route r, uint64_t e) { assert(held && r.tcIndex == 2 && e == 9); return allowed; }
    bool retainedVbt(Route, uint64_t, const uint8_t *&p, size_t &n) {
        assert(held && allowed); p = vbt.data(); n = vbt.size(); return vbtOk;
    }
    bool readDssmActual(uint32_t &v) {
        assert(held && allowed && reads < 2); v = dssm[reads];
        const bool ok = reads++ != failRead;
        if (revokeAfterRead && reads == 2) allowed = false;
        return ok;
    }
    bool readNativeReferenceEncoding(uint32_t &v) { assert(held); v = native; return nativeOk; }
};

int main(int argc, char **argv) {
    if (argc == 2) {
        std::ifstream file(argv[1], std::ios::binary); assert(file.good());
        std::vector<uint8_t> actual((std::istreambuf_iterator<char>(file)), {});
        VbtAfcPolicy policy{};
        assert(parseVbtAfcPolicy(actual.data(), actual.size(), policy));
        assert(policy.bdbVersion == 247 && !policy.overrideAfc && policy.startup == 0);
        std::cout << "PASS: captured Yoga VBT BDB247 requires no AFC override\n"; return 0;
    }
    for (unsigned version : {155u, 247u, 248u, 249u, 250u})
    for (unsigned config = 0; config < 256; ++config) {
        auto v = fixture(version, config); VbtAfcPolicy p{};
        assert(parseVbtAfcPolicy(v.data(), v.size(), p));
        const auto expected = version >= 249 ? config & 3 : 0;
        assert(p.bdbVersion == version && p.overrideAfc == (expected != 0));
        assert(p.startup == (expected == 1 ? 0 : expected ? 7 : 0));
    }
    auto v = fixture(249, 2);
    for (size_t size = 0; size < v.size(); ++size) {
        VbtAfcPolicy p{123, true, 4};
        assert(!parseVbtAfcPolicy(v.data(), size, p));
        assert(p.bdbVersion == 123 && p.startup == 4);
    }
    for (size_t at : {size_t(0), size_t(22), size_t(24), size_t(28), size_t(48),
                      size_t(66), size_t(68), size_t(70), size_t(71)}) {
        auto bad = v; bad[at] = 0xff; checksum(bad);
        VbtAfcPolicy p{}; assert(!parseVbtAfcPolicy(bad.data(), bad.size(), p));
    }
    {
        auto bad = v; bad[30] = 0xff; checksum(bad);
        VbtAfcPolicy p{}; assert(!parseVbtAfcPolicy(bad.data(), bad.size(), p));
        bad = v; bad[79] ^= 1;
        assert(!parseVbtAfcPolicy(bad.data(), bad.size(), p));
        bad = v; le16(bad, 71, 6); checksum(bad);
        assert(!parseVbtAfcPolicy(bad.data(), bad.size(), p));
    }
    for (unsigned code = 0; code < 8; ++code) {
        uint32_t kHz = 123;
        const bool ok = decodeDssmReference((code << 29) | 0x1fffffff, kHz);
        assert(ok == (code < 3));
        if (ok) assert(kHz == (code == 0 ? 24000u : code == 1 ? 19200u : 38400u));
        else assert(kHz == 123);
    }
    {
        uint32_t kHz = 123; assert(!decodeDssmReference(UINT32_MAX, kHz) && kHz == 123);
    }
    for (int bad = 0; bad < 11; ++bad) {
        InputMock m;
        if (bad == 0) m.busy = true;
        if (bad == 1) m.allowed = false;
        if (bad == 2) m.vbtOk = false;
        if (bad == 3) m.vbt[0] = 0;
        if (bad == 4) m.failRead = 0;
        if (bad == 5) m.failRead = 1;
        if (bad == 6) m.nativeOk = false;
        if (bad == 7) m.dssm[0] = UINT32_MAX;
        if (bad == 8) m.dssm[1] = 0;
        if (bad == 9) m.native = 0;
        if (bad == 10) m.native = 3;
        NativeClockInputs out{123, {123, false, 4}};
        assert(readNativeClockInputs(m, Route{2}, 9, out) != ClockInputResult::Ok);
        assert(!m.held && out.referenceKHz == 123 && out.afc.startup == 4);
    }
    {
        InputMock m; NativeClockInputs out{};
        assert(readNativeClockInputs(m, Route{2}, 9, out) == ClockInputResult::Ok);
        assert(out.referenceKHz == 38400 && out.afc.overrideAfc && out.afc.startup == 7 && !m.held);
    }
    {
        InputMock m; m.revokeAfterRead = true;
        NativeClockInputs out{123, {123, false, 4}};
        assert(readNativeClockInputs(m, Route{2}, 9, out) == ClockInputResult::Unauthorized);
        assert(!m.held && out.referenceKHz == 123 && out.afc.startup == 4);
    }
    {
        InputMock m; NativeClockInputs out{};
        {
            NativeClockInputGuard<InputMock> guard(m);
            assert(guard.read(Route{2}, 9, out) == ClockInputResult::Ok);
            assert(m.held); // consuming operation is still protected
        }
        assert(!m.held);
    }
    // Every byte is independently corrupted and every short input parsed under
    // ASan/UBSan. Valid reserved-bit changes may still parse, but never overrun.
    for (size_t at = 0; at < v.size(); ++at) for (unsigned byte = 0; byte < 256; ++byte) {
        auto changed = v; changed[at] = uint8_t(byte); checksum(changed);
        for (size_t count : {at, changed.size()}) {
            VbtAfcPolicy p{}; parseVbtAfcPolicy(changed.data(), count, p);
        }
    }
    std::cout << "PASS: bounded VBT AFC policy, actual DSSM decoding, stable native agreement and fail-closed inputs\n";
}
