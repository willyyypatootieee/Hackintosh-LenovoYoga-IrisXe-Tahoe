#include "kern_xelp_geometry.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace XELPGeometry;
int main() {
    unsigned cases=0;
    for(auto width:{320U,640U,1280U,1920U,2560U,3840U,7680U})
        for(auto height:{240U,480U,720U,1080U,1200U,1440U,2160U,4320U})
            for(auto padding:{0U,1U,16U})for(auto format:{4U,8U}) {
                const auto stride=(width*4+63)/64+padding,size=((height-1)<<16)|(width-1);
                const auto r=linear(0x80000000|(format<<23),stride,size,0,0);
                assert(r.valid && r.width==width && r.height==height && r.pitch==64*stride);
                assert(r.bytes==((uint64_t(64*stride)*height+4095)&~4095ULL));
                auto ht=((width+280-1)<<16)|(width-1),vt=((height+45-1)<<16)|(height-1);
                assert(timing(r,ht,vt));assert(!timing(r,ht^1,vt) && !timing(r,ht,vt^1));
                assert(!linear(0x80000000|(format<<23),0,size,0,0).valid);
                assert(!linear(0x80000000|(format<<23),stride,size,1,0).valid);
                ++cases;
            }
    for(auto forbidden:{1U,2U,1U<<4,1U<<10,1U<<15})assert(!linear(0x84000000|forbidden,120,0x0437077F,0,0).valid);
    assert(!linear(0x04000000,120,0x0437077F,0,0).valid);
    assert(!linear(0x84000000,119,0x0437077F,0,0).valid);
    assert(!linear(0x84000000,120,0xE000E000,0,0).valid);
    printf("PASS %u linear geometry cases: RGB24/30, 320..7680 pixels, 240..4320 lines, padding, actual timing and bounded ranges. CPU only.\n",cases);
}
