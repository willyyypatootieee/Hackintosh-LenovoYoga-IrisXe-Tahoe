#!/usr/bin/env python3
"""Check real memory-only recorder/live serializer, including ring reuse."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
bodies='\n'.join(re.search(p,s,re.S).group() for p in [r'void tglAUXRecord\(.*?\n\}',r'static bool tglAUXControlSerialize\(.*?\n\}'])
f=r'''#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <vector>
#include <sys/mman.h>
struct TGLAUXRecord { uint64_t ns,thread;uint32_t word[28]; };
TGLAUXRecord tglAUXFirst[9][256]{},tglAUXLast[9][128]{},tglAUXControl[9][64]{};
uint32_t tglAUXCounts[9]{},tglAUXControlCounts[9]{};
int lock,*tglAUXLock=&lock;bool held=false;
void IOSimpleLockLock(int*) {assert(!held);held=true;}
void IOSimpleLockUnlock(int*) {assert(held);held=false;}
bool tglQuietTraining(){return false;}
void clock_get_uptime(uint64_t*p){static uint64_t t=0;*p=++t;}
void absolutetime_to_nanoseconds(uint64_t a,uint64_t*p){*p=a;}
void *current_thread(){return reinterpret_cast<void*>(1);}
void *IOMalloc(size_t n){assert(!held);return malloc(n);}
void IOFree(void*p,size_t){assert(!held);free(p);}
struct OSSerialize {std::vector<uint8_t> bytes;};
struct OSData {
 std::vector<uint8_t> bytes;
 static OSData *withBytes(void*p,size_t n){assert(!held);auto d=new OSData;d->bytes.assign((uint8_t*)p,(uint8_t*)p+n);return d;}
 bool serialize(OSSerialize *s){assert(!held);s->bytes=bytes;return true;}
 void release(){assert(!held);delete this;}
};
// BODIES
int main(){
 alignas(8) uint8_t port[0x600]{};uint32_t config[]={4};
 *reinterpret_cast<uint32_t**>(port+0x548)=config;
 auto noRead=mmap(nullptr,4096,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(noRead!=MAP_FAILED);
 *reinterpret_cast<void**>(port+0x578)=noRead;
 OSSerialize s;auto ref=reinterpret_cast<void*>(4);
 assert(tglAUXControlSerialize(nullptr,ref,&s));
 assert(s.bytes.size()==16+64*128);
 for(unsigned i=0;i<80;++i){
  *reinterpret_cast<uint32_t*>(port+0x550)=i;
  tglAUXRecord(port,7,false,0,i,0);
  for(unsigned j=0;j<4;++j)tglAUXRecord(port,0,true,0,0,0);
 }
 // No EDID/client-open publication occurred; reads must see the new events.
 assert(tglAUXControlSerialize(nullptr,ref,&s));
 uint32_t h[4];memcpy(h,s.bytes.data(),16);
 assert(h[0]==1&&h[1]==4&&h[2]==400&&h[3]==80);
 for(unsigned i=16;i<80;++i){TGLAUXRecord v;memcpy(&v,s.bytes.data()+16+(i%64)*128,128);
  assert(v.word[0]==i*5+1&&v.word[1]==7&&v.word[5]==i&&v.word[7]==i&&v.word[27]==0x4d454d31);
 }
 assert(!tglAUXControlSerialize(nullptr,reinterpret_cast<void*>(9),&s));
 tglAUXLock=nullptr;assert(!tglAUXControlSerialize(nullptr,ref,&s));
 munmap(noRead,4096);
}
'''.replace('// BODIES',bodies)
with tempfile.TemporaryDirectory(prefix='tgl-aux-live-') as d:
 d=Path(d);(d/'check.cpp').write_text(f)
 subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined',str(d/'check.cpp'),'-o',str(d/'check')],check=True)
 subprocess.run([str(d/'check')],check=True)
print('PASS: actual live serializer, 80 control/320 bulk records, ring wrap, protected MMIO, no allocation under lock')
