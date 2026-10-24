// Exact captured Apple transport adapter; not a loadable kext or MMIO backend.
// Native readAUX/writeAUX cannot provide actual transferred-byte counts.
#pragma once
#include "AdlpTypecTransaction.hpp"
#include <stddef.h>

namespace YogaTypec {
enum class NativeAuxResult : uint8_t {
    Ok, Short, InvalidInput, Busy, Unauthorized, Changed, ReadFailed,
    Timeout, ReceiveError, InvalidReply, Nack, Defer, NativeError
};

struct NativeAuxReply {
    NativeAuxResult result;
    size_t actual;
};

// Backend integration requirements (NOT implemented by this header):
// - tryLockAux/unlockAux use the SAME controller+0xc30 recursive lock as Apple.
// - Admission/lifetime exclusion, a retained typed live mapping, TC3 DP-alt
//   ownership and NativeAuxLease remain held outside this short command lock.
// - authorized checks those facts, the exact driver build, port/DDI5 identity,
//   epoch, actual AUX2 power, and disabled simulation/overrides on every call.
// - No private lock, native emulated readRegister32, cached status, high-level
//   readAUX/writeAUX or forwarding through another hook can satisfy this API.
// - runCaptured calls the audited original runAUXCommand directly on the retained
//   port: command0=read,1=write; uint32_t[5]; tx length; final bool=false.
//   It nests the recursive lock. DC6/core holds belong to the outer lease, not
//   this native method. Its IOReturn is NOT a transferred-byte count.
// - readActual32 does ordered MMIO on that retained map. No other AUX user can
//   run between the original command and its control/data readback.
// - The native original must stay byte/ABI verified, including no post-command
//   status-clear before return. Unmap/sleep/detach cannot proceed until release.
// One attempt only: no training waits, retry sleeps, allocation or power changes
// under this lock. Caller schedules bounded DEFER/error retries outside the lock.
template<class Backend> class NativeAuxCommand {
    Backend &io;
    static constexpr uint32_t ctl = 0x64510; // TC3 AUX, not AUX C or native HDMI
    static constexpr uint32_t data = ctl + 4;
    static constexpr uint32_t busy = 1u << 31, done = 1u << 30;
    static constexpr uint32_t timeout = 1u << 28, receive = 1u << 25;
    static constexpr uint32_t sizeMask = 31u << 20, tbt = 1u << 11;
    static constexpr uint32_t completionMask = busy | done | timeout | receive | sizeMask | tbt;

    NativeAuxReply transfer(Route route, uint64_t epoch, uint32_t address,
                            uint8_t *output, const uint8_t *input, size_t count) {
        const bool write = input != nullptr;
        if (!route.valid() || route.tcIndex != 2 || !epoch || !count || count > 16 ||
            address > 0xfffff || count - 1 > 0xfffff - address ||
            (!write && !output) || (write && output))
            return {NativeAuxResult::InvalidInput, 0};
        if (!io.tryLockAux()) return {NativeAuxResult::Busy, 0};
        struct Unlock { Backend &io; ~Unlock() { io.unlockAux(); } } unlock{io};
        if (!io.authorized(route, epoch)) return {NativeAuxResult::Unauthorized, 0};

        uint32_t before = 0;
        if (!io.readActual32(ctl, before) || before == UINT32_MAX)
            return {NativeAuxResult::ReadFailed, 0};
        if (before & busy) return {NativeAuxResult::Busy, 0};
        if (before & tbt) return {NativeAuxResult::Unauthorized, 0};

        // Apple expects a register-order header word followed by host-memory
        // payload bytes. Its original swaps ONLY the payload words on transmit.
        uint32_t words[5] = {};
        words[0] = (write ? 0x80000000u : 0x90000000u) | (address << 8) | uint32_t(count - 1);
        if (write) {
            auto *payload = reinterpret_cast<uint8_t *>(words + 1);
            for (size_t i = 0; i < count; ++i) payload[i] = input[i];
        }
        if (!io.authorized(route, epoch)) return {NativeAuxResult::Changed, 0};
        const uint32_t native = io.runCaptured(write ? 1u : 0u, words,
                                               write ? uint32_t(count + 4) : 4u, false);
        // On a write, hardware may already have changed even if this returns an
        // error. Outer transaction must journal intent BEFORE calling write().
        if (!io.authorized(route, epoch)) return {NativeAuxResult::Changed, 0};
        uint32_t status = 0;
        if (!io.readActual32(ctl, status) || status == UINT32_MAX)
            return {NativeAuxResult::ReadFailed, 0};
        if (status & busy) return {NativeAuxResult::Busy, 0};
        if (status & tbt) return {NativeAuxResult::Changed, 0};
        if (status & timeout) return {NativeAuxResult::Timeout, 0};
        if (status & receive) return {NativeAuxResult::ReceiveError, 0};
        if (!(status & done)) return {NativeAuxResult::NativeError, 0};
        const size_t bytes = (status & sizeMask) >> 20;
        if (!bytes || bytes > 20 || bytes > (write ? 2 : count + 1))
            return {NativeAuxResult::InvalidReply, 0};

        uint8_t reply[20] = {};
        for (size_t offset = 0; offset < bytes; offset += 4) {
            uint32_t value = 0;
            // All-ones data is legitimate; only the read's boolean is validity.
            if (!io.readActual32(data + uint32_t(offset), value))
                return {NativeAuxResult::ReadFailed, 0};
            for (size_t i = 0; i < 4 && offset + i < bytes; ++i)
                reply[offset + i] = uint8_t(value >> (24 - i * 8));
        }
        uint32_t after = 0;
        if (!io.readActual32(ctl, after) || after == UINT32_MAX)
            return {NativeAuxResult::ReadFailed, 0};
        if ((after & completionMask) != (status & completionMask) || !io.authorized(route, epoch))
            return {NativeAuxResult::Changed, 0};
        // This is native DPCD, not I2C-over-AUX: values4/8 are not native ACKs.
        if (reply[0] & 15) return {NativeAuxResult::InvalidReply, 0};
        switch (reply[0] >> 4) {
            case 1: return {NativeAuxResult::Nack, 0};
            case 2: return {NativeAuxResult::Defer, 0};
            case 0: break;
            default: return {NativeAuxResult::InvalidReply, 0};
        }
        if (native) return {NativeAuxResult::NativeError, 0};
        // Linux intel_dp_aux_transfer: ACK-only writes imply full size; a
        // second byte reports a short write. Refuse an impossible count rather
        // than silently clamp it and turn malformed data into full completion.
        const size_t actual = write ? (bytes == 1 ? count : reply[1]) : bytes - 1;
        if (actual > count) return {NativeAuxResult::InvalidReply, 0};
        if (!write) for (size_t i = 0; i < actual; ++i) output[i] = reply[i + 1];
        return {actual == count ? NativeAuxResult::Ok : NativeAuxResult::Short, actual};
    }

public:
    explicit NativeAuxCommand(Backend &backend) : io(backend) {}
    NativeAuxCommand(const NativeAuxCommand &) = delete;
    NativeAuxCommand &operator=(const NativeAuxCommand &) = delete;
    NativeAuxReply read(Route route, uint64_t epoch, uint32_t address,
                         uint8_t *out, size_t count) {
        return transfer(route, epoch, address, out, nullptr, count);
    }
    NativeAuxReply write(Route route, uint64_t epoch, uint32_t address,
                          const uint8_t *in, size_t count) {
        if (!in) return {NativeAuxResult::InvalidInput, 0};
        return transfer(route, epoch, address, nullptr, in, count);
    }
};
} // namespace YogaTypec
