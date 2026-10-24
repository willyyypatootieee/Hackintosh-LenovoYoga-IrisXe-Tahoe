#include "kern_display13_color.hpp"
#include <assert.h>
#include <stdio.h>
#include <vector>
int main() {
 assert(Display13Color::observationRegister(1,0x49010)==0x49110);
 assert(Display13Color::observationRegister(1,0x4A480)==0x4AC80);
 assert(Display13Color::observationRegister(1,0x70180)==0x71180);
 assert(!Display13Color::observationRegister(1,0x4A404) && !Display13Color::observationRegister(1,0x4A488));
 assert(!Display13Color::observationRegister(3,0x70180));

 for(unsigned pipe=0;pipe<3;++pipe) {
  const auto post=Display13Color::postIndex(pipe),pre=Display13Color::preIndex(pipe);
  assert(Display13Color::registerPair(pipe,pre,pre+4,true));
  assert(Display13Color::registerPair(pipe,post,post+4,false));
  for(unsigned peer=0;peer<3;++peer)if(peer!=pipe) {
   assert(!Display13Color::registerPair(pipe,Display13Color::preIndex(peer),Display13Color::preIndex(peer)+4,true));
   assert(Display13Color::maxChannel(post,Display13Color::postIndex(peer)+0x20)==6);
  }
  for(unsigned j=0;j<6;++j)assert(Display13Color::maxChannel(post,post+(j<3 ? 0x20 : 0x30)+4*(j%3))==j);
  assert(Display13Color::maxChannel(post,post+0x2C)==6 && Display13Color::maxChannel(post,post+0x21)==6);
  assert(!Display13Color::registerPair(pipe,pre,pre+8,true));
 }
 assert(!Display13Color::registerPair(3,0,4,true) && !Display13Color::postIndex(3));
 assert(Display13Color::gamma10MaxToU16(0)==0 && Display13Color::gamma10MaxToU16(1023)==65536);
 assert(Display13Color::gamma10MaxToU16(512)==32800 && Display13Color::gamma10MaxToU16(1024)==UINT32_MAX);
 for(unsigned i=1;i<=1023;++i)assert(Display13Color::gamma10MaxToU16(i)>Display13Color::gamma10MaxToU16(i-1));
 Display13Color::PreGammaStream stream;std::vector<uint32_t> out;
 for(unsigned i=0;i<33;++i)assert(stream.push(i*2048,[&](uint32_t v){out.push_back(v);}));
 assert(stream.push(3*65536,[&](uint32_t v){out.push_back(v);}));
 assert(stream.push(7*65536,[&](uint32_t v){out.push_back(v);}));
 assert(stream.complete() && out.size()==131);
 for(unsigned i=0;i<129;++i)assert(out[i]==i*512);
 assert(out[129]==3*65536 && out[130]==7*65536);
 assert(!stream.push(0,[&](uint32_t v){out.push_back(v);}) && out.size()==131);
 Display13Color::PreGammaStream custom;out.clear();
 for(unsigned i=0;i<35;++i)assert(custom.push(i<33 ? i*i*61 : i==33 ? 65536 : 196608,[&](uint32_t v){out.push_back(v);}));
 assert(custom.complete());for(unsigned i=0;i<33;++i)assert(out[i*4]==i*i*61);
 for(unsigned i=1;i<129;++i)assert(out[i]>=out[i-1]);
 Display13Color::PreGammaStream descending;out.clear();
 for(unsigned i=0;i<35;++i)assert(descending.push(i&1 ? UINT32_MAX : 0,[&](uint32_t v){out.push_back(v);}));
 assert(descending.complete() && out[1]==1073741824 && out[2]==2147483648 && out[3]==3221225471 && out[4]==UINT32_MAX);
 puts("AlderLakeN Display13 pre-CSC fixtures passed: 35-to-131 knots, exact unity, donor/custom knots, extended range, descending/overflow and overrun rejection");
}
