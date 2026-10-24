// Kernel-side typed IOKit implementation. No hook registration or deployment.
#pragma once
#include "NativeDssmRead.hpp"
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>

namespace YogaTypec {
// Implemented by the native hook/lifecycle coordinator, NOT a private mutex.
// tryEnter must exclude native stop/remap/sleep until leave; input exclusion is
// already held by the caller. Admission during a routed original start/stop is
// forbidden. This interface deliberately has no permissive default implementation.
class NativeMappingLifetime {
public:
    virtual bool tryEnter() = 0;
    virtual void leave() = 0;
    virtual bool current(IOService *controller, Route route, uint64_t epoch) = 0;
    // These must come from byte-verified, relocated symbols in THIS loaded image.
    virtual uintptr_t controllerVptr() const = 0;
    virtual uintptr_t managerVptr() const = 0;
    virtual bool exactBuildVerified() const = 0;
protected:
    ~NativeMappingLifetime() = default;
};

class NativeIokitMapping {
    NativeMappingLifetime &lifetime;
    IOService *const source;
    IOService *controller = nullptr;
    IOPCIDevice *provider = nullptr;
    OSObject *manager = nullptr;
    IOMemoryMap *mapping = nullptr;
    IOMemoryDescriptor *descriptor = nullptr;
    IODeviceMemory *bar = nullptr;
    Route route{0xff};
    uint64_t epoch = 0;
    bool held = false;
    bool nativeIdentity() const;
    bool gpuIdentity() const;
    bool snapshot(NativeMmioSnapshot &out) const;
    bool readActual32Checked(uint32_t offset, uint32_t &value);
public:
    // Source must be an IOService obtained from a verified native hook, never
    // an address from an IORegistry property or a guessed gController pointer.
    NativeIokitMapping(IOService *s, NativeMappingLifetime &l) : lifetime(l), source(s) {}
    NativeIokitMapping(const NativeIokitMapping &) = delete;
    NativeIokitMapping &operator=(const NativeIokitMapping &) = delete;
    // No implicit lifecycle/power/MMIO work at destruction. Caller explicitly
    // releases references while the native lifetime boundary is still held.
    bool lockMapping();
    void unlockMapping();
    bool authorized(Route r, uint64_t e);
    bool retainMapping(Route r, uint64_t e);
    void releaseMapping();
    bool snapshotRetained(NativeMmioSnapshot &out);
    bool readRetained32(uint32_t offset, uint32_t &value);
    // Separate, read-only AUX surface. Caller holds same retained mapping,
    // native command serialization and outer core/DC6/AUX power lifetime.
    bool readAux32(uint32_t offset, uint32_t &value);
    IOService *retainedController() const { return nativeIdentity() ? controller : nullptr; }
};
} // namespace YogaTypec
