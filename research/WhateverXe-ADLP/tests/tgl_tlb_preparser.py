#!/usr/bin/env python3
"""Run native donor TLB emitter/writer plus real r133 wrappers on CPU memory.
No GPU execution claim. Includes pending-bit recursion, both native WA branches,
address modes, reservation accounting, ring end and unchanged native payload.
"""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
repo=Path(__file__).resolve().parents[1]
s=(repo/'WhateverXe/kern_xe.cpp').read_text(); b=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(b).hexdigest()=='bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
for name,at in [('tlbSpaceBody',0x8507c),('tlbWriteHead',0x8509c),('tlbReserveCall',0x4177c),('tlbPendingCall',0x41eca)]:
 vals=re.search(r'const uint8_t '+name+r'\[\] \{([^}]+)',s)[1]
 want=bytes(int(v.strip(),0) for v in vals.split(',')); assert b[at:at+len(want)]==want,name
assert '&& flushOK && tlbPreparserOK && hangOK' in s
helpers=s[s.index('uintptr_t tglOrigRCSTLBSpace'):s.index('bool tglRCSTLBSerialize')]
code=r'''#include <stdint.h>
#include <sys/mman.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <vector>
#include <cstdio>
#define FunctionCast(fn,addr) reinterpret_cast<decltype(&fn)>(addr)
HELPERS
static uint8_t *mem;static uint64_t scratch;static uint32_t mode;
static uint8_t storage[4096+64];static uint64_t cookie;
static uint32_t getMode(void*) {return mode;}
static uint64_t getScratch(void*) {return scratch;}
static void *getVirtual(void*) {return storage;}
static void jump(unsigned off,uintptr_t target) {
 uint8_t j[14]={0xff,0x25,0,0,0,0};memcpy(j+6,&target,8);memcpy(mem+off,j,14);
}
template<typename T> static void put(uint8_t *p,unsigned at,T v){memcpy(p+at,&v,sizeof(v));}
int main(int argc,char **argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);
 std::vector<char>d((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
 mem=(uint8_t*)mmap(nullptr,d.size(),7,MAP_PRIVATE|MAP_ANON,-1,0);assert(mem!=MAP_FAILED);
 memcpy(mem,d.data(),d.size());put(mem,0xc8200,(uintptr_t)&cookie);
 jump(0x42364,(uintptr_t)getMode);jump(0x8224,(uintptr_t)getScratch);
 jump(0x10b60,(uintptr_t)getVirtual);
 tglOrigRCSTLBSpace=(uintptr_t)(mem+0x8507c);tglOrigRCSTLBWrite=(uintptr_t)(mem+0x8509c);
 tglRCSWriteBuffer=(uintptr_t)(mem+0x41e7a);
 auto nativeSpace=(uint32_t(*)(void*))tglOrigRCSTLBSpace;
 auto nativeWrite=(void(*)(void*))tglOrigRCSTLBWrite;
 auto write=(bool(*)(void*,const uint32_t*,uint32_t))tglRCSWriteBuffer;
 uint8_t owner[0x1400],ring[0x900],ctx[0x80];uintptr_t vtable[64]={};
 vtable[0x160/8]=(uintptr_t)tglRCSTLBWrite;
 vtable[0x150/8]=(uintptr_t)tglRCSTLBSpace;
 unsigned cases=0;
 for(unsigned wa=0;wa<2;++wa)for(mode=0;mode<2;++mode)
 for(auto addr:{0x40000010ULL,0x123456789abfULL})for(unsigned at:{0U,3968U}){
  scratch=addr;
  auto init=[&](){memset(owner,0,sizeof(owner));memset(ring,0,sizeof(ring));memset(ctx,0,sizeof(ctx));
   memset(storage,0xa5,sizeof(storage));put(owner,0xfd6,wa<<16);
   put(ring,0,(uintptr_t)vtable);put(ring,0x10,(uintptr_t)owner);put(ring,0x20,(uintptr_t)ctx);
   put(ring,0x64,at);put(ring,0x88,128U);put(ring,0x90,4095U);
  };
  init();const auto n=nativeSpace(ring);assert(n==(wa?12:6));nativeWrite(ring);
  std::vector<uint8_t> expected(storage+at,storage+at+n*4);
  assert(*(uint32_t*)(ring+0x64)==at+n*4);assert(*(uint32_t*)(ring+0x88)==128-n*4);
  init();assert(tglRCSTLBSpace(ring)==n+2);
  // Actual native writer consumes pending bit, invokes the hooked virtual TLB
  // method once and then emits the caller's word. Do not fake this recursion.
  put(owner,0x1340,1ULL);ring[0x6d]=1;const uint32_t word=0;
  const auto calls=tglRCSTLBCounters[1];assert(write(ring,&word,1));
  assert(tglRCSTLBCounters[1]==calls+1 && tglRCSTLBCounters[2]==calls+1);
  assert(*(uint64_t*)(owner+0x1340)==0 && ring[0x6d]==0);
  assert(*(uint32_t*)(storage+at)==0x02800101);
  assert(!memcmp(storage+at+4,expected.data(),expected.size()));
  assert(*(uint32_t*)(storage+at+4+n*4)==0x02800100);
  assert(*(uint32_t*)(storage+at+8+n*4)==word);
  assert(*(uint32_t*)(ring+0x64)==at+(n+3)*4);
  assert(*(uint32_t*)(ring+0x88)==128-(n+3)*4);
  for(unsigned j=at+(n+3)*4;j<sizeof(storage);++j)assert(storage[j]==0xa5);
  ++cases;
 }
 printf("PASS: %u native emitter/writer cases; unchanged payload, paired control, space and recursion\n",cases);
}
'''.replace('HELPERS',helpers)
with tempfile.TemporaryDirectory(prefix='tgl-tlb-') as t:
 p=Path(t);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),sys.argv[1]],check=True)
