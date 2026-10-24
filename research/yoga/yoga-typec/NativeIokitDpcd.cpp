#include "NativeIokitDpcd.hpp"
#include <kern/clock.h>

namespace YogaTypec {
uint64_t NativeIokitAuxSchedule::nowMicros() {
    uint64_t absolute = 0, nanos = 0;
    clock_get_uptime(&absolute);
    absolutetime_to_nanoseconds(absolute,&nanos);
    return nanos / 1000;
}
bool NativeIokitAuxSchedule::current(Route r, uint64_t e) {
    if (physical.commandLockHeld() || !physical.tryLockAux()) return false;
    const bool valid = physical.authorized(r,e);
    physical.unlockAux();
    return valid;
}
bool NativeIokitAuxSchedule::pause(uint64_t micros) {
    // Caller must be on a sleepable thread with only the outer retained
    // lifecycle/power lease held. Never wait inside shared cursor/AUX lock.
    if (physical.commandLockHeld() || micros != 1000) return false;
    IOSleep(1);
    return !physical.commandLockHeld();
}
bool NativeIokitDpcd::readDpcd(Route r, uint64_t e, uint32_t address,
                               uint8_t *out, size_t count, size_t &actual) {
    actual = 0;
    const auto result = retry.read(r,e,address,out,count);
    if (result.result != NativeAuxResult::Ok || result.actual != count) return false;
    actual = result.actual;
    return true;
}
bool NativeIokitDpcd::writeDpcd(Route r, uint64_t e, uint32_t address,
                                const uint8_t *in, size_t count, size_t &actual) {
    actual = 0;
    const auto result = retry.write(r,e,address,in,count);
    if (result.result != NativeAuxResult::Ok || result.actual != count) return false;
    actual = result.actual;
    return true;
}
} // namespace YogaTypec
