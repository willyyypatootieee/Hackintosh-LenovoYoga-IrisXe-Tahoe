#!/usr/bin/env python3
"""Execute donor render-tail emitter on CPU memory; no GPU or completion proof."""
from pathlib import Path
import hashlib,re,subprocess,sys,tempfile
repo=Path(__file__).resolve().parents[1]
source=(repo/'WhateverXe/kern_xe.cpp').read_text()
data=Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(data).hexdigest() in {
 '890735a93a19d3ddda24ff31bda3075dbd145fb7b26c11769f33c4564a8027ac',
 # Same donor with deployed r98 import aliases; emitter bytes guarded below.
 'bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0',
}
def arrays(name):
 text=re.search(r'const uint8_t '+name+r'\[\]\[10\] \{(.*?)\};',source,re.S).group(1)
 return [bytes(int(v.strip(),0) for v in row.split(',')) for row in re.findall(r'\{([^}]+)\}',text)]
find,replace=arrays('flushFind'),arrays('flushReplace')
sites=[int(v.strip(),0) for v in re.search(r'const uint32_t flushSites\[\] \{([^}]+)',source).group(1).split(',')]
assert sites==[0x84e5d,0x85406,0x51a20,0x51a3a]
for off,old,new in zip(sites,find,replace):
 assert data[off:off+10]==old
 expected=bytearray(old)
 if off!=0x51a3a:
  expected[3]|=2;expected[7]|=0x20 # HDC + DepthStall
  if off==0x51a20: expected[9]|=0x10 # retained r128 tile flush
 else:
  expected[3]|=2 # HDC
  expected[6]=(expected[6]&~8)|4 # constant -> state invalidation
  expected[9]|=0x20 # command cache invalidation for state redirect
 assert bytes(expected)==new
assert data[0x51a3a:0x51a44]==bytes.fromhex('48bb0400007a98441000')
with tempfile.TemporaryDirectory(prefix='tgl-render-flush-') as tmp:
 p=Path(tmp)
 code=r'''
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fstream>
#include <vector>
extern "C" __attribute__((naked)) void runBlock(void*,uint64_t,void*) {
 asm("pushq %rbp\n pushq %rbx\n pushq %r14\n subq $128,%rsp\n"
 "leaq 128(%rsp),%rbp\n movq %rsi,%rax\n movq %rdx,%r14\n callq *%rdi\n"
 "addq $128,%rsp\n popq %r14\n popq %rbx\n popq %rbp\n retq");
}
int main(int argc,char**argv) {
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);
 std::vector<char>d((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
 auto mem=(uint8_t*)mmap(nullptr,d.size(),PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0);
 assert(mem!=MAP_FAILED);memcpy(mem,d.data(),d.size());mem[0x51a6a]=0xc3;
 const uint8_t first[]={FIRST},old[]={OLD},patched[]={PATCHED};
 const uint64_t addresses[]={0x40000010ULL,0x123400001010ULL,0x123456789abfULL,0xfffffffffffcULL};
 for(auto address:addresses) {
  uint32_t a[20],b[20];memset(a,0xa5,sizeof(a));memset(b,0xa5,sizeof(b));
  memcpy(mem+0x51a20,first,10);memcpy(mem+0x51a3a,old,10);runBlock(mem+0x519e8,address,a);
  memcpy(mem+0x51a3a,patched,10);runBlock(mem+0x519e8,address,b);
  assert(a[0]==0x7a000204 && a[1]==0x50107221);
  assert(a[6]==0x7a000004 && a[7]==0x00104498);
  assert(a[2]==uint32_t(address+16)&~3U);
  assert(a[3]==uint32_t((address+16)>>32));
  assert(a[8]==a[2] && a[9]==a[3]);
  // Relative to r131 only the specified second-packet bits may change.
  assert(b[6]==0x7a000204 && b[7]==0x20104494);
  for(unsigned i=0;i<20;++i) if(i!=6 && i!=7) assert(a[i]==b[i]);
  // The preceding stalling packet satisfies the state-invalidate ordering.
  assert((b[1]&(1U<<20)) && (b[7]&4) && !(b[7]&8));
  printf("scratch=%llx: r131 -> r134 cache alternative; addresses/order/suffix preserved PASS\n",(unsigned long long)address);
 }
 munmap(mem,d.size());
}
'''.replace('FIRST',','.join(hex(v) for v in replace[2])).replace('OLD',','.join(hex(v) for v in find[-1])).replace('PATCHED',','.join(hex(v) for v in replace[-1]))
 code=code.replace('a[2]==uint32_t(address+16)&~3U','a[2]==(uint32_t(address+16)&~3U)')
 (p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-arch','x86_64','-std=c++14','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),sys.argv[1]],check=True)
print('All four exact source guards/replacements PASS; GPU effect untested')
