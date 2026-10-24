#include "kern_display13_fb_gtt.hpp"
#include <assert.h>
#include <map>
#include <stdio.h>
using namespace Display13FBGTT;
constexpr uint64_t Stolen=0x06000000;
struct IO {
    std::map<uint32_t,uint64_t> entries;
    unsigned failAfter {};
    void writePTE(uint32_t reg,uint64_t value) {entries[reg]=value;}
    uint64_t readPTE(uint32_t reg) {return failAfter && entries.size()==failAfter ? 0 : entries[reg];}
};
int main() {
    IO cursor,dsb;uint32_t written;
    assert(install(cursor,Stolen,0x180000,0x180000,false,0x400000000,0x180000,written,Stolen));
    assert(written==384 && cursor.entries.begin()->first==0x830000 && cursor.entries.begin()->second==0x400000001);
    assert(install(dsb,Stolen+0x180000,0x12000,0x180000,true,0x500000000,0x12000,written,Stolen) && written==18);
    assert(cursor.entries.rbegin()->first<dsb.entries.begin()->first);
    for(auto entry:cursor.entries)assert((entry.second&4095)==1);
    IO reject;
    assert(!install(reject,Stolen-4096,0x180000,0x180000,false,0x400000000,0x180000,written,Stolen));
    assert(!install(reject,Stolen,0x12000,0x180000,true,0x400000000,0x12000,written,Stolen));
    assert(!install(reject,Stolen,0x180000,0x180000,false,0x400000001,0x180000,written,Stolen));
    assert(!install(reject,Stolen,0x180000,0x180000,false,0x400000000,0x17FFFF,written,Stolen));
    assert(!install(reject,Stolen,0x180000,0x180000,false,AddressMask,0x180000,written,Stolen));
    assert(reject.entries.empty() && !written);
    IO failed;failed.failAfter=2;
    assert(!install(failed,Stolen,0x80000,0x80000,false,0x400000000,0x80000,written,Stolen) && written==2);
    for(auto stolen : {0x3C00000ULL,0x4000000ULL,0x6000000ULL,0x8000000ULL}) {
        IO alternate;assert(install(alternate,stolen,0x180000,0x180000,false,0x400000000,0x180000,written,stolen));
        assert(alternate.entries.begin()->first==0x800000+(stolen>>9));
        IO wrong;assert(!install(wrong,stolen+4096,0x180000,0x180000,false,0x400000000,0x180000,written,stolen) && wrong.entries.empty());
    }
    puts("PASS AlderLakeN auxiliary GGTT: disjoint cursor/DSB ranges, system-memory flags, DMA alignment/span/overflow, bounded write-readback failure. CPU only.");
}
