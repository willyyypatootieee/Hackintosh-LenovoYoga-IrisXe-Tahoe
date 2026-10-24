#!/usr/bin/env python3
"""Execute the actual CCS bridge against the donor block on x86_64 macOS."""
from pathlib import Path
import re, subprocess, sys, tempfile
repo=Path(__file__).resolve().parents[1]
tmp=tempfile.TemporaryDirectory(prefix="tgl-ccs-")
work=Path(tmp.name)
src=(repo/'WhateverXe/kern_xe.cpp').read_text()
body=re.search(r'extern "C" __attribute__\(\(naked, used\)\) void tglCCSPSBridge\(\) \{.*?\n\}',src,re.S).group()
code=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
extern "C" { uintptr_t tglCCSPSResume; }
BRIDGE
extern "C" __attribute__((naked)) void resumeBridge() { asm("retq"); }
extern "C" __attribute__((naked)) void nativeBlock() {
 asm("shll $6,%eax\n orl $2,%eax\n movw %ax,0xa0(%r15)\n movw $0x1f80,0xa2(%r15)\n retq");
}
// input: edi=type, rsi=packet, rdx=result, rcx=target. Save every observed
// register before any comparison; identical wrapper for native and replacement.
extern "C" __attribute__((naked)) void runBlock(unsigned,void*,void*,void(*)()) {
 asm("pushq %r15\n pushq %rbx\n pushq %r12\n pushq %r13\n pushq %r14\n pushq %rbp\n"
 "movq $0x55667788,%r13\n movq $0x66778899,%r14\n movq $0x778899aa,%rbp\n movq %rdx,%r12\n movq %rsi,%r15\n movl %edi,%eax\n"
 "movq $0x10203040,%rbx\n movq $0x12345678,%r8\n movq $0x22334455,%r9\n"
 "movq $0x33445566,%r10\n movq $0x44556677,%r11\n"
 "callq *%rcx\n"
 "movq %rax,0(%r12)\n movq %rbx,8(%r12)\n movq %rdx,16(%r12)\n"
 "movq %rsi,24(%r12)\n movq %rdi,32(%r12)\n movq %rbp,40(%r12)\n"
 "movq %r8,48(%r12)\n movq %r9,56(%r12)\n movq %r10,64(%r12)\n movq %r11,72(%r12)\n"
 "movq %r13,80(%r12)\n movq %r14,88(%r12)\n movq %r15,96(%r12)\n"
 "pushfq\n popq 104(%r12)\n movq %rsp,112(%r12)\n movq %rcx,120(%r12)\n movq %r12,128(%r12)\n"
 "popq %rbp\n popq %r14\n popq %r13\n popq %r12\n popq %rbx\n popq %r15\n retq");
}
int main() {
 tglCCSPSResume=(uintptr_t)resumeBridge;
 uint8_t packet[256], native[256]; uint64_t regs[17], expected[17];
 for(unsigned t=0;t<4;t++) {
  memset(packet,0xa5,sizeof(packet));
  runBlock(t,packet,regs,nativeBlock);
  assert(regs[15]==(uintptr_t)nativeBlock); regs[15]=0;
  memcpy(native,packet,sizeof(packet)); memcpy(expected,regs,sizeof(regs));
  memset(packet,0xa5,sizeof(packet));
  runBlock(t,packet,regs,tglCCSPSBridge);
  assert(regs[15]==(uintptr_t)tglCCSPSBridge); regs[15]=0;
  assert(!memcmp(expected,regs,sizeof(regs)));
  if(t==2) native[0xa1]|=1;
  assert(!memcmp(native,packet,sizeof(packet)));
  printf("mode%u PS=%08x: packet/ABI PASS\n",t,*(uint32_t*)(packet+0xa0));
 }
}
'''.replace('BRIDGE',body)
(work/'test.cpp').write_text(code)
# Optional exact Apple donor: never checked in with this test.
if len(sys.argv)>1:
 donor=Path(sys.argv[1]).read_bytes()
 for name,off in [('ccsPSBlock',0x8d056),('ccsPSType',0x8d02b)]:
  raw=re.search(r'const uint8_t '+name+r'\[\] \{([^}]+)',src).group(1)
  data=bytes(int(x.strip(),0) for x in raw.split(','))
  assert donor[off:off+len(data)]==data
 print('Exact donor site guards PASS',flush=True)
subprocess.run(['clang++','-arch','x86_64','-O2',str(work/'test.cpp'),'-o',str(work/'test')],check=True)
subprocess.run([str(work/'test')],check=True)
