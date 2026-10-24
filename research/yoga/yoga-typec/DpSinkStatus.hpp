// DP 1.x link status block (DPCD 0x202..0x207), pure validation only.
#pragma once
#include <stddef.h>
#include <stdint.h>
namespace YogaTypec {
inline bool dpSinkLinkVerified(unsigned lanes, const uint8_t *status, size_t count) {
    if (!status || count != 6 || (lanes != 1 && lanes != 2 && lanes != 4)) return false;
    bool allOnes = true;
    for (unsigned i = 0; i < 6; ++i) allOnes = allOnes && status[i] == 0xff;
    if (allOnes || !(status[2] & 1)) return false; // interlane align
    for (unsigned lane = 0; lane < lanes; ++lane) {
        const auto bits = (status[lane / 2] >> (4 * (lane % 2))) & 7;
        if (bits != 7) return false; // CR done + channel EQ done + symbol locked
    }
    return true;
}
} // namespace YogaTypec
