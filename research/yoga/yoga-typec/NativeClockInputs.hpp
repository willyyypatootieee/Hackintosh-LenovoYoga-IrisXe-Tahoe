// Narrow configuration boundary for the future native TC3 backend.
// No native pointers, MMIO mapping, hardware writes or default clock guesses.
#pragma once
#include "AdlpTypecTransaction.hpp"
#include <stddef.h>
#include <string.h>

namespace YogaTypec {
struct VbtAfcPolicy {
    uint16_t bdbVersion;
    bool overrideAfc;
    uint8_t startup;
};

inline uint16_t clockInputLe16(const uint8_t *p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline uint32_t clockInputLe32(const uint8_t *p) {
    return uint32_t(clockInputLe16(p)) | (uint32_t(clockInputLe16(p + 2)) << 16);
}

// Linux v6.12 intel_bios.c parse_general_features and intel_vbt_defs.h.
// Caller supplies the SAME retained IGPU's verified VBT bytes, not a Dell VBT
// or an unbound file/property. This parser establishes structure, not identity.
// Parse only up to first GENERAL_FEATURES like Linux find_raw_section: later
// unrelated blocks need not be traversed to obtain this policy. All traversed
// lengths are bounded. Output unchanged on any failure. No implicit padding of
// a missing 249+ field; fail closed rather than silently infer no override.
inline bool parseVbtAfcPolicy(const uint8_t *bytes, size_t length, VbtAfcPolicy &out) {
    if (!bytes || length < 48 || memcmp(bytes, "$VBT", 4)) return false;
    const size_t header = clockInputLe16(bytes + 22);
    const size_t total = clockInputLe16(bytes + 24);
    const size_t bdb = clockInputLe32(bytes + 28);
    if (header < 48 || total < header || total > length || bdb < header ||
        bdb > total || total - bdb < 22) return false;
    uint8_t checksum = 0;
    for (size_t i = 0; i < total; ++i) checksum = uint8_t(checksum + bytes[i]);
    if (checksum || memcmp(bytes + bdb, "BIOS_DATA_BLOCK ", 16)) return false;
    const auto version = clockInputLe16(bytes + bdb + 16);
    const size_t bdbHeader = clockInputLe16(bytes + bdb + 18);
    const size_t bdbSize = clockInputLe16(bytes + bdb + 20);
    if (bdbHeader < 22 || bdbSize < bdbHeader || bdbSize > total - bdb) return false;
    const size_t end = bdb + bdbSize;
    size_t cursor = bdb + bdbHeader;
    while (end - cursor >= 3) {
        const auto id = bytes[cursor];
        size_t size = clockInputLe16(bytes + cursor + 1);
        cursor += 3;
        // MIPI_SEQUENCE v3+ carries its 32-bit block size after version byte.
        if (id == 53) {
            if (end - cursor < 1 || size < 1) return false;
            if (bytes[cursor] >= 3) {
                if (end - cursor < 5) return false;
                size = clockInputLe32(bytes + cursor + 1);
                if (size < 5) return false;
            }
        }
        if (size > end - cursor) return false;
        if (id == 1) {
            if (size < 5 || (version >= 249 && size < 7)) return false;
            const unsigned config = version >= 249 ? bytes[cursor + 6] & 3 : 0;
            out = {version, config != 0, uint8_t(config == 1 ? 0 : config ? 7 : 0)};
            return true;
        }
        cursor += size;
    }
    return false;
}

inline bool decodeDssmReference(uint32_t actual, uint32_t &kHz) {
    if (actual == UINT32_MAX) return false;
    switch (actual >> 29) {
        case 0: kHz = 24000; return true;
        case 1: kHz = 19200; return true;
        case 2: kHz = 38400; return true;
        default: return false; // unlike Linux warning/fallback, never guess
    }
}

struct NativeClockInputs {
    uint32_t referenceKHz;
    VbtAfcPolicy afc;
};
enum class ClockInputResult : uint8_t {
    Ok, Busy, Unauthorized, InvalidVbt, InvalidRead, Changed, NativeMismatch
};

// Backend lockInputs uses established native lifetime/controller serialization.
// authorized proves retained identity, epoch and display-core power until unlock.
// readDssmActual must use a validated actual MMIO mapping: native ReadRegister32
// returns ZERO on bounds failure, which otherwise falsely looks like 24 MHz.
// readNativeReferenceEncoding obtains controller+0xeb0 only after exact-build
// object provenance/length checks and initCDClock completion. Not a raw cast.
// retainedVbt is immutable and bound to this controller for the whole callback.
// No writes, sleeps, allocations or fallback/native clock sanitization here.
// Lock also excludes native clock-input reinitialization and VBT replacement.
// The Locked helper is ONLY for a caller already holding that same exclusion
// through its consuming MMIO operation. Output is not a perpetual MMIO lease.
template<class Backend>
ClockInputResult readNativeClockInputsLocked(Backend &io, Route route, uint64_t epoch,
                                            NativeClockInputs &out) {
    if (!route.valid() || !io.authorized(route, epoch)) return ClockInputResult::Unauthorized;
    const uint8_t *vbt = nullptr; size_t size = 0;
    VbtAfcPolicy afc{};
    if (!io.retainedVbt(route, epoch, vbt, size) || !parseVbtAfcPolicy(vbt, size, afc))
        return ClockInputResult::InvalidVbt;
    uint32_t first, second, native, reference;
    if (!io.readDssmActual(first) || !decodeDssmReference(first, reference) ||
        !io.readNativeReferenceEncoding(native) ||
        !io.readDssmActual(second) || second == UINT32_MAX)
        return ClockInputResult::InvalidRead;
    if ((first >> 29) != (second >> 29)) return ClockInputResult::Changed;
    if (native > 2 || native != (first >> 29)) return ClockInputResult::NativeMismatch;
    if (!io.authorized(route, epoch)) return ClockInputResult::Unauthorized;
    out = {reference, afc};
    return ClockInputResult::Ok;
}

// Deliberately holds the input/lifetime exclusion after validation, until the
// consuming PLL operation has completed. Never holds it across scheduled steps,
// sleeps or polls. It does not establish the separate shared DKL selector lock.
template<class Backend> class NativeClockInputGuard {
    Backend &io;
    bool held;
public:
    explicit NativeClockInputGuard(Backend &backend) : io(backend), held(io.lockInputs()) {}
    ~NativeClockInputGuard() { if (held) io.unlockInputs(); }
    NativeClockInputGuard(const NativeClockInputGuard &) = delete;
    NativeClockInputGuard &operator=(const NativeClockInputGuard &) = delete;
    ClockInputResult read(Route route, uint64_t epoch, NativeClockInputs &out) {
        return held ? readNativeClockInputsLocked(io, route, epoch, out) : ClockInputResult::Busy;
    }
};

template<class Backend>
ClockInputResult readNativeClockInputs(Backend &io, Route route, uint64_t epoch,
                                      NativeClockInputs &out) {
    NativeClockInputGuard<Backend> guard(io);
    return guard.read(route, epoch, out);
}
} // namespace YogaTypec
