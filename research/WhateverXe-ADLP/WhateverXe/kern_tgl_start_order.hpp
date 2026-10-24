#pragma once
#include <stdint.h>

// First observation per lifecycle stage / physical DDI. Never reused at stop.
// Publication is release/acquire: a serializer skips an unfinished record.
namespace TGLStartOrder {
constexpr unsigned Slots=35, Words=10;
struct Slot { uint32_t state {}, calls {}; uint64_t data[Words] {}; };
struct Trace {
 Slot slots[Slots] {};
 static unsigned index(unsigned stage,unsigned ddi) {
  return stage>=1 && stage<=8 ? stage-1 :
   (stage>=9 && stage<=11 && ddi<9 ? 8+(stage-9)*9+ddi : Slots);
 }
 Slot *reserve(unsigned stage,unsigned ddi) {
  const auto i=index(stage,ddi); if(i==Slots)return nullptr;
  auto &s=slots[i]; __atomic_add_fetch(&s.calls,1,__ATOMIC_RELAXED);
  uint32_t empty=0;
  return __atomic_compare_exchange_n(&s.state,&empty,1,false,__ATOMIC_ACQUIRE,
   __ATOMIC_RELAXED) ? &s : nullptr;
 }
 static void publish(Slot &s) { __atomic_store_n(&s.state,2,__ATOMIC_RELEASE); }
 void snapshot(uint64_t *out) const {
  out[0]=345;
  for(unsigned i=0;i<Slots;++i) {
   const auto &s=slots[i]; auto p=out+1+i*(Words+1);
   p[0]=__atomic_load_n(&s.calls,__ATOMIC_RELAXED);
   const bool ready=__atomic_load_n(&s.state,__ATOMIC_ACQUIRE)==2;
   for(unsigned j=0;j<Words;++j)p[j+1]=ready ? s.data[j] : 0;
  }
 }
};
}
