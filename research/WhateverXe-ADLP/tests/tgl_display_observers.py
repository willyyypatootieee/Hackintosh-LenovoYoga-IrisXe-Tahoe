#!/usr/bin/env python3
"""Real scanout/HDCP observers must not read mapped but unpowered MMIO."""
from pathlib import Path
import argparse,re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('--source',type=Path);a=p.parse_args()
s=(a.source or r/'WhateverXe/kern_xe.cpp').read_text()
bodies='\n'.join(re.search(p,s,re.S).group() for p in [r'void XE::tglObserveScanout\(.*?\n\}',r'uint32_t XE::tglObserveHDCP1\(.*?\n\}'])
f=r'''#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include <sys/mman.h>
#define arrsize(a) (sizeof(a)/sizeof((a)[0]))
void clock_get_uptime(uint64_t*p){*p=42;}
void absolutetime_to_nanoseconds(uint64_t a,uint64_t*p){*p=a;}
void *current_thread(){return reinterpret_cast<void*>(1);}
std::vector<uint64_t> record;
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char*,void*p,size_t n){record.assign((uint64_t*)p,(uint64_t*)p+n/8);}
};
struct XE {
 void *tglObservationController=nullptr;
 static void tglObserveScanout(void*,void*,uint32_t,bool,uint32_t);
 static uint32_t tglObserveHDCP1(void*,uint32_t,uint32_t,bool,uint32_t,uint32_t);
};
XE instance,*callbackXE=&instance;
// BODIES
int main(){
 alignas(8) uint8_t fb[0x5000]{},ctl[0x2000]{},plane[0x200]{},access[0x80]{},hdcp[0x80]{},path[0x3700]{};
 auto mmio=mmap(nullptr,0x1000000,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(mmio!=MAP_FAILED);
 instance.tglObservationController=ctl;
 *reinterpret_cast<void**>(fb+0x1D0)=ctl;
 *reinterpret_cast<void**>(ctl+0xC40)=access;
 *reinterpret_cast<void**>(access+0x50)=mmio;
 *reinterpret_cast<uint32_t*>(access+0x60)=0x1000000;
 *reinterpret_cast<void**>(ctl+0xCA0)=(uint8_t*)mmio+0x800000;
 *reinterpret_cast<uint32_t*>(ctl+0xCEC)=0x100000;
 for(unsigned pipe=0;pipe<4;++pipe)for(unsigned event=0;event<8;++event)for(unsigned done=0;done<2;++done){
  *reinterpret_cast<uint32_t*>(plane+0x78)=pipe;
  XE::tglObserveScanout(fb,plane,event,done,7);
  assert(record.size()==256&&record[3]==event&&record[4]==done&&record[5]==7);
  assert(record[255]==0x4D454D31&&record[112]==113);
  for(unsigned i=53;i<=90;++i)assert(record[i]==0);
  for(unsigned i=113;i<255;++i)assert(record[i]==0);
 }
 *reinterpret_cast<void**>(hdcp+0x10)=ctl;*reinterpret_cast<void**>(hdcp+0x20)=path;
 *reinterpret_cast<uint32_t*>(hdcp+0x48)=3;*reinterpret_cast<uint32_t*>(path+0x3640)=2;
 assert(XE::tglObserveHDCP1(hdcp,0,5,false,0,9)==2);
 assert(record[8]==3&&record[9]==2&&record[10]==0&&record[11]==0x4D454D31);
 // Return must not follow a released native object.
 assert(XE::tglObserveHDCP1(mmio,0,5,true,0,2)==2);
 for(unsigned i=12;i<=24;++i)assert(record[i]==0);
 munmap(mmio,0x1000000);
}
'''.replace('// BODIES',bodies)
with tempfile.TemporaryDirectory(prefix='tgl-observers-') as d:
 d=Path(d);(d/'check.cpp').write_text(f)
 subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined',str(d/'check.cpp'),'-o',str(d/'check')],check=True)
 subprocess.run([str(d/'check')],check=True)
print('PASS: 64 scanout boundaries and HDCP released-object return; inaccessible MMIO/GGTT never touched')
