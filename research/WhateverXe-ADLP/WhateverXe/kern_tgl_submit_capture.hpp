// Read-only snapshot of buffers retained by the native submit bank.
// Exact TGL donor: context init@7B9D4, ring init@41414, CPU getter@10B60.
#ifndef kern_tgl_submit_capture_hpp
#define kern_tgl_submit_capture_hpp
#include <stdint.h>
#include "kern_tgl_ppgtt.hpp"
namespace TGLSubmitCapture {
enum Word {
 Schema, Time, Submit, BankSlot, Runtime, Streamer, Owner, Context,
 ContextOwner, ContextID, Descriptor, Task, Table, TableClass,
 LRCBuffer, LRCCPU, Ring, RingContext, RingOwner, RingBuffer, RingCPU,
 RingLength, Engine, DescriptorAfter, Stable, LRCCopied, RingCopied,
 Root, SoftwareRoot, Tail, Head, RingGPU, Control, Status, EndTime,
 Checked, Mismatch, FirstGPU, FirstPTE, FirstPhysical, Unknown,
 RequestedPages, CopiedPages, CPURoot, LRCSize, LRCPages, LRCClass, Words=48
};
struct Record {
 uint64_t q[Words] {};
 uint8_t lrc[81920] {}, ring[32768] {};
 // Current CPU physical translation and actual GGTT PTE, including the
 // remainder of the retained context. Absent CPU pages remain unknown.
 uint64_t pages[28][3] {};
 struct Batch {uint64_t q[12] {};uint8_t bytes[4096] {};} batch[2];
 struct Aux {uint64_t q[16] {};uint8_t bytes[16384] {};uint64_t pages[4][3] {};} aux[2];
 // Two BBS pages and eight pages covering the saved 32KiB Aux root VA.
 uint64_t chainOwnership[8] {};
 struct ChainPair {uint64_t qualified {},stable {};TGLPPGTT::Chain before,after;} chains[10];
};
inline bool kernelRange(uint64_t p,uint64_t length) {
 return length && (p>>47)==0x1FFFF && p<=UINT64_MAX-length;
}
// No native calls, mappings, allocation, locks, register reads or GPU writes.
// read/copy must be no-fault. The caller retains the native bank's work item.
template <typename Read,typename Copy>
void capture(Record &r,uint64_t ctx,uint64_t owner,Read read,Copy copy) {
 auto q=r.q;q[Schema]=5;q[Context]=ctx;q[Owner]=owner;
 auto get=[&](uint64_t p,uint64_t offset,uint64_t &v)->bool {
  return kernelRange(p,offset+8) && read(p+offset,v);
 };
 q[Status]=1;
 if (!get(ctx,0x50,q[ContextOwner]) || q[ContextOwner]!=owner ||
     !get(ctx,0x7C,q[ContextID]) || !get(ctx,0x89,q[Descriptor]) ||
     !get(ctx,0x58,q[Task]) || !get(ctx,0x98,q[LRCBuffer]) ||
     !get(ctx,0xB0,q[Ring])) return;
 q[ContextID]&=UINT32_MAX;
 q[Status]=2;
 if (!get(q[LRCBuffer],0x38,q[LRCCPU]) || !get(q[LRCBuffer],0x20,q[LRCSize]) ||
     !get(q[LRCBuffer],0,q[LRCClass]) ||
     !get(q[Ring],0x20,q[RingContext]) || q[RingContext]!=ctx ||
     !get(q[Ring],0x10,q[RingOwner]) || q[RingOwner]!=owner ||
     !get(q[Ring],0x80,q[RingBuffer]) ||
     !get(q[RingBuffer],0x38,q[RingCPU]) ||
     !get(q[Ring],0x8C,q[RingLength]) || !get(q[Ring],0x40,q[Engine])) return;
 q[RingLength]&=UINT32_MAX;q[Engine]&=255;
 q[Status]=3;
 if (q[Engine] || q[LRCSize]<8192 || q[LRCSize]>sizeof(r.lrc) || (q[LRCSize]&4095) ||
     !kernelRange(q[LRCCPU],q[LRCSize]) ||
     !kernelRange(q[RingCPU],q[RingLength]) || q[RingLength]<4096 ||
     q[RingLength]>sizeof(r.ring) || (q[RingLength]&(q[RingLength]-1))) return;
 q[Status]=4;
 q[LRCCopied]=copy(q[LRCCPU],r.lrc,8192);
 if (q[LRCCopied]!=8192) return;
 q[LRCPages]=3;
 for (uint64_t p=8192;p<q[LRCSize];p+=4096) {
  const auto copied=copy(q[LRCCPU]+p,r.lrc+p,4096);q[LRCCopied]+=copied;
  if (copied==4096) q[LRCPages]|=1ULL<<(p/4096);
 }
 auto dword=[&](unsigned offset)->uint32_t {
  const auto p=r.lrc+offset;
  return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
 };
 q[Head]=dword(0x1014);q[Tail]=dword(0x101C);q[RingGPU]=dword(0x1024);
 q[Control]=dword(0x102C);q[Root]=(uint64_t(dword(0x10C4))<<32)|dword(0x10CC);
 const auto head=q[Head]&0x1FFFFC,tail=q[Tail]&0x1FFFF8;
 if (head>=q[RingLength] || tail>=q[RingLength]) return;
 // Unused runtime ring pages can be lazy/unresident. Read only pages touched
 // by the pending interval; absent unrelated pages are not capture failures.
 for (uint64_t p=0;p<q[RingLength];p+=4096) {
  const bool used=head<tail ? (p<tail && p+4096>head) :
   (head>tail && (p+4096>head || p<tail));
  if (!used) continue;
  q[RequestedPages]|=1ULL<<(p/4096);
  const auto copied=copy(q[RingCPU]+p,r.ring+p,4096);q[RingCopied]+=copied;
  if (copied==4096) q[CopiedPages]|=1ULL<<(p/4096);
 }
 if (q[CopiedPages]!=q[RequestedPages]) return;
 q[Status]=5;
 uint64_t back=0,buffer=0,cpu=0;
 if (!get(ctx,0x89,q[DescriptorAfter]) || !get(ctx,0xB0,back) || back!=q[Ring] ||
     !get(ctx,0x98,buffer) || buffer!=q[LRCBuffer] ||
     !get(buffer,0x38,cpu) || cpu!=q[LRCCPU] ||
     !get(q[Ring],0x80,buffer) || buffer!=q[RingBuffer] ||
     !get(buffer,0x38,cpu) || cpu!=q[RingCPU] ||
     q[DescriptorAfter]!=q[Descriptor]) return;
 q[Stable]=1;q[Status]=0;
 if (get(q[Task],0x260,q[Table])) get(q[Table],0,q[TableClass]);
}

// Native POSH (context+A0) and retained per-context WA (owner+13F8) use the
// same shared-buffer subclass as the LRC. Reject a different class rather
// than reading its presumed CPU member. gpu is the saved LRC reference.
template <typename Read,typename Copy>
void captureAux(Record::Aux &r,uint64_t reference,uint64_t gpu,
                uint64_t expectedClass,Read read,Copy copy) {
 auto q=r.q;q[3]=expectedClass;q[6]=gpu;q[7]=gpu&~4095ULL;q[13]=1;
 auto get=[&](uint64_t p,uint64_t offset,uint64_t &v)->bool {
  return kernelRange(p,offset+8) && read(p+offset,v);
 };
 if (!get(reference,0,q[1])) return;
 if (!q[1] && !gpu) {q[12]=1;q[13]=0;return;} // qualified absence
 q[13]=2;
 if (!gpu || !get(q[1],0,q[2]) || !expectedClass || q[2]!=expectedClass ||
     !get(q[1],0x20,q[4]) || !get(q[1],0x38,q[5])) return;
 q[13]=3;
 if (q[4]<4096 || q[4]>sizeof(r.bytes) || (q[4]&4095) ||
     !kernelRange(q[5],q[4]) || (q[5]&4095)) return;
 q[13]=4;q[8]=copy(q[5],r.bytes,q[4]);
 if (q[8]!=q[4]) return;
 uint64_t back=0,cpu=0;
 q[13]=5;
 if (!get(reference,0,back) || back!=q[1] || !get(back,0x38,cpu) || cpu!=q[5]) return;
 q[12]=1;q[13]=0;
}
}
#endif
