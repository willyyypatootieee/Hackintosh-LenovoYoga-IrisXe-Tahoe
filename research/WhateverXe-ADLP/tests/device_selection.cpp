#include "kern_xelp_platform.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
using namespace XELP;
int main() {
 unsigned implemented=0;
 for(const auto &entry:DeviceTable) {
  const uint32_t pci=uint32_t(entry.device)<<16|0x8086;
  auto p=identify(pci,0);
  assert(p.identity==pci && p.effectiveIdentity==pci && !p.overridden());
  assert(p.family==entry.family && p.display==entry.display && p.gt==entry.backend);
  assert(!strcmp(model(p),entry.model));
  // 46A6 is a strict D0-only profile; table presence alone is insufficient.
  const bool expected=entry.backend!=GT::Unknown &&
   (entry.backend!=GT::ADLPGT2 || entry.device!=0x46A6 || p.revision==0x0c);
  assert(p.admitted()==expected);
  implemented+=p.admitted();
  const uint32_t value=entry.device;
  assert(same(p,select(pci,0,true,&value,4)));
  // Every catalogued backend can be selected for an unknown physical ID.
  auto f=select(0x12348086,0,true,&value,4);
  assert(f.overridden() && f.identity==0x12348086 && f.effectiveIdentity==pci);
  assert(f.admitted()==p.admitted());
  assert(!strcmp(model(f),entry.model));
  auto nonIntel=select(0x123410DE,0,true,&value,4);assert(!nonIntel.admitted());
 }
  // This table walk uses revision 0. The 46A6 D0 profile is covered below.
  assert(implemented==8);
 uint32_t id=0x46D2;
 assert(select(0x12348086,0,true,&id,4).admitted());
 assert(!select(0x12348086,1,true,&id,4).admitted()); // Keep physical stepping guard.
 assert(!select(0x12348086,0,false,nullptr,0).admitted());
 for(size_t n:{size_t(0),size_t(1),size_t(2),size_t(3),size_t(5),size_t(8)})
  assert(!select(0x9A498086,0,true,&id,n).admitted());
  assert(!select(0x9A498086,0,true,nullptr,4).admitted());
 for(uint32_t bad:{0U,0xFFFFU,0x808646D2U,0x1234U})
  assert(!select(0x9A498086,0,true,&bad,4).admitted());
  auto p=select(0x46A68086,0,true,&id,4);assert(p.admitted() && p.family==Family::AlderLakeN);
 assert(!strcmp(model(p),"Intel Iris Xe Graphics")); // Name is physical, backend is overridden.
 auto q=p;q.effectiveIdentity=0x46D18086;assert(!same(p,q));
 ProbeConfig c;c.profile=p;c.combo=c.comboBackend=3;assert(c.valid());
  c.abi=2;assert(!c.valid());c.abi=4;c.profile.gt=GT::TGLGT2;assert(!c.valid());
  auto adlp=identify(0x46A68086,0x0c);
  assert(adlp.admitted() && adlp.family==Family::AlderLakeP && adlp.gt==GT::ADLPGT2);
  assert(!identify(0x46A68086,0).admitted() && !identify(0x46A68086,0x0d).admitted());
  auto adlpOverride=identify(0x12348086,0x0c,0x46A68086);assert(!adlpOverride.admitted());
  puts("PASS device table, physical/effective identity, strict override bytes/vendor/revision, model choice and ABI4");
}
