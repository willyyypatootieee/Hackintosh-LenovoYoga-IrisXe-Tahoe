#include "kern_tgl_start_order.hpp"
#include <cassert>
#include <thread>
#include <vector>
#include <cstdio>
int main() {
 TGLStartOrder::Trace t;
 uint64_t out[1+35*11];
 assert(!t.reserve(0,0));assert(!t.reserve(12,0));assert(!t.reserve(9,9));
 auto s=t.reserve(9,4);assert(s);s->data[0]=77;
 t.snapshot(out);assert(out[1+(8+4)*11]==1);assert(out[2+(8+4)*11]==0);
 TGLStartOrder::Trace::publish(*s);assert(!t.reserve(9,4));
 t.snapshot(out);assert(out[2+(8+4)*11]==77);
 std::vector<std::thread> workers;
 for(unsigned n=0;n<8;++n)workers.emplace_back([&t,n]{
  for(unsigned j=0;j<1000;++j) {
   auto p=t.reserve(10,5);if(p) {
    for(unsigned k=0;k<10;++k)p->data[k]=n+1;
    TGLStartOrder::Trace::publish(*p);
   }
   uint64_t a[1+35*11];t.snapshot(a);auto row=a+2+(8+9+5)*11;
   for(unsigned k=1;k<10;++k)assert(row[k]==row[0]);
  }
 });
 for(auto &w:workers)w.join();t.snapshot(out);
 assert(out[1+(8+9+5)*11]==8000);
 assert(out[2+(8+9+5)*11]>0);
 for(unsigned stage=1;stage<=8;++stage)assert(t.reserve(stage,9));
 puts("start-order: invalid slots, unpublished snapshot, immutable first record and 8000 concurrent events passed");
}
