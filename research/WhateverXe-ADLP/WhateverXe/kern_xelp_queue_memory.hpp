// Bounded, read-only qualification of the owned roundtrip probe's program.
#ifndef kern_xelp_queue_memory_hpp
#define kern_xelp_queue_memory_hpp
#include <stdint.h>
namespace XELPQueueMemory {
struct Program {uint64_t instruction {},surface {},dynamic {},bindless {},vertex {},bindings[2] {};};
// This is one admitted producer shape, not a general command decoder. Refuse
// a different layout before following any GPU address obtained from bytes.
inline bool program(const uint8_t *b,unsigned length,Program &p) {
 if(length<2696)return false;
 auto d=[&](unsigned o)->uint32_t {return uint32_t(b[o])|(uint32_t(b[o+1])<<8)|(uint32_t(b[o+2])<<16)|(uint32_t(b[o+3])<<24);};
 auto address=[&](unsigned o)->uint64_t{return (uint64_t(d(o+4))<<32)|(d(o)&~4095U);};
 if(d(0x514)!=0x61010014 || d(0x61C)!=0x782A0000 || d(0x8F8)!=0x782A0000 ||
    d(0x674)!=0x78080003 || d(0x930)!=0x78080003 ||
    d(0x7A8)!=0x7B000008 || d(0x7B0)!=3 || d(0x7B8)!=1 ||
    d(0x950)!=0x7B000008 || d(0x958)!=3 || d(0x960)!=1 ||
    d(0x8DC)!=0 || d(0xA84)!=0x05000000)return false;
 p.instruction=address(0x514+40);p.surface=address(0x514+16);
 p.dynamic=address(0x514+24);p.bindless=address(0x514+64);
 p.vertex=(uint64_t(d(0x680))<<32)|d(0x67C);
 p.bindings[0]=d(0x620);p.bindings[1]=d(0x8FC);
 if(!p.instruction || !p.surface || !p.dynamic || !p.bindless || (p.vertex&4095) ||
    !p.vertex || p.bindings[0]!=0x40 || p.bindings[1]!=0x60)return false;
 if(((uint64_t(d(0x93C))<<32)|d(0x938))!=p.vertex+0x60)return false;
 const uint64_t addresses[] {p.instruction,p.surface,p.dynamic,p.bindless,p.vertex};
 for(auto a:addresses)if(a>=0x800000000000ULL-0x8000)return false;
 return true;
}
// Exact four producer surface states of the 2048x1024 RGBA8 probe. Preserve
// native order (upload source/dest, readback source/dest), not binding order.
inline bool surfaces(const uint8_t *b,unsigned length,uint64_t *gpu) {
 if(length<256)return false;
 for(unsigned i=0;i<4;++i){const auto s=b+i*64;
  auto d=[&](unsigned o)->uint32_t{return uint32_t(s[o])|(uint32_t(s[o+1])<<8)|(uint32_t(s[o+2])<<16)|(uint32_t(s[o+3])<<24);};
  if(d(0)!=(i==0 || i==3?0x20094000U:0x20097000U) ||
     d(4)!=(i==0 || i==2?0xCA000000U:0x8A000000U) || d(8)!=0x03FF01FF ||
     d(12)!=0x1FFF || d(16) || d(20) || d(24) || d(28)!=0x09770000 ||
     d(40) || d(44) || d(48) || d(52) || d(56) || d(60))return false;
  gpu[i]=(uint64_t(d(36))<<32)|d(32);
  if(!gpu[i] || gpu[i]>=0x800000000000ULL-0x800000 || (gpu[i]&255))return false;
 }
 return gpu[1]==gpu[2];
}
}
#endif
