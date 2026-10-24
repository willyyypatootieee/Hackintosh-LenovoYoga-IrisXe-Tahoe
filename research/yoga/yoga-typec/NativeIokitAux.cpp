#include "NativeIokitAux.hpp"
#include <libkern/c++/OSMetaClass.h>
#include <string.h>

namespace YogaTypec {
namespace {
template<class T> T auxField(const void *p, size_t offset) {
    T out; memcpy(&out,static_cast<const uint8_t *>(p)+offset,sizeof(out)); return out;
}
}
bool NativeIokitAux::portIdentity(IOService *controller, Route r, uint64_t e) const {
    if (!controller || !port || !binding.current(controller,port,r,e) ||
        !port->metaCast("AppleIntelPortHAL") || !port->getMetaClass() ||
        port->getMetaClass()->getClassSize() < 0x650 || !binding.portVptr() ||
        auxField<uintptr_t>(port,0) != binding.portVptr() || !binding.verifiedOriginal()) return false;
    auto *const *global = binding.verifiedGlobalControllerSlot();
    if (!global || auxField<IOService *>(global,0) != controller) return false;
    const auto *config = binding.retainedConfig();
    if (!config || auxField<const uint32_t *>(port,0x548) != config) return false;
    // TC3/DDI-F requires the actual retained stock TC3 configuration row, not
    // the deployed DDI-B row with its misleading TC1 comment. This check does
    // not mutate or publish that config: connector routing is coordinator work.
    static const uint32_t expected[6] = {5,11,1,1,0x400,0x201};
    if (memcmp(config,expected,sizeof(expected))) return false;
    // Exact runAUXCommand sets TBT_IO bit11 iff mode==2; native DP-alt is3.
    // Do not copy the misleading numeric labels from HookCase's old probe.
    return auxField<uint32_t>(port,0x550) == 3 &&
        auxField<uint32_t>(port,0x590) == 0x64500 &&
        auxField<uint32_t>(port,0x594) == 0x64510 &&
        auxField<uint32_t>(port,0x598) == 0x64514;
}
bool NativeIokitAux::tryLockAux() {
    if (lock) return false;
    IOService *controller = map.retainedController();
    if (!controller || !portIdentity(controller,route,epoch)) return false;
    auto *candidate = auxField<IORecursiveLock *>(controller,0xc30);
    if (!candidate || !IORecursiveLockTryLock(candidate)) return false;
    lock = candidate;
    return true;
}
void NativeIokitAux::unlockAux() {
    if (!lock) return;
    IORecursiveLock *held = lock;
    lock = nullptr;
    IORecursiveLockUnlock(held);
}
bool NativeIokitAux::authorized(Route r, uint64_t e) {
    IOService *controller = map.retainedController();
    if (!lock || r.tcIndex != route.tcIndex || e != epoch || !map.authorized(r,e) || !portIdentity(controller,r,e) ||
        auxField<IORecursiveLock *>(controller,0xc30) != lock) return false;
    uint32_t hpd=0, power=0, buffer=0;
    if (!map.readAux32(0x44470,hpd) || hpd == UINT32_MAX ||
        !map.readAux32(0x45444,power) || power == UINT32_MAX ||
        !map.readAux32(0x64500,buffer) || buffer == UINT32_MAX) return false;
    return (hpd & ((1u<<18)|(1u<<2))) == (1u<<18) &&
        (power & ((1u<<11)|(1u<<10))) == ((1u<<11)|(1u<<10)) &&
        (buffer & (1u<<6)) && map.authorized(r,e) && portIdentity(controller,r,e);
}
bool NativeIokitAux::readActual32(uint32_t offset, uint32_t &value) {
    return lock && map.readAux32(offset,value);
}
uint32_t NativeIokitAux::runCaptured(uint32_t command, uint32_t *words, uint32_t bytes, bool flag) {
    constexpr uint32_t denied = 0xe00002e2; // never interpreted as byte count
    if (!words || flag || command > 1 || !authorized(route,epoch)) return denied;
    const size_t count = (words[0] & 255) + 1;
    if (!count || count > 16 || bytes != (command ? count + 4 : 4) ||
        (words[0] >> 28) != (command ? 8u : 9u)) return denied;
    auto original = binding.verifiedOriginal();
    if (!original) return denied;
    return original(port,command,words,bytes,false);
}
} // namespace YogaTypec
