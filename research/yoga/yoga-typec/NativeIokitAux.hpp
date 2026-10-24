// Kernel implementation for NativeAuxCommand's physical transport callbacks.
// Hook/lifecycle coordinator is still required; this file installs no routes.
#pragma once
#include "NativeIokitMapping.hpp"
#include "NativeAuxCommand.hpp"
#include <IOKit/IOLib.h>

namespace YogaTypec {
class NativeAuxPortBinding {
public:
    using Original = uint32_t (*)(void *, uint32_t, uint32_t *, uint32_t, bool);
    // Caller owns/retains the native port, immutable PortConfig, controller and
    // verified original trampoline. This check must remain true under the outer
    // native input/lifecycle lease, never be a user option or cached ready flag.
    virtual bool current(IOService *, OSObject *, Route, uint64_t) = 0;
    virtual uintptr_t portVptr() const = 0;
    virtual const uint32_t *retainedConfig() const = 0;
    virtual Original verifiedOriginal() const = 0;
    // Original runAUXCommand dereferences loaded-image _gController, NOT a port
    // back-pointer. Return its byte-verified relocated slot, whose lifetime and
    // writes are excluded by the outer lifecycle lease. Never a registry value.
    virtual IOService *const *verifiedGlobalControllerSlot() const = 0;
protected:
    ~NativeAuxPortBinding() = default;
};

class NativeIokitAux {
    NativeIokitMapping &map;
    NativeAuxPortBinding &binding;
    OSObject *const port;
    const Route route;
    const uint64_t epoch;
    IORecursiveLock *lock = nullptr;
    bool portIdentity(IOService *, Route, uint64_t) const;
public:
    // Mapping/lifetime/input lease is already held, with references retained.
    // Backend lifetime must be shorter than that lease; no implicit power work.
    NativeIokitAux(NativeIokitMapping &m, NativeAuxPortBinding &b, OSObject *p, Route r, uint64_t e)
        : map(m), binding(b), port(p), route(r), epoch(e) {}
    NativeIokitAux(const NativeIokitAux &) = delete;
    NativeIokitAux &operator=(const NativeIokitAux &) = delete;
    bool tryLockAux();
    bool commandLockHeld() const { return lock != nullptr; }
    void unlockAux();
    bool authorized(Route r, uint64_t e);
    bool readActual32(uint32_t offset, uint32_t &value);
    uint32_t runCaptured(uint32_t command, uint32_t *words, uint32_t bytes, bool flag);
};
} // namespace YogaTypec
