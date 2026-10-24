#!/usr/bin/env python3
"""Exercise the retained copy wrapper's routing and argument/memory contract."""
from pathlib import Path
import re, subprocess, tempfile
repo = Path(__file__).resolve().parents[1]
source = (repo / 'WhateverXe/kern_xe.cpp').read_text()
wrapper = re.search(r'void XE::wrapTGLFastCopy\(.*?\n\}', source, re.S).group()
code = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
using Copy = void(*)(void*,void*,const void*,void*);
struct XE {
 static XE *callbackXE;
 Copy tglSourceCopy, orgTGLFastCopy;
 static void wrapTGLFastCopy(void*,void*,const void*,void*);
};
#define FunctionCast(name, target) (target)
XE *XE::callbackXE;
WRAPPER
static void *expected[4];
static unsigned called, selected;
void check(void *c,void *p,const void *r,void *w,unsigned branch) {
 assert(c==expected[0] && p==expected[1] && r==expected[2] && w==expected[3]);
 ++called;selected=branch;((uint8_t*)p)[0xa0]=0x71;
}
void fast(void*c,void*p,const void*r,void*w){check(c,p,r,w,0);}
void legacy(void*c,void*p,const void*r,void*w){check(c,p,r,w,1);}
int main() {
 XE driver{legacy,fast};XE::callbackXE=&driver;
 unsigned count=0; uint8_t c[16]={}, r[16]={}, w[16]={};
 for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b) {
  uint8_t storage[0x120],before[0x120];memset(storage,0xa5,sizeof(storage));
  auto p=storage+16;p[0xc]=a;p[0x54]=b;memcpy(before,storage,sizeof(storage));
  expected[0]=c;expected[1]=p;expected[2]=r;expected[3]=w;called=0;
  XE::wrapTGLFastCopy(c,p,r,w);
  assert(called==1 && selected==((a==0||a==2||a==3)&&(b==0||b==2||b==3)));
  before[16+0xa0]=0x71;assert(!memcmp(before,storage,sizeof(storage)));
  ++count;
 }
 puts("PASS 65536 tiling pairs: exact native branch, one call, all arguments and callee writes preserved, guards unchanged");
}
'''.replace('WRAPPER', wrapper)
with tempfile.TemporaryDirectory(prefix='tgl-copy-routing-') as tmp:
    p = Path(tmp)
    (p / 'check.cpp').write_text(code)
    subprocess.run(['clang++', '-std=c++14', '-O1', '-g', '-fsanitize=address,undefined', str(p / 'check.cpp'), '-o', str(p / 'check')], check=True)
    subprocess.run([str(p / 'check')], check=True)
