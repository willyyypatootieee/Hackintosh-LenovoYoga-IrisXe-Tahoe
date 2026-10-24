#include "../WhateverXe/kern_tgl_vm.hpp"
#include <cassert>
#include <map>

int main() {
 const uint64_t direct=0xffff900000000000ULL, root=direct+0x1000;
 const uint64_t va=0xffffff9000002510ULL;
 std::map<uint64_t,uint64_t> memory;
 auto read=[&](uint64_t p,uint64_t &v) {auto i=memory.find(p);if(i==memory.end())return false;v=i->second;return true;};
 auto slot=[&](uint64_t table,unsigned shift) {return table+((va>>shift)&511)*8;};
 memory[slot(root,39)]=0x2001;
 memory[slot(direct+0x2000,30)]=0x3001;
 memory[slot(direct+0x3000,21)]=0x4001;
 memory[slot(direct+0x4000,12)]=0xd000209b; // 4K, PWT/PCD/PAT all set
 auto run=[&] {return TGLVM::inspect(root,direct,direct+0x10000,va,read);};
 auto a=run();assert(!a.status && a.shift==12 && a.physical==0xd0002510 && a.patIndex==7 && a.reads==4);
 memory[slot(direct+0x3000,21)]=0xd000109b; // 2M, bit12 PAT
 a=run();assert(!a.status && a.shift==21 && a.physical==0xd0002510 && a.patIndex==7 && a.reads==3);
 memory[slot(direct+0x2000,30)]=0xc000109b; // 1G
 a=run();assert(!a.status && a.shift==30 && a.physical==0xc0002510 && a.patIndex==7 && a.reads==2);
 memory[slot(direct+0x2000,30)]=0x3000;assert(run().status==3);
 memory[slot(direct+0x2000,30)]=0x100001;assert(run().status==5);
 memory.erase(slot(root,39));assert(run().status==2);
 memory[slot(root,39)]=0x2081;assert(run().status==4);
 assert(TGLVM::inspect(root,direct,direct+0x10000,0x800000000000ULL,read).status==1);
 assert(TGLVM::inspect(root+1,direct,direct+0x10000,va,read).status==1);
}
