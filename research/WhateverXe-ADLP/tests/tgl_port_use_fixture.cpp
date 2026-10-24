#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
#include "kern_tgl_port_use.hpp"
using Gate=TGLPortUse;using S=Gate::Status;using K=Gate::Kind;
struct IO {std::mutex mutex;void useLock(){mutex.lock();}void useUnlock(){mutex.unlock();}};
struct Fixture {
 IO io;Gate gate;
 Fixture(){for(unsigned d=3;d<9;++d)assert(gate.bind(io,1,100+d,d)==S::Success);}
 Gate::Token enter(unsigned d,unsigned thread=10){auto r=gate.enter(io,1,100+d,d,thread);assert(r.ok());return r.token;}
 Gate::Token claim(unsigned d,unsigned thread=20){auto r=gate.claim(io,1,100+d,d,thread);assert(r.ok());return r.token;}
 void leave(Gate::Token &t,unsigned thread=10){assert(gate.leave(io,1,thread,t)==S::Success);}
 void close(unsigned d){assert(gate.close(io,1,100+d,d)==S::Success);}
 void finish(Gate::Token &t,bool reopen=false,unsigned thread=20){assert(gate.finish(io,1,thread,t,reopen)==S::Success);}
};
void stopNested(){
 Fixture f;auto outer=f.enter(4);assert(f.gate.stop(f.io,1)==S::Success);
 assert(f.gate.enter(f.io,1,104,4,11).status==S::Closed);
 assert(f.gate.enter(f.io,1,105,5,10).status==S::Closed); // same thread != same port
 assert(f.gate.claim(f.io,1,104,4,20).status==S::Busy);
 auto inner=f.enter(4);f.leave(inner);f.leave(outer);
 auto drain=f.claim(4);auto aux=f.enter(4,20);
 assert(f.gate.finish(f.io,1,20,drain,false)==S::Busy);f.leave(aux,20);
 assert(f.gate.finish(f.io,1,20,drain,true)==S::Closed); // stop cannot reopen
 f.finish(drain);assert(f.gate.emptyClosed(f.io,1,104,4));
 assert(f.gate.claim(f.io,1,104,4,20).status==S::Closed);
 assert(f.gate.enter(f.io,1,104,4,20).status==S::Closed);
}
void reservations(){
 unsigned order[]={3,4,5};unsigned cases=0;
 do {
  Fixture f;Gate::Token resident[3];
  for(unsigned d=3;d<=5;++d){resident[d-3]=f.enter(d);assert(f.gate.hold(f.io,1,10,resident[d-3],K::Training)==S::Success);}
  // No executing function remains, but resources must still prevent free.
  assert(f.gate.stop(f.io,1)==S::Success);
  for(unsigned d:order){
   assert(!f.gate.emptyClosed(f.io,1,100+d,d));auto drain=f.claim(d);
   assert(f.gate.finish(f.io,1,20,drain,false)==S::Busy);
   for(unsigned other=3;other<=5;++other)if(other!=d && resident[other-3].valid())
    assert(f.gate.drop(f.io,1,20,drain,resident[other-3])==S::WrongIdentity);
   auto nested=f.enter(d,20);f.leave(nested,20);
   assert(f.gate.drop(f.io,1,20,drain,resident[d-3])==S::Success);f.finish(drain);
   assert(f.gate.emptyClosed(f.io,1,100+d,d));
  }++cases;
 }while(std::next_permutation(order,order+3));
 assert(cases==6);
 // Training -> scanout adoption on a different thread is not an idle gap.
 Fixture f;auto training=f.enter(4);assert(f.gate.hold(f.io,1,10,training,K::Training)==S::Success);
 auto adopt=f.enter(4,11);auto scanout=f.enter(4,11);
 assert(f.gate.hold(f.io,1,11,scanout,K::Scanout)==S::Success);
 assert(f.gate.drop(f.io,1,11,adopt,training)==S::Success);f.leave(adopt,11);
 f.close(4);auto drain=f.claim(4);assert(f.gate.finish(f.io,1,20,drain,false)==S::Busy);
 assert(f.gate.drop(f.io,1,20,drain,scanout)==S::Success);f.finish(drain);
}
void bounds(){
 Fixture f;
 assert(f.gate.bind(f.io,2,104,4)==S::WrongIdentity);
 assert(f.gate.bind(f.io,1,999,4)==S::WrongIdentity);
 assert(f.gate.bind(f.io,1,104,9)==S::Invalid);
 assert(f.gate.enter(f.io,1,104,4,0).status==S::Invalid);
 assert(f.gate.enter(f.io,1,999,4,10).status==S::WrongIdentity);
 Gate::Token items[Gate::WorkCapacity];
 for(auto &t:items)t=f.enter(4);
 assert(f.gate.enter(f.io,1,104,4,10).status==S::Full);
 for(auto &t:items)assert(f.gate.hold(f.io,1,10,t,K::Cleanup)==S::Success);
 f.close(4);auto drain=f.claim(4); // reserved drain + I2C/AUX scopes survive a full work table
 auto i2c=f.enter(4,20),aux=f.enter(4,20);
 assert(f.gate.enter(f.io,1,104,4,20).status==S::Full);
 f.leave(aux,20);f.leave(i2c,20);
 for(auto &t:items)assert(f.gate.drop(f.io,1,20,drain,t)==S::Success);
 auto stale=drain;f.finish(drain,true);assert(!f.gate.emptyClosed(f.io,1,104,4));
 assert(f.gate.finish(f.io,1,20,stale,true)==S::Stale);
 auto a=f.enter(4);stale=a;f.leave(a);auto b=f.enter(4);
 assert(f.gate.leave(f.io,1,10,stale)==S::Stale); // no ABA on reused slot
 assert(f.gate.leave(f.io,1,11,b)==S::WrongIdentity);
 assert(f.gate.hold(f.io,1,10,b,static_cast<K>(99))==S::Invalid);f.leave(b);
 f.gate.sequence=UINT64_MAX;assert(f.gate.enter(f.io,1,104,4,10).status==S::Overflow);
 Gate another;assert(another.bind(f.io,1,104,4)==S::Success);
 assert(another.bind(f.io,1,104,5)==S::WrongIdentity); // a port cannot alias two DDIs
}
void concurrent(){
 // Actual 3-port calls remain concurrent. No lock stays held across a native
 // boundary; stop races with entries, then permits only same-port descendants.
 for(unsigned iteration=0;iteration<100;++iteration){
  Fixture f;std::atomic<unsigned> ready{0};std::atomic<bool> stopping{false};std::vector<std::thread> workers;
  for(unsigned d=3;d<6;++d)workers.emplace_back([&,d]{
   auto outer=f.enter(d,d);++ready;
   while(!stopping.load())std::this_thread::yield();
   auto inner=f.enter(d,d);f.leave(inner,d);f.leave(outer,d);
   assert(f.gate.enter(f.io,1,100+d,d,d).status==S::Closed);
  });
  while(ready.load()!=3)std::this_thread::yield();
  assert(f.gate.stop(f.io,1)==S::Success);stopping=true;
  for(auto &w:workers)w.join();
  for(unsigned d=3;d<6;++d){auto drain=f.claim(d);f.finish(drain);assert(f.gate.emptyClosed(f.io,1,100+d,d));}
 }
}
int main(){stopNested();reservations();bounds();concurrent();puts("PASS port admission: stop/nested calls, persistent training/scanout/cleanup, all 6 three-port drain orders, full capacity cleanup, identity/stale/overflow guards, 100 three-thread stop races (ASan/UBSan). Hardware idle and kernel entry coverage remain host obligations.");}
