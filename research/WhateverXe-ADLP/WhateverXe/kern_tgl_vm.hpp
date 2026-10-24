// Bounded read-only x86_64 kernel mapping inspection for the two admitted OSes.
#ifndef kern_tgl_vm_hpp
#define kern_tgl_vm_hpp
#include <stdint.h>

namespace TGLVM {
struct Mapping {
 uint64_t entry[4] {};
 uint64_t physical {};
 uint32_t shift {}, patIndex {}, reads {}, status {};
};

// read must fail without faulting. No map changes, speculative dereferences,
// allocation or kernel locking. The caller owns the mapped object's lifetime.
template <typename Read>
Mapping inspect(uint64_t root, uint64_t directBase, uint64_t directEnd,
                uint64_t address, Read read) {
 Mapping out;
 if ((address >> 47) != 0x1FFFF || (root & 4095) ||
     (root >> 47) != 0x1FFFF || directEnd <= directBase) {out.status=1;return out;}
 uint64_t table=root;
 const unsigned shifts[] {39,30,21,12};
 for (unsigned level=0;level<4;++level) {
  const auto slot=((address>>shifts[level])&511)*8;
  if (table>UINT64_MAX-slot || !read(table+slot,out.entry[level])) {out.status=2;return out;}
  ++out.reads;
  const auto value=out.entry[level];
  if (!(value&1)) {out.status=3;return out;}
  if (level==0 && (value&128)) {out.status=4;return out;}
  if (level==3 || (value&128)) {
   out.shift=shifts[level];
   const auto mask=(1ULL<<out.shift)-1;
   // Huge-page PAT occupies bit12, not bit7. Neither is a physical address bit.
   out.physical=(value&0x000FFFFFFFFFF000ULL&~mask)|(address&mask);
   out.patIndex=((value>>3)&3)|((value>>(level==3 ? 5 : 10))&4);
   return out;
  }
  const auto physical=value&0x000FFFFFFFFFF000ULL;
  if (physical>UINT64_MAX-directBase) {out.status=5;return out;}
  table=directBase+physical;
  if (table<directBase || table>=directEnd || directEnd-table<4096) {out.status=5;return out;}
 }
 out.status=6;return out;
}
}
#endif
