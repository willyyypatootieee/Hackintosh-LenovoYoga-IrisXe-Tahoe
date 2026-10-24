#!/usr/bin/env python3
"""Execute the native L3-control MMIO-copy loop on CPU memory for both tables."""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile,struct
s=(Path(__file__).resolve().parents[1]/'WhateverXe/kern_xe.cpp').read_text()
d=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(d).hexdigest()=='bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0'
assert d[0xff18:0xff1f]==bytes.fromhex('488d15b1010900')
assert 0xff1f+struct.unpack_from('<i',d,0xff1b)[0]==0xa00d0
assert struct.unpack_from('<32I',d,0xa00d0)==tuple([0x100010]*16+[0x300030]*12+[0x100010]*4)
block=re.search(r'uint32_t l3Find\[32\],l3Replace\[32\];.*?\n\t\t\t}',s,re.S)[0]
code=bytearray(d[0xff16:0xff36]);struct.pack_into('<i',code,5,512-9)
cpp=r'''#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
extern "C" __attribute__((naked)) void run(void*,void*) {asm("movq %rsi,%rax\ncallq *%rdi\nretq");}
int main(){
 TABLE
 const uint8_t loop[]={LOOP};
 auto m=(uint8_t*)mmap(nullptr,4096,7,MAP_ANON|MAP_PRIVATE,-1,0);assert(m!=MAP_FAILED);
 memcpy(m,loop,sizeof(loop));m[sizeof(loop)]=0xc3;
 uint8_t regs[0xc000],expected[0xc000];
 for(int candidate=0;candidate<2;++candidate){
  memset(regs,0xA5,sizeof(regs));memcpy(expected,regs,sizeof(regs));
  auto table=candidate?l3Replace:l3Find;memcpy(m+512,table,128);run(m,regs);
  for(unsigned i=0;i<32;++i){
   auto value=*(uint32_t*)(regs+0xb020+4*i);assert(value==table[i]);
   for(unsigned k=0;k<2;++k)assert(((value>>(k*16+4))&3)==(candidate?1:((i>=16&&i<28)?3:1)));
  }
  memcpy(expected+0xb020,table,128);assert(!memcmp(regs,expected,sizeof(regs)));
 }
 puts("PASS native 32-register loop: baseline/candidate policies, all 64 indices UC in candidate, every other MMIO byte unchanged");
}'''.replace('TABLE',block).replace('LOOP',','.join(map(str,code)))
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.cpp').write_text(cpp)
 subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
