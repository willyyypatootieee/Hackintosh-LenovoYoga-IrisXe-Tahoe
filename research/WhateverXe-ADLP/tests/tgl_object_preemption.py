#!/usr/bin/env python3
"""Execute exact native initialization block on CPU-backed memory, no GPU access."""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
source=(Path(__file__).resolve().parents[1]/'WhateverXe/kern_xe.cpp').read_text()
data=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(data).hexdigest()=='bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
def array(name):return bytes(int(x,0) for x in re.search(r'const uint8_t '+name+r'\[\] \{([^}]+)',source).group(1).split(','))
old,new=array('objectPreemptFind'),array('objectPreemptReplace')
assert old==data[0x2516c:0x25177]
assert old==bytes.fromhex('ba010001008990ec200000')
assert new==bytes.fromhex('ba000001008990ec200000')
code=r'''#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <fstream>
#include <vector>
#include <stdio.h>
extern "C" __attribute__((naked)) void run(void*,void*) {
 asm("pushq %r13\n movq %rsi,%r13\n callq *%rdi\n popq %r13\n retq");
}
int main(int argc,char**argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);std::vector<char>d((std::istreambuf_iterator<char>(f)),{});
 auto m=(uint8_t*)mmap(nullptr,d.size(),7,MAP_ANON|MAP_PRIVATE,-1,0);assert(m!=MAP_FAILED);memcpy(m,d.data(),d.size());m[0x251a1]=0xc3;
 uint8_t owner[0x1300]={},a[0x3000],b[0x3000];const uint64_t flags[]={0,1ULL<<32,~0ULL};
 for(auto value:flags){
  memcpy(owner+0x1190,&value,8);
  for(unsigned i=0;i<sizeof(a);i++)a[i]=b[i]=(i*37+11)&255;
  void *base=a;memcpy(owner+0x1240,&base,8);m[0x2516d]=1;run(m+0x2514b,owner);
  base=b;memcpy(owner+0x1240,&base,8);m[0x2516d]=0;run(m+0x2514b,owner);
  assert(*(uint32_t*)(a+0x20ec)==0x10001 && *(uint32_t*)(b+0x20ec)==0x10000);
  assert((*(uint32_t*)(a+0x2580)^*(uint32_t*)(b+0x2580))==1);
  assert((*(uint32_t*)(b+0x2580)&1)==0 && (*(uint32_t*)(b+0x2580)&0x10000));
  assert(*(uint32_t*)(a+0x20e0)==0x40004000 && *(uint32_t*)(b+0x20e0)==0x40004000);
  for(unsigned i=0;i<sizeof(a);i++)if(i!=0x20ec && i!=0x2580)assert(a[i]==b[i]);
  printf("flags=%llx: only object-preemption bit changes in both selectors; all other memory preserved PASS\n",(unsigned long long)value);
 }
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),sys.argv[1]],check=True)
print('Native CPU initialization test passed; GPU outcome untested')
