#include "../WhateverXe/kern_xelp_queue_memory.hpp"
#include <cassert>
#include <cstring>
#include <vector>
int main() {
 using namespace XELPQueueMemory;
 std::vector<uint8_t> b(4096),s(256);
 auto word=[&](unsigned o,uint32_t v){memcpy(b.data()+o,&v,4);};
 word(0x514,0x61010014);word(0x61C,0x782A0000);word(0x8F8,0x782A0000);
 word(0x674,0x78080003);word(0x930,0x78080003);
 word(0x7A8,0x7B000008);word(0x7B0,3);word(0x7B8,1);word(0x950,0x7B000008);word(0x958,3);word(0x960,1);word(0xA84,0x05000000);
 word(0x53C,0x46681461);word(0x524,0x46801441);word(0x52C,0x46881441);word(0x554,0x468D1441);
 word(0x67C,0x46671000);word(0x938,0x46671060);word(0x620,0x40);word(0x8FC,0x60);
 const uint32_t bases[] {0x43393000,0x44514000,0x44514000,0x43C53100};
 for(unsigned i=0;i<4;++i){const uint32_t v[16] {(i==0 || i==3)?0x20094000U:0x20097000U,
   (i==0 || i==2)?0xCA000000U:0x8A000000U,0x03FF01FF,0x1FFF,0,0,0,0x09770000,bases[i],0,0,0,0,0,0,0};memcpy(s.data()+i*64,v,64);}
 Program p;assert(program(b.data(),b.size(),p));
 assert(p.instruction==0x46681000 && p.surface==0x46801000 && p.vertex==0x46671000);
 assert(!program(b.data(),2695,p));assert(program(b.data(),2696,p));
 auto mutate=[&](unsigned o,uint32_t value){auto copy=b;memcpy(copy.data()+o,&value,4);assert(!program(copy.data(),copy.size(),p));};
 mutate(0x514,0x61010013);mutate(0x8DC,0x05000000);mutate(0x7B0,0);mutate(0x8FC,0x40);mutate(0x938,0x46671100);
 uint64_t gpu[4];assert(surfaces(s.data(),s.size(),gpu));assert(gpu[0]==0x43393000 && gpu[1]==0x44514000 && gpu[3]==0x43C53100);
 assert(!surfaces(s.data(),255,gpu));auto copy=s;copy[0x20]=1;assert(!surfaces(copy.data(),copy.size(),gpu));
 copy=s;copy[0xA2]^=1;assert(!surfaces(copy.data(),copy.size(),gpu));
}
