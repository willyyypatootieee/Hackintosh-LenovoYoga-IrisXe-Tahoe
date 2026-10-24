// Concrete DPCD transport for the trainer callbacks; outer native leases required.
#pragma once
#include "NativeIokitAux.hpp"
#include "NativeAuxRetry.hpp"

namespace YogaTypec {
class NativeIokitAuxSchedule {
    NativeIokitAux &physical;
public:
    explicit NativeIokitAuxSchedule(NativeIokitAux &p) : physical(p) {}
    uint64_t nowMicros();
    bool current(Route r, uint64_t e);
    bool pause(uint64_t micros);
};
class NativeIokitDpcd {
    NativeAuxCommand<NativeIokitAux> command;
    NativeIokitAuxSchedule schedule;
    NativeAuxRetry<NativeAuxCommand<NativeIokitAux>,NativeIokitAuxSchedule> retry;
public:
    explicit NativeIokitDpcd(NativeIokitAux &physical)
        : command(physical), schedule(physical), retry(command,schedule) {}
    NativeIokitDpcd(const NativeIokitDpcd &) = delete;
    NativeIokitDpcd &operator=(const NativeIokitDpcd &) = delete;
    bool writeIntentObserved() const { return retry.writeIntentObserved(); }
    bool readDpcd(Route r, uint64_t e, uint32_t address, uint8_t *out, size_t count, size_t &actual);
    bool writeDpcd(Route r, uint64_t e, uint32_t address, const uint8_t *in, size_t count, size_t &actual);
};
} // namespace YogaTypec
