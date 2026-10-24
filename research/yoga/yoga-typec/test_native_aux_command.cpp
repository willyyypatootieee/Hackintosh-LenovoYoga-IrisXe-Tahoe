#include "NativeAuxCommand.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
using namespace YogaTypec;

struct Fake {
    bool locked = false, lockable = true, allowed = true, ran = false;
    bool changeIdentity = false, changeStatus = false;
    uint32_t initial = 0, status = (1u << 30) | (7u << 20), native = 0;
    unsigned attempts = 0, releases = 0, readCalls = 0, authCalls = 0;
    unsigned failRead = 0, failAuth = 0, command = 99, sentBytes = 0;
    uint32_t sent[5] = {};
    uint8_t reply[20] = {0, 0x77, 0x77, 1, 0, 0, 0};
    std::vector<uint32_t> reads;
    bool tryLockAux() { ++attempts; if (!lockable) return false; assert(!locked); return locked = true; }
    void unlockAux() { assert(locked); locked = false; ++releases; }
    bool authorized(Route r, uint64_t e) {
        assert(locked && r.tcIndex == 2 && e == 9);
        return ++authCalls != failAuth && allowed && !(ran && changeIdentity);
    }
    bool readActual32(uint32_t address, uint32_t &out) {
        assert(locked); reads.push_back(address);
        if (++readCalls == failRead) return false;
        if (address == 0x64510) {
            out = ran ? status : initial;
            if (ran && changeStatus && readCalls > 2) out ^= 1u << 20;
        } else {
            assert(ran && address >= 0x64514 && address <= 0x64524 && !(address & 3));
            const unsigned n = address - 0x64514;
            out = 0;
            for (unsigned i = 0; i < 4; ++i) out |= uint32_t(reply[n + i]) << (24 - 8 * i);
        }
        return true;
    }
    uint32_t runCaptured(uint32_t kind, uint32_t *words, uint32_t bytes, bool flag) {
        assert(locked && !flag && !ran); ran = true;
        command = kind; sentBytes = bytes; memcpy(sent, words, sizeof(sent));
        // Simulate stale/wrong original scratch storage. Adapter MUST use actual
        // control/data registers, not copied requested length or these words.
        memset(words, 0xa5, sizeof(sent));
        return native;
    }
};

static NativeAuxReply read(Fake &f, uint8_t *out, size_t count = 6) {
    NativeAuxCommand<Fake> adapter(f);
    auto r = adapter.read({2}, 9, 0x202, out, count);
    assert(!f.locked && f.releases == (f.lockable ? 1u : 0u));
    return r;
}

int main() {
    uint8_t output[16];
    {
        Fake f; memset(output, 0xcc, sizeof(output));
        auto r = read(f, output);
        assert(r.result == NativeAuxResult::Ok && r.actual == 6);
        assert(!memcmp(output, f.reply + 1, 6) && output[6] == 0xcc);
        assert(f.command == 0 && f.sentBytes == 4 && f.sent[0] == 0x90020205);
        assert(f.reads == std::vector<uint32_t>({0x64510,0x64510,0x64514,0x64518,0x64510}));
    }
    // Exhaust all hardware-reported sizes at every allowed payload size. No
    // requested-length completion; bytes beyond actual remain untouched.
    for (unsigned count = 1; count <= 16; ++count) for (unsigned bytes = 0; bytes <= 31; ++bytes) {
        Fake f; memset(output, 0xcc, sizeof(output));
        f.status = (1u << 30) | (bytes << 20);
        for (unsigned i = 1; i < 20; ++i) f.reply[i] = uint8_t(i);
        auto r = read(f, output, count);
        if (!bytes || bytes > count + 1) {
            assert(r.result == NativeAuxResult::InvalidReply && r.actual == 0);
            for (auto byte : output) assert(byte == 0xcc);
        } else {
            assert(r.actual == bytes - 1);
            assert(r.result == (bytes == count + 1 ? NativeAuxResult::Ok : NativeAuxResult::Short));
            for (unsigned i = 0; i < 16; ++i) assert(output[i] == (i < r.actual ? i + 1 : 0xcc));
        }
    }
    for (unsigned count = 1; count <= 16; ++count) for (unsigned bytes = 1; bytes <= 3; ++bytes)
        for (unsigned written = 0; written <= 17; ++written) {
            Fake f; NativeAuxCommand<Fake> adapter(f);
            uint8_t input[16]; for (unsigned i = 0; i < 16; ++i) input[i] = uint8_t(i + 3);
            f.status = (1u << 30) | (bytes << 20); f.reply[1] = uint8_t(written);
            auto r = adapter.write({2}, 9, 0x102, input, count);
            assert(!f.locked && f.releases == 1 && f.command == 1 && f.sentBytes == count + 4);
            assert(f.sent[0] == (0x80010200u | (count - 1)));
            assert(!memcmp(reinterpret_cast<uint8_t *>(f.sent + 1), input, count));
            const auto *payload = reinterpret_cast<uint8_t *>(f.sent + 1);
            for (unsigned i = count; i < 16; ++i) assert(payload[i] == 0);
            if (bytes > 2 || (bytes == 2 && written > count)) {
                assert(r.result == NativeAuxResult::InvalidReply && !r.actual);
            } else {
                assert(r.actual == (bytes == 1 ? count : written));
                assert(r.result == (r.actual == count ? NativeAuxResult::Ok : NativeAuxResult::Short));
            }
        }
    // Every possible reply header, including I2C reply values/reserved bits.
    for (unsigned header = 0; header < 256; ++header) {
        Fake f; f.reply[0] = uint8_t(header); memset(output, 0xcc, sizeof(output));
        auto r = read(f, output);
        const auto expected = header == 0 ? NativeAuxResult::Ok : header == 0x10 ? NativeAuxResult::Nack :
            header == 0x20 ? NativeAuxResult::Defer : NativeAuxResult::InvalidReply;
        assert(r.result == expected);
        if (header) { assert(!r.actual); for (auto byte : output) assert(byte == 0xcc); }
    }
    // Every failure point leaves caller memory untouched and releases the lock.
    for (unsigned failure = 1; failure <= 5; ++failure) {
        Fake f; f.failRead = failure; memset(output, 0xcc, sizeof(output));
        auto r = read(f, output); assert(r.result == NativeAuxResult::ReadFailed && !r.actual);
        for (auto byte : output) assert(byte == 0xcc);
    }
    for (unsigned failure = 1; failure <= 4; ++failure) {
        Fake f; f.failAuth = failure; memset(output, 0xcc, sizeof(output));
        auto r = read(f, output); assert(!r.actual);
        assert(r.result == (failure == 1 ? NativeAuxResult::Unauthorized : NativeAuxResult::Changed));
        for (auto byte : output) assert(byte == 0xcc);
    }
    const struct { uint32_t status; NativeAuxResult error; } cases[] = {
        {UINT32_MAX,NativeAuxResult::ReadFailed}, {1u<<31,NativeAuxResult::Busy},
        {(1u<<30)|(1u<<28),NativeAuxResult::Timeout},
        {(1u<<30)|(1u<<25),NativeAuxResult::ReceiveError},
        {(1u<<30)|(1u<<11),NativeAuxResult::Changed}, {0,NativeAuxResult::NativeError}
    };
    for (auto c : cases) {
        Fake f; f.status = c.status; memset(output, 0xcc, sizeof(output));
        auto r = read(f, output); assert(r.result == c.error && !r.actual);
        for (auto byte : output) assert(byte == 0xcc);
    }
    { Fake f; f.lockable = false; assert(read(f,output).result == NativeAuxResult::Busy); assert(!f.ran); }
    { Fake f; f.allowed = false; assert(read(f,output).result == NativeAuxResult::Unauthorized); assert(!f.ran); }
    { Fake f; f.changeIdentity = true; assert(read(f,output).result == NativeAuxResult::Changed); }
    { Fake f; f.changeStatus = true; assert(read(f,output).result == NativeAuxResult::Changed); }
    { Fake f; f.native = 0xe00002eb; assert(read(f,output).result == NativeAuxResult::NativeError); }
    { Fake f; f.initial = 1u << 31; assert(read(f,output).result == NativeAuxResult::Busy); assert(!f.ran); }
    { Fake f; f.initial = 1u << 11; assert(read(f,output).result == NativeAuxResult::Unauthorized); assert(!f.ran); }
    { Fake f; f.initial = UINT32_MAX; assert(read(f,output).result == NativeAuxResult::ReadFailed); assert(!f.ran); }
    { Fake f; memset(f.reply + 1,0xff,16); assert(read(f,output).result == NativeAuxResult::Ok); assert(output[0] == 0xff); }
    // Invalid caller inputs must not acquire a lock or touch hardware.
    {
        Fake f; NativeAuxCommand<Fake> adapter(f);
        assert(adapter.read({1},9,0,output,1).result == NativeAuxResult::InvalidInput);
        assert(adapter.read({2},0,0,output,1).result == NativeAuxResult::InvalidInput);
        assert(adapter.read({2},9,0,output,0).result == NativeAuxResult::InvalidInput);
        assert(adapter.read({2},9,0,output,17).result == NativeAuxResult::InvalidInput);
        assert(adapter.read({2},9,0x100000,output,1).result == NativeAuxResult::InvalidInput);
        assert(adapter.read({2},9,0xfffff,output,2).result == NativeAuxResult::InvalidInput);
        assert(adapter.read({2},9,0,nullptr,1).result == NativeAuxResult::InvalidInput);
        assert(adapter.write({2},9,0,nullptr,1).result == NativeAuxResult::InvalidInput);
        assert(!f.attempts && !f.ran);
    }
    puts("PASS: captured AUX ABI, actual lengths, short transfers, errors and serialization");
}
