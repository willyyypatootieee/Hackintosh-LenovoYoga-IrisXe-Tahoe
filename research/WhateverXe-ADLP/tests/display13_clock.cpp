#include <cassert>
#include <cstdio>
#include <map>
#include "kern_display13_clock.hpp"
using namespace Display13Clock;
struct IO {
    std::map<uint32_t,uint32_t> regs;
    unsigned reads {};
    bool changed {};
    bool readable(uint32_t reg) const {return regs.count(reg);}
    uint32_t read(uint32_t reg) {return regs.at(reg)^((changed && ++reads>4 && reg==PLL) ? 1U:0U);}
};
int main() {
    // Synthetic register fixtures from the ADLP D0 table, NOT measured S1 PLL.
    const uint32_t frequency[3][5] {{176000,192000,312000,552000,648000},
        {172800,192000,307200,556800,652800},{179200,192000,307200,556800,652800}};
    const uint32_t ratios[3][5] {{22,16,26,46,54},{27,20,32,58,68},{14,10,16,29,34}};
    for(unsigned ref=0;ref<3;++ref)for(unsigned level=0;level<5;++level) {
        const auto f=frequency[ref][level];
        const auto decimal=(2*f+500)/1000-2;
        const auto control=(level==0 ? 1U<<22:0U)|decimal|0x380000;
        auto r=decode(ref<<29,0xC0000000|ratios[ref][level],control,0);
        assert(r.status==Status::Complete && r.cdclkKHz==f && r.refIndex==ref &&
            r.nativeHz()==uint64_t(f)*2000 && r.nativeMaxHz()==uint64_t(frequency[ref][4])*2000);
        assert(r.divider==(level==0 ? 3U:2U));
    }
    // Measured Control=380518 alone is insufficient. These PLL/DSSM inputs
    // are constructed candidates, and must agree with control's decimal.
    auto r=decode(1U<<29,0xC0000044,0x380518,0);
    assert(r.status==Status::Complete && r.cdclkKHz==652800 && r.nativeHz()==1305600000);
    assert(decode(0,0xC0000036,0x380518,0).status==Status::DecimalMismatch);
    assert(decode(3U<<29,0xC0000044,0x380518,0).status==Status::UnsupportedReference);
    assert(decode(1U<<29,0x40000044,0x380518,0).status==Status::Disabled);
    assert(decode(1U<<29,0x80000044,0x380518,0).status==Status::Unlocked);
    assert(decode(1U<<29,0xC0000044,0x380518,0x80000000).status==Status::UnsupportedSquash);
    assert(decode(1U<<29,0xC0000043,0x380518,0).status==Status::UnsupportedFrequency);
    assert(decode(1U<<29,0xC0000044,0x780518,0).status==Status::UnsupportedFrequency);
    assert(decode(UINT32_MAX,0xC0000044,0x380518,0).status==Status::Unavailable);
    // A disabled clock has no live Hz, but its physical-reference table still
    // exposes max Hz for the existing cold-resume path.
    r=decode(0,0,0,0);assert(r.status==Status::Disabled && !r.nativeHz() && r.nativeMaxHz()==1296000000ULL);
    IO io {{{DSSM,1U<<29},{PLL,0xC0000044},{Control,0x380518},{Squash,0}}};
    assert(read(io).status==Status::Complete);
    io.changed=true;assert(read(io).status==Status::Changed);
    io.regs.erase(Squash);assert(read(io).status==Status::NotReady);
    puts("PASS Display13 CDCLK readout: ADLP D0 table, PLL/divider/decimal agreement, native Hz ABI, transition rejection. No clock writes or live qualification.");
}
