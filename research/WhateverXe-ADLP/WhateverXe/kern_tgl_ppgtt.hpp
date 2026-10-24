// Read-only traversal of the exact TGL donor's 4 KiB software page tables.
// Layout reference: native read@DC02, pageWalk3@E3AE and root getter@D0AE.
#ifndef kern_tgl_ppgtt_hpp
#define kern_tgl_ppgtt_hpp
#include <stdint.h>
namespace TGLPPGTT {
struct Mapping {
 uint64_t descriptors[4] {}, entries[3] {};
 uint64_t rootPhysical {}, pteAddress {}, pte {}, physical {};
 uint32_t status {}, reads {};
};
// Caller supplies a no-fault reader and owns the page-table lifetime. This
// inspects software state; it does not prove the GPU's cached translation.
// Never call native virtual methods, acquire a lock or map physical memory.
template <typename Read>
Mapping inspect(uint64_t table, uint64_t gpu, Read read) {
 Mapping out;
 auto get=[&](uint64_t base,uint64_t offset,uint64_t &value)->bool {
  if (!base || base>UINT64_MAX-offset || !read(base+offset,value)) {
   out.status=1;return false;
  }
  ++out.reads;return true;
 };
 // Restrict to lower canonical addresses used by the admitted donor.
 if (gpu>>47) {out.status=2;return out;}
 uint64_t children;
 if (!get(table,0x40,out.descriptors[0]) || !get(table,0x48,children)) return out;
 if (!out.descriptors[0]) {out.status=3;return out;}
 if (!get(out.descriptors[0],0x18,out.rootPhysical)) return out;
 const unsigned shifts[] {39,30,21};
 for (unsigned level=0;level<3;++level) {
  const uint64_t offset=((gpu>>shifts[level])&511)*32;
  if (!get(children,offset,out.descriptors[level+1])) return out;
  out.entries[level]=children+offset;
  if (!out.descriptors[level+1]) {out.status=3;return out;}
  if (level<2 && !get(out.entries[level],8,children)) return out;
 }
 uint64_t ptes;
 if (!get(out.descriptors[3],0x20,ptes)) return out;
 const uint64_t offset=((gpu>>12)&511)*8;
 if (!get(ptes,offset,out.pte)) return out;
 out.pteAddress=ptes+offset;
 if (!(out.pte&1)) {out.status=4;return out;}
 out.physical=(out.pte&0x7FFFFFF000ULL)|(gpu&4095);
 return out;
}

struct Chain {
 // gpu, status, reads, levels, root, physical, mismatch mask, software status
 uint64_t q[8] {};
 // descriptor, CPU page, declared physical, CPU physical, entry address,
 // GPU-facing entry, index. These are CPU-visible bytes, not GPU cache state.
 uint64_t rows[4][7] {};
};
// Compare owned software descriptors with every actual hardware PDE/PTE and
// with the CPU translation of their backing pages. Never follow a raw PDE.
// Donor expandLevel@DFDA/E2A0/E354 publishes child physical|3 in parent+20.
template <typename Read,typename Physical>
Chain inspectChain(uint64_t table,uint64_t gpu,Read read,Physical physical) {
 Chain out;auto q=out.q;q[0]=gpu;
 const auto m=inspect(table,gpu,read);q[4]=m.rootPhysical;q[7]=m.status;
 if (m.status) {q[1]=1;return out;}
 const unsigned shifts[] {39,30,21,12};
 auto get=[&](uint64_t base,uint64_t offset,uint64_t &value)->bool {
  if ((base>>47)!=0x1FFFF || base>UINT64_MAX-offset || !read(base+offset,value)) return false;
  ++q[2];return true;
 };
 for(unsigned i=0;i<4;++i) {
  auto r=out.rows[i];r[0]=m.descriptors[i];r[6]=(gpu>>shifts[i])&511;
  q[1]=2;
  if (!get(r[0],0x18,r[2]) || !get(r[0],0x20,r[1])) return out;
  q[1]=3;
  if (!r[2] || (r[2]&4095) || (r[1]&4095) || !physical(r[1],r[3])) return out;
  if(r[2]!=r[3])q[6]|=1ULL<<i;
  q[1]=4;
  if(!get(r[1],r[6]*8,r[5]))return out;
  r[4]=r[1]+r[6]*8;++q[3];
  if(!(r[5]&1))q[6]|=1ULL<<(12+i);
  // This exact donor uses four levels of 4KiB pages. Refuse large-page
  // interpretation rather than conflating PAT2 (PTE bit7) with PDE PS.
  if(i<3 && (r[5]&128))q[6]|=1ULL<<(16+i);
  if(i==2 && (r[5]&64))q[6]|=1ULL<<19;
 }
 for(unsigned i=0;i<3;++i)
  if((out.rows[i][5]&0x7FFFFFF000ULL)!=out.rows[i+1][2])q[6]|=1ULL<<(4+i);
 q[5]=(out.rows[3][5]&0x7FFFFFF000ULL)|(gpu&4095);
 if(out.rows[3][5]!=m.pte || q[5]!=m.physical)q[6]|=1ULL<<8;
 q[1]=0;return out;
}
}
#endif
