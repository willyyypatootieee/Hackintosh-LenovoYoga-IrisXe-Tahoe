#include <cassert>
#include <initializer_list>
#include "kern_xelp_forcewake_irq.hpp"
int main() {
 for(unsigned f=0;f<4;++f)for(unsigned d=0;d<8;++d) {
  const uint64_t flags=uint64_t(f)<<11;
  assert(XELPForceWakeIRQ::required(flags,d)==(f==1 && d!=0));
 }
 for(bool prior:{false,true}) {
  bool irq=prior;unsigned calls=0,sets=0;
  auto set=[&](bool value){bool old=irq;irq=value;++sets;return old;};
  const auto saved=XELPForceWakeIRQ::protect(set,[&]{assert(!irq);++calls;
   auto nested=XELPForceWakeIRQ::protect(set,[&]{assert(!irq);++calls;});
   assert(!nested && !irq);
  });
  assert(saved==prior && irq==prior && calls==2 && sets==4);
 }
}
