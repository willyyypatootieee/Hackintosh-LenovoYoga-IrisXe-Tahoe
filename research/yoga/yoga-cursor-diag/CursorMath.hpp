// Pure bounded arithmetic shared by the kernel diagnostic and Linux tests.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace CursorMath {
inline bool matches(const uint8_t *actual, const uint8_t *expected, size_t length) {
    if (!actual || !expected || !length) return false;
    for (size_t i = 0; i < length; i++) if (actual[i] != expected[i]) return false;
    return true;
}
constexpr uint32_t MaxSide = 256;
constexpr uint32_t SampleRows = 8;
constexpr uint32_t SampleColumns = 16;
constexpr uint32_t MaxImages = 32;

inline bool dimensions(uint32_t width, uint32_t height) {
    return width > 0 && height > 0 && width <= MaxSide && height <= MaxSide;
}
inline bool side(uint32_t value) { return value == 64 || value == 128 || value == 256; }
inline uint32_t row(uint32_t index, uint32_t height) {
    return index * (height - 1) / (SampleRows - 1);
}
inline bool range(uintptr_t address, size_t length) {
    return address >= 0xffff800000000000ULL && length && length - 1 <= UINTPTR_MAX - address;
}
inline bool bufferOffset(uint32_t base, uint32_t allocationBase, uint32_t length,
                         uint32_t dimension, uint32_t &offset) {
    if (!side(dimension) || !length || length > 16 * 1024 * 1024 || base < allocationBase)
        return false;
    offset = (base - allocationBase) & ~3U;
    uint32_t bytes = dimension * dimension * 4;
    return offset <= length && bytes <= length - offset;
}
inline uint32_t convert(uint32_t pixel) {
    if (pixel == 0x00ffffff) return 0x0f000000;
    uint32_t a = pixel >> 24;
    if (!a || a == 255) return pixel;
    uint32_t b = ((pixel & 255) * a * 0x8081U) >> 23;
    uint32_t g = ((((pixel >> 8) & 255) * a * 0x8081U) >> 23) << 8;
    uint32_t r = ((((pixel >> 16) & 255) * a * 0x8081U) >> 23) << 16;
    return (pixel & 0xff000000) | r | g | b;
}
inline bool selectedImage(uint32_t call) {
    return call < 8 || (call >= 8 && (call - 8) % 32 == 0 && (call - 8) / 32 < MaxImages - 8);
}
}
