#include "CursorMath.hpp"
#include "CursorGuards.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>

int main() {
    using namespace CursorMath;
    const uint8_t *guards[] = {guardImage, guardScale, guardState, guardMemory, guardParams, guardNoFault};
    for (uint32_t i = 0; i < 6; i++) {
        uint32_t length = i == 5 ? 32 : 48;
        uint8_t candidate[48] {};
        for (uint32_t j = 0; j < length; j++) candidate[j] = guards[i][j];
        assert(matches(candidate, guards[i], length));
        for (uint32_t j = 0; j < length; j++) {
            candidate[j] ^= 1;
            assert(!matches(candidate, guards[i], length));
            candidate[j] ^= 1;
        }
    }
    assert(!matches(nullptr, guardImage, 48) && !matches(guardImage, nullptr, 48));
    assert(!matches(guardImage, guardImage, 0));
    assert(!dimensions(0, 1) && !dimensions(1, 0));
    assert(dimensions(1, 1) && dimensions(256, 256));
    assert(!dimensions(257, 1) && !dimensions(UINT32_MAX, UINT32_MAX));
    for (uint32_t h = 1; h <= 256; h++) {
        assert(row(0, h) == 0 && row(7, h) == h - 1);
        for (uint32_t i = 0; i < 8; i++) assert(row(i, h) < h);
    }
    assert(!range(0, 1) && !range(0x1000, 10));
    assert(!range(0xffff800000000000ULL, 0));
    assert(range(0xffff800000000000ULL, 512));
    assert(!range(UINTPTR_MAX - 3, 8) && range(UINTPTR_MAX - 3, 4));
    uint32_t offset = 0;
    for (uint32_t d : {64U, 128U, 256U}) {
        uint32_t bytes = d * d * 4;
        assert(bufferOffset(0x3c40000, 0x3c00000, 0x80000, d, offset) && offset == 0x40000);
        assert(bufferOffset(0x1000, 0x1000, bytes, d, offset) && offset == 0);
        assert(!bufferOffset(0x1000, 0x1000, bytes - 1, d, offset));
        assert(!bufferOffset(0x1000, 0x2000, 0x80000, d, offset));
        assert(!bufferOffset(UINT32_MAX, 0x1000, 0x80000, d, offset));
    }
    assert(!bufferOffset(0, 0, 0x80000, 63, offset));
    assert(!bufferOffset(0, 0, 0x1000001, 64, offset));
    assert(convert(0x00ffffff) == 0x0f000000);
    assert(convert(0) == 0 && convert(0xffffffff) == 0xffffffff);
    // All component/alpha pairs, checked against integer division, not copied formula.
    for (uint32_t a = 0; a <= 255; a++) {
        for (uint32_t c = 0; c <= 255; c++) {
            uint32_t pixel = (a << 24) | (c << 16) | (c << 8) | c;
            uint32_t value = !a || a == 255 ? c : c * a / 255;
            uint32_t expected = (a << 24) | (value << 16) | (value << 8) | value;
            if (pixel == 0x00ffffff) expected = 0x0f000000;
            assert(convert(pixel) == expected);
        }
    }
    uint32_t captures = 0;
    for (uint32_t n = 0; n < 1000000; n++) captures += selectedImage(n);
    assert(captures == MaxImages && !selectedImage(UINT32_MAX));
    puts("PASS: 272 single-byte guard mutations, dimensions, offsets, overflow, row bounds, 65536 pixel cases, 32-image ceiling");
}
