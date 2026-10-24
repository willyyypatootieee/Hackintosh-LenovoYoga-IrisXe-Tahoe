// Bounded failure-only reference capture. Candidate SBA interpretation only:
// external calls are retained for offline validation, never silently ignored.
#ifndef kern_tgl_hang_refs_hpp
#define kern_tgl_hang_refs_hpp
#include <stdint.h>
#include <string.h>
namespace TGLHangRefs {
constexpr unsigned MaxPages=24, MaxCalls=4;
enum Kind : uint64_t {Batch=1,Table=2,Surface=4,Sampler=8,Constant=16,Shader=32,Vertex=64,Index=128,Pixels=256};
struct Page {
 uint64_t q[8]; // gpu page, kind mask, copied, qualified, reader status, reserved
 TGLPPGTT::Mapping before,after;
 uint8_t bytes[4096];
};
struct Capture {
 uint64_t q[32]; // version, outer guard, time, batch base, BB, anchor, pages, full, parse status
 // q9..: surface/dynamic/instruction bases, BT, sampler, VS constant,
 // PS constant, VB, IB, index size, first index, count, raw draw header.
 Page pages[MaxPages];
};
inline uint32_t word(const uint8_t *p) {uint32_t v;memcpy(&v,p,4);return v;}
inline uint64_t pair(const uint8_t *p) {uint64_t v;memcpy(&v,p,8);return v;}
inline unsigned length(uint32_t w) {
 const unsigned kind=w>>29,op=(w>>23)&63;
 if (kind==3) return ((w>>27)&3)==1 ? 1 : (w&255)+2;
 if (kind!=0) return 0;
 if (op==0 || op==5 || op==10) return 1;
 if (op==0x1C || op==0x20 || op==0x22 || op==0x24 || op==0x31) return (w&255)+2;
 return 0;
}
// Probe a structural anchor; payload coincidences and unresolved external state
// still require offline validation. No reference is interpreted as executable.
inline bool walkable(const uint8_t *b,unsigned n,unsigned begin,unsigned end) {
 for (unsigned o=begin,steps=0;o<=end && steps++<4096;) {
  if (o==end) return true;
  if (o+4>n) return false;
  unsigned dw=length(word(b+o));
  if (!dw || dw>(n-o)/4 || o+dw*4>end) return false;
  o+=dw*4;
 }
 return false;
}
template<class ReadPage>
void collect(Capture &c,const uint8_t *batch,unsigned n,uint64_t base,uint64_t bb,ReadPage read) {
 c.q[0]=131;c.q[3]=base;c.q[4]=bb;
 c.q[8]=1;
 if (n>16384 || n<88 || bb<base || bb-base>n-4 || ((bb-base)&3)) return;
 const unsigned end=static_cast<unsigned>(bb-base);unsigned anchor=n;
 for (unsigned o=0;o+88<=n && o<=end;o+=4)
  if (word(batch+o)==0x61010014 && walkable(batch,n,o,end)) anchor=o;
 if (anchor==n) return;
 c.q[5]=base+anchor;c.q[8]=0;
 const uint8_t *s=batch+anchor;
 // Missing modify-enable means inherited state, not zero or a usable base.
 if (!(word(s+16)&1) || !(word(s+24)&1) || !(word(s+40)&1)) {c.q[8]=2;return;}
 c.q[9]=pair(s+16)&~4095ULL;c.q[10]=pair(s+24)&~4095ULL;c.q[11]=pair(s+40)&~4095ULL;
 auto add=[&](uint64_t gpu,uint64_t kind)->Page* {
  if (!gpu || gpu>>47) return nullptr;
  const uint64_t page=gpu&~4095ULL;
  for (unsigned i=0;i<c.q[6];++i) if (c.pages[i].q[0]==page) {c.pages[i].q[1]|=kind;return &c.pages[i];}
  if (c.q[6]>=MaxPages) {c.q[7]++;return nullptr;}
  auto &p=c.pages[c.q[6]++];p.q[0]=page;p.q[1]=kind;read(p);return &p;
 };
 auto address=[](uint64_t base,uint64_t off)->uint64_t {return base && off<0x800000000000ULL && base<=0x7FFFFFFFFFFFULL-off ? base+off : 0;};
 auto span=[&](uint64_t gpu,unsigned bytes,uint64_t kind) {
  if (!gpu || !bytes || bytes>4096 || gpu>0x7FFFFFFFFFFFULL-(bytes-1)) return;
  add(gpu,kind);add(gpu+bytes-1,kind);
 };
 uint64_t calls[MaxCalls]={},programs[4]={},vertices[64]={};
 uint32_t strides[64]={};unsigned nc=0;
 bool haveBT=false,haveSampler=false;uint32_t ibBytes=0;int32_t baseVertex=0;
 for (unsigned o=anchor;o<end;) {
  const uint8_t *p=batch+o;const uint32_t w=word(p);unsigned dw=length(w);
  if (!dw || dw>(n-o)/4) {c.q[8]=3;return;}
  if (w==0x18B00101 && dw==3) {if(nc<MaxCalls) calls[nc++]=pair(p+4)&~3ULL;else c.q[23]++;}
  if ((w>>16)==0x782A && dw==2) {c.q[12]=word(p+4)&~31U;haveBT=true;}
  if ((w>>16)==0x782F && dw==2) {c.q[13]=word(p+4)&~31U;haveSampler=true;}
  if ((w>>16)==0x7810 && dw==9) programs[0]=pair(p+4)&~63ULL;
  if ((w>>16)==0x7820 && dw==12) {programs[1]=pair(p+4)&~63ULL;programs[2]=pair(p+32)&~63ULL;programs[3]=pair(p+40)&~63ULL;}
  if ((w>>16)==0x786D && dw==4) {
   // Buffer0 is dynamic-relative in this donor; higher buffers are absolute.
   // Keep the raw flag/length bits in q14/15 for offline mode validation.
   if ((w&0x1F00)==0x100 && (word(p+4)&0xF0000)==0x10000) c.q[14]=pair(p+8);
   if ((w&0x1F00)==0x1000 && (word(p+4)&0xF0000)==0x20000) c.q[15]=pair(p+8);
  }
  if ((w>>16)==0x7808 && dw>=5 && (dw-1)%4==0) for(unsigned i=1;i<dw;i+=4) {
   const unsigned index=word(p+4*i)>>26;vertices[index]=pair(p+4*i+4);strides[index]=word(p+4*i)&4095;
  }
  if ((w>>16)==0x7809 && dw>=3 && (dw-1)%2==0) {
   c.q[28]=0;
   for(unsigned i=1;i<dw;i+=2) if(word(p+4*i)&(1U<<25)) c.q[28]|=1ULL<<(word(p+4*i)>>26);
  }
  if ((w>>16)==0x780A && dw==5) {c.q[17]=pair(p+8);c.q[18]=1ULL<<((word(p+4)>>8)&3);ibBytes=word(p+16);}
  if ((w>>16)==0x7B00 && dw>=7) {c.q[19]=word(p+12);c.q[20]=word(p+8);c.q[21]=w;c.q[22]=word(p+4);baseVertex=static_cast<int32_t>(word(p+24));}
  o+=dw*4;
 }
 c.q[25]=static_cast<uint32_t>(baseVertex);c.q[26]=ibBytes;c.q[27]=nc;
 // Capture external batches first; absent pages remain explicit holes.
 for (unsigned i=0;i<nc;++i) span(calls[i],256,Batch);
 for (unsigned i=0;i<4;++i) if (programs[i]) span(address(c.q[11],programs[i]),512,Shader);
 if (haveSampler) span(address(c.q[10],c.q[13]),256,Sampler);
 if (c.q[14]&31) span(address(c.q[10],c.q[14]&~31ULL),static_cast<unsigned>(c.q[14]&31)*32,Constant);
 if (c.q[15]&31) span(c.q[15]&~31ULL,static_cast<unsigned>(c.q[15]&31)*32,Constant);
 // Candidate VB range only; vertex IDs/index contents determine actual reads.
 for(unsigned i=0;i<64;++i) if(c.q[28]&(1ULL<<i)) {
  if(!c.q[16]) {c.q[16]=vertices[i];c.q[24]=strides[i];}
  span(vertices[i],256,Vertex);
 }
 if ((c.q[22]&256) && !(c.q[21]&1024) && c.q[18]<=4 && c.q[20] && c.q[20]<=2048) {
  const uint64_t off=c.q[19]*c.q[18],bytes=c.q[20]*c.q[18];
  if (off<=ibBytes && bytes<=ibBytes-off && bytes<=4096) span(address(c.q[17],off),static_cast<unsigned>(bytes),Index);
 }
 // 32 entries cover the observed RT0/source17 while retaining bounded scope.
 if (!haveBT) return;
 const uint64_t table=address(c.q[9],c.q[12]);span(table,128,Table);
 for(unsigned i=0;table && i<32;++i) {
  const uint64_t at=table+4*i;auto *t=add(at,Table);
  if (!t || !t->q[3] || (at&4095)>4092) continue;
  const uint32_t off=word(t->bytes+(at&4095));if (!off || (off&63)) continue;
  const uint64_t gpu=address(c.q[9],off);auto *p=add(gpu,Surface);
  if (!p || !p->q[3] || (gpu&4095)>4032) continue;
  const uint8_t *state=p->bytes+(gpu&4095);
  const unsigned type=word(state)>>29;
  if (type==1) add(pair(state+32),Pixels);
 }
}
}
#endif
