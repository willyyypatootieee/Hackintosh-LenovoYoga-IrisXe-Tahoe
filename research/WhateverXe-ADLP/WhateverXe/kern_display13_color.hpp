#ifndef KERN_DISPLAY13_COLOR_HPP
#define KERN_DISPLAY13_COLOR_HPP
#include <stdint.h>
namespace Display13Color {
// Exact donor DisplayPath::init stores these registers using pipe << 11.
// Pipe identity is independent of framebuffer and physical DDI indices.
constexpr uint32_t postIndex(unsigned pipe) {return pipe<3 ? 0x4A400+0x800*pipe : 0;}
constexpr uint32_t preIndex(unsigned pipe) {return pipe<3 ? 0x4A484+0x800*pipe : 0;}
inline bool registerPair(unsigned pipe,uint32_t index,uint32_t data,bool pre) {
    const auto expected=pre ? preIndex(pipe) : postIndex(pipe);
    return expected && index==expected && data==expected+4;
}
inline unsigned maxChannel(uint32_t index,uint64_t reg) {
    // EXT and EXT2 each have three aligned channel endpoints. Do not admit
    // gaps or another pipe's palette/endpoint registers.
    for(unsigned group=0;group<2;++group)for(unsigned ch=0;ch<3;++ch)
        if(reg==uint64_t(index)+(group ? 0x30 : 0x20)+4*ch)return group*3+ch;
    return 6;
}
// Read-only observation banks, independently derived in native Path::init:
// CSC pipe<<8, indexed palette pipe<<11, plane/transcoder pipe<<12.
inline uint32_t observationRegister(unsigned pipe,uint32_t reg) {
    if(pipe>=3 || (reg&3) || reg==0x4A404 || reg==0x4A488)return 0;
    const auto bank=(reg>=0x49000 && reg<0x49100) ? 0x100*pipe :
        (reg>=0x4A400 && reg<0x4A800) ? 0x800*pipe : 0x1000*pipe;
    return reg+bank;
}
// Display12 has 33 uniform knots [0,1] plus two extended-range knots.
// Display13 has 129 uniform knots plus the SAME two extended-range knots.
// Linear subdivision preserves the donor curve instead of replacing it with
// identity or disabling pre-CSC gamma. Preserve full 3.16 values and endpoints.
constexpr uint32_t gamma10MaxToU16(uint32_t value) {
    return value<=1023 ? static_cast<uint32_t>((uint64_t(value)*65536+511)/1023) : UINT32_MAX;
}
struct PreGammaStream {
    uint32_t inputs {},outputs {},previous {};
    template<typename Emit> bool push(uint32_t value,Emit emit) {
        if(inputs>=35)return false;
        if(!inputs || inputs>=33) {emit(value);++outputs;}
        else for(unsigned k=1;k<=4;++k) {
            emit(static_cast<uint32_t>((uint64_t(previous)*(4-k)+uint64_t(value)*k+2)/4));
            ++outputs;
        }
        previous=value;++inputs;return true;
    }
    bool complete() const {return inputs==35 && outputs==131;}
};
}
#endif
