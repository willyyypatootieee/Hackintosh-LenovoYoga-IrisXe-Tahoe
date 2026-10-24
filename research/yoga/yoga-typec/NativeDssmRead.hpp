// Read-only native mapping boundary. No raw pointers, native casts or MMIO
// writes. Backend supplies typed retained IOKit provenance, not file properties.
#pragma once
#include "AdlpTypecTransaction.hpp"

namespace YogaTypec {
struct NativeMmioSnapshot {
    uint64_t controller, provider, manager, mapping, descriptor;
    uint64_t virtualAddress, physicalAddress, mappingLength, barAddress, barLength;
    uint64_t controllerAddress, managerAddress;
    uint32_t controllerLength, managerLength;
    uint64_t epoch;
    bool exactBuild, yogaIgpu, started, stopping, emulated;
    bool kernelTask, mmioAttributesVerified, descriptorMatchesBar;
};

inline bool sameNativeMapping(const NativeMmioSnapshot &a, const NativeMmioSnapshot &b) {
    return a.controller == b.controller && a.provider == b.provider &&
        a.manager == b.manager && a.mapping == b.mapping && a.descriptor == b.descriptor &&
        a.virtualAddress == b.virtualAddress && a.physicalAddress == b.physicalAddress &&
        a.mappingLength == b.mappingLength && a.barAddress == b.barAddress &&
        a.barLength == b.barLength && a.controllerAddress == b.controllerAddress &&
        a.managerAddress == b.managerAddress && a.controllerLength == b.controllerLength &&
        a.managerLength == b.managerLength && a.epoch == b.epoch &&
        a.exactBuild == b.exactBuild && a.yogaIgpu == b.yogaIgpu &&
        a.started == b.started && a.stopping == b.stopping && a.emulated == b.emulated &&
        a.kernelTask == b.kernelTask && a.mmioAttributesVerified == b.mmioAttributesVerified &&
        a.descriptorMatchesBar == b.descriptorMatchesBar;
}

inline bool validNativeDssmMapping(const NativeMmioSnapshot &s, uint64_t epoch) {
    constexpr uint64_t dssmEnd = 0x51008;
    if (!epoch || s.epoch != epoch || !s.controller || !s.provider || !s.manager ||
        !s.mapping || !s.descriptor || !s.exactBuild || !s.yogaIgpu || !s.started ||
        s.stopping || s.emulated || !s.kernelTask || !s.mmioAttributesVerified ||
        !s.descriptorMatchesBar || !s.virtualAddress || !s.physicalAddress ||
        !s.barAddress || (s.virtualAddress & 3) || (s.physicalAddress & 3)) return false;
    // Narrowly accept the native full-BAR mapping, not submaps or guessed BARs.
    // Native controller and manager store32-bit lengths; truncation is refused.
    if (s.mappingLength < dssmEnd || s.mappingLength > UINT32_MAX ||
        s.mappingLength != s.barLength || s.physicalAddress != s.barAddress ||
        s.controllerLength != s.mappingLength || s.managerLength != s.mappingLength ||
        s.controllerAddress != s.virtualAddress || s.managerAddress != s.virtualAddress)
        return false;
    return s.virtualAddress <= UINT64_MAX - (s.mappingLength - 1) &&
        s.physicalAddress <= UINT64_MAX - (s.mappingLength - 1);
}

enum class DssmReadResult : uint8_t { Ok, Busy, Unauthorized, InvalidMapping, ReadFailed, Changed };

// Caller already holds NativeClockInputGuard's input/lifetime exclusion.
// Backend lockMapping participates in native start/stop/remap exclusion, taken
// AFTER that input lock. No backend exists yet; a private new lock is NOT enough.
// retainMapping atomically retains controller/provider/manager/map/descriptor
// and binds them to this EXACT route/epoch; failure leaves no references held.
// snapshotRetained uses typed IOKit map/descriptor/PCI APIs plus exact-build
// audited native fields. Flags represent verified facts, never caller options.
// A retain alone does not prevent explicit unmap: lifecycle exclusion is required.
// readRetained32 is ordered actual MMIO on the retained map, not native
// ReadRegister32 (which returns0 on bounds failure), emulation or cached data.
// No writes, allocations, sleeps or native power transitions under these locks.
// Release drops object references only, never power or hardware ownership.
template<class Backend>
DssmReadResult readNativeDssmActualLocked(Backend &io, Route route, uint64_t epoch,
                                         uint32_t &out) {
    if (!route.valid() || route.tcIndex != 2 || !epoch) return DssmReadResult::Unauthorized;
    if (!io.lockMapping()) return DssmReadResult::Busy;
    struct Unlock { Backend &io; ~Unlock() { io.unlockMapping(); } } unlock{io};
    if (!io.authorized(route, epoch) || !io.retainMapping(route, epoch))
        return DssmReadResult::Unauthorized;
    struct Release { Backend &io; ~Release() { io.releaseMapping(); } } release{io};
    NativeMmioSnapshot before{}, after{};
    if (!io.snapshotRetained(before) || !validNativeDssmMapping(before, epoch))
        return DssmReadResult::InvalidMapping;
    uint32_t value;
    if (!io.readRetained32(0x51004u, value) || value == UINT32_MAX)
        return DssmReadResult::ReadFailed;
    if (!io.snapshotRetained(after) || !validNativeDssmMapping(after, epoch) ||
        !sameNativeMapping(before, after)) return DssmReadResult::Changed;
    if (!io.authorized(route, epoch)) return DssmReadResult::Unauthorized;
    out = value; // zero is valid data ONLY after successful actual mapping read
    return DssmReadResult::Ok;
}
} // namespace YogaTypec
