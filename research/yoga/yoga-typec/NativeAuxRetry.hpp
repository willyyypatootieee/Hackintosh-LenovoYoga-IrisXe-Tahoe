// Bounded DPCD retries around the actual single-command adapter.
// No power acquisition, native hook registration or journal reconciliation.
#pragma once
#include "NativeAuxCommand.hpp"

namespace YogaTypec {
// Scheduler contract: monotonic microseconds; current() checks retained powered
// route/epoch admission, not a cached flag. pause() runs only AFTER the command
// RAII unlock has completed and without global FIA/DKL/clock/cursor locks held.
// It must not outlive the caller's native lifetime/core/DC6/AUX lease.
template<class Command, class Scheduler> class NativeAuxRetry {
    Command &command;
    Scheduler &schedule;
    bool dirty = false;
    static constexpr unsigned attempts = 7;
    static constexpr uint64_t budget = 40000, spacing = 1000;

    NativeAuxReply transfer(Route r, uint64_t e, uint32_t address,
                            uint8_t *out, const uint8_t *in, size_t count) {
        const bool write = in != nullptr;
        if (!r.valid() || r.tcIndex != 2 || !e || !count || count > 16 ||
            address > 0xfffff || count - 1 > 0xfffff - address ||
            (!write && !out) || (write && out))
            return {NativeAuxResult::InvalidInput,0};
        const uint64_t since = schedule.nowMicros();
        uint64_t last = since;
        NativeAuxReply reply{NativeAuxResult::Timeout,0};
        for (unsigned n = 0; n < attempts; ++n) {
            const auto now = schedule.nowMicros();
            if (now < last || now - since >= budget) return {NativeAuxResult::Timeout,0};
            last = now;
            if (!schedule.current(r,e)) return {NativeAuxResult::Changed,0};
            // Intent precedes even an ultimately rejected write; this flag is
            // conservative and never substitutes for the outer mutation journal.
            if (write) dirty = true;
            reply = write ? command.write(r,e,address,in,count) : command.read(r,e,address,out,count);
            const auto after = schedule.nowMicros();
            if (!schedule.current(r,e)) return {NativeAuxResult::Changed,0};
            if (after < last || after - since >= budget) return {NativeAuxResult::Timeout,0};
            last = after;
            // Never replay a short/uncertain write, timeout, lost status or
            // ambiguous Busy after a hardware attempt. Explicit native DEFER
            // alone authorizes resending the SAME write. Reads may retry link
            // errors; NACK/malformed/short responses are terminal in both cases.
            const bool retry = reply.result == NativeAuxResult::Defer || (!write &&
                (reply.result == NativeAuxResult::Busy || reply.result == NativeAuxResult::Timeout ||
                 reply.result == NativeAuxResult::ReceiveError));
            if (!retry || n + 1 == attempts) return reply;
            if (budget - (after - since) <= spacing || !schedule.pause(spacing))
                return {NativeAuxResult::Timeout,0};
        }
        return reply;
    }
public:
    NativeAuxRetry(Command &c, Scheduler &s) : command(c), schedule(s) {}
    NativeAuxRetry(const NativeAuxRetry &) = delete;
    NativeAuxRetry &operator=(const NativeAuxRetry &) = delete;
    bool writeIntentObserved() const { return dirty; }
    // Intentionally no automatic clear: caller's transaction owns cleanup.
    NativeAuxReply read(Route r, uint64_t e, uint32_t address, uint8_t *out, size_t count) {
        // Do not publish partial/failed-attempt bytes to a trainer. The original
        // command has its own scratch; this second buffer covers retries/deadline.
        uint8_t temporary[16] = {};
        if (!out || !count || count > sizeof(temporary)) return {NativeAuxResult::InvalidInput,0};
        auto reply = transfer(r,e,address,temporary,nullptr,count);
        if (reply.result == NativeAuxResult::Ok && reply.actual == count) {
            for (size_t i=0; i<count; ++i) out[i]=temporary[i];
        } else if (reply.result == NativeAuxResult::Ok) return {NativeAuxResult::InvalidReply,0};
        return reply;
    }
    NativeAuxReply write(Route r, uint64_t e, uint32_t address, const uint8_t *in, size_t count) {
        if (!in) return {NativeAuxResult::InvalidInput,0};
        return transfer(r,e,address,nullptr,in,count);
    }
};
} // namespace YogaTypec
