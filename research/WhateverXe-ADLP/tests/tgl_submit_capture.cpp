#include "../WhateverXe/kern_tgl_submit_capture.hpp"
#include <cassert>
#include <cstring>
#include <map>

int main() {
 using namespace TGLSubmitCapture;
 static Record r;
 const uint64_t ctx=0xffff900000010000ULL,owner=ctx+0x1000,ring=ctx+0x2000;
 const uint64_t lrcbuf=ctx+0x3000,ringbuf=ctx+0x4000,lrc=ctx+0x10000,cpu=ctx+0x40000;
 std::map<uint64_t,uint64_t> mem {{ctx+0x50,owner},{ctx+0x7C,4},
  {ctx+0x89,0x80402bb319ULL},{ctx+0x58,ctx+0x5000},{ctx+0x98,lrcbuf},
  {ctx+0xB0,ring},{lrcbuf+0x38,lrc},{lrcbuf+0x20,81920},{lrcbuf,ctx+0x6000},
  {ring+0x20,ctx},{ring+0x10,owner},
  {ring+0x80,ringbuf},{ringbuf+0x38,cpu},{ring+0x8C,32768},{ring+0x40,0}};
 auto read=[&](uint64_t p,uint64_t &v) {auto i=mem.find(p);if(i==mem.end())return false;v=i->second;return true;};
 unsigned copies=0;uint32_t head=0,tail=96;
 auto copy=[&](uint64_t p,uint8_t *out,uint64_t length)->uint64_t {
  assert((p==lrc && length==8192) ||
   (p>=lrc+8192 && p<lrc+81920 && length==4096) ||
   (p>=cpu && p<cpu+32768 && length==4096));
  ++copies;std::memset(out,0,length);
  if(p==lrc) {std::memcpy(out+0x1014,&head,4);std::memcpy(out+0x101C,&tail,4);}
  return length;
 };
 auto run=[&] {r=Record{};copies=0;capture(r,ctx,owner,read,copy);};
 run();assert(!r.q[Status] && r.q[Stable] && copies==20 && r.q[ContextID]==4);
 assert(r.q[LRCCopied]==81920 && r.q[LRCPages]==0xfffff);
 assert(r.q[RingCopied]==4096 && r.q[RequestedPages]==1 && r.q[CopiedPages]==1);
 head=32760;tail=8;run();assert(!r.q[Status] && copies==21 && r.q[CopiedPages]==129);
 head=0;tail=96;
 mem[ctx+0x50]=owner+8;run();assert(r.q[Status]==1 && !copies);mem[ctx+0x50]=owner;
 mem[ring+0x20]=ctx+8;run();assert(r.q[Status]==2 && !copies);mem[ring+0x20]=ctx;
 mem[ring+0x8C]=65536;run();assert(r.q[Status]==3 && !copies);
 mem[ring+0x8C]=12288;run();assert(r.q[Status]==3 && !copies);mem[ring+0x8C]=32768;
 mem[ringbuf+0x38]=0x10000;run();assert(r.q[Status]==3 && !copies);mem[ringbuf+0x38]=cpu;
 r=Record{};
 capture(r,ctx,owner,read,[](uint64_t,uint8_t *,uint64_t)->uint64_t {return 0;});
 assert(r.q[Status]==4 && !r.q[Stable]);
 // Replacement during the second copy must not publish a qualified snapshot.
 r=Record{};
 capture(r,ctx,owner,read,[&](uint64_t p,uint8_t *out,uint64_t length)->uint64_t {
  const auto result=copy(p,out,length);if(p==cpu)mem[ctx+0x89]^=4096;return result;
 });
 assert(r.q[Status]==5 && !r.q[Stable]);
 mem[ctx+0x89]^=4096;
 static Record::Aux aux;
 mem[ctx+0xA0]=lrcbuf;mem[lrcbuf+0x20]=16384;
 auto auxCopy=[](uint64_t,uint8_t *out,uint64_t length)->uint64_t {std::memset(out,0,length);return length;};
 captureAux(aux,ctx+0xA0,0x40200000,ctx+0x6000,read,auxCopy);
 assert(!aux.q[13] && aux.q[12] && aux.q[8]==16384);
 aux=Record::Aux{};captureAux(aux,ctx+0xA0,0x40200000,ctx+0x6008,read,auxCopy);
 assert(aux.q[13]==2 && !aux.q[8]);
 mem[ctx+0xA0]=0;aux=Record::Aux{};captureAux(aux,ctx+0xA0,0,ctx+0x6000,read,auxCopy);
 assert(!aux.q[13] && aux.q[12] && !aux.q[1]);
 aux=Record::Aux{};captureAux(aux,ctx+0xA0,0x40200000,ctx+0x6000,read,auxCopy);
 assert(aux.q[13]==2 && !aux.q[12]);
 assert(!kernelRange(UINT64_MAX-4095,4096));
 assert(!kernelRange(0x800000000000ULL,4096));
}
