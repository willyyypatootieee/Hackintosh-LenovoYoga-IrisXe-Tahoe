from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
f='\n'.join(re.search(p,s,re.S).group() for p in [r'volatile uint8_t \*XE::tglClockOrderTarget\(.*?\n\}',r'void XE::wrapTGLDisableClocks\(.*?\n\}'])
tx=s[s.index('struct TGLClockOffTransaction {'):s.index('} tglClockOffTransaction;')+len('} tglClockOffTransaction;')]
code=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
void *current_thread(){return (void*)1;}unsigned delays=0,writes=0,natives=0;uint32_t lastReg;
void IODelay(unsigned n){assert(n==8);++delays;}
struct XE {
 void *tglObservationController;uintptr_t tglClockOffCaller=1,orgTGLPathLookup,orgTGLDisableClocks;
 bool tglLegacyTCEligible(unsigned d){return d>=3&&d<=8;}
 static volatile uint8_t *tglClockOrderTarget(void*,void*,void*,void*);
 static void wrapTGLDisableClocks(void*,void*,void*);
 static void *wrapTGLPathLookup(void*);
 static void wrapTGLRegisterWrite(void *a,unsigned long reg,uint32_t value){++writes;lastReg=reg;auto b=*reinterpret_cast<uint8_t**>((uint8_t*)a+0x50);memcpy(b+reg,&value,4);}
};
XE obj,*callbackXE=&obj;
TX
FUNCTIONS
alignas(8) uint8_t c[0xd00],p[0x600],d[0x3700],fb[0x4b00],a[0x100],mmio[0x16a000];uint32_t cfg[6];
template<class T>void put(void*o,unsigned n,T v){memcpy((uint8_t*)o+n,&v,sizeof(v));}
void *lookup(void*){return d;}void native(void*,void*,void*){++natives;}
uint32_t &reg(unsigned n){return *reinterpret_cast<uint32_t*>(mmio+n);}
int main(){
 unsigned cases=0;
 for(unsigned ddi=3;ddi<=8;++ddi)for(unsigned pipe=0;pipe<3;++pipe)for(unsigned fault=0;fault<5;++fault){
  memset(c,0,sizeof(c));memset(p,0,sizeof(p));memset(d,0,sizeof(d));memset(fb,0,sizeof(fb));memset(a,0,sizeof(a));memset(mmio,0,sizeof(mmio));
  obj.tglObservationController=c;obj.orgTGLPathLookup=(uintptr_t)lookup;obj.orgTGLDisableClocks=(uintptr_t)native;
  cfg[0]=ddi;cfg[2]=1;put(p,0x548,cfg);put(p,0x550,uint32_t(4));put(p,0x578,a);put(c,0xc40,a);
  put(d,0x3640,pipe);put(d,0x3648,p);put(d,0x32b0,fb);put(fb,0x1d0,c);put(fb,0x4a40,a);put(a,0x50,mmio);put(a,0x60,uint32_t(sizeof(mmio)));
  const unsigned clock=0x46140+4*pipe,buffer=0x64000+0x100*ddi;
  reg(clock)=(ddi+1)<<28;reg(buffer)=0x80;
  if(fault==1)reg(buffer)|=0x80000000;if(fault==2)reg(0x70008+0x1000*pipe)=0x40000000;
  if(fault==3)reg(clock)=((ddi==8?3:ddi+1)+1)<<28;if(fault==4)reg(buffer)=0;
  tglClockOffTransaction={};delays=writes=natives=0;XE::wrapTGLDisableClocks(c,p,nullptr);
  assert(natives==1&&writes==unsigned(fault==0||fault==4));
  if(writes)assert(lastReg==clock&&reg(clock)==0);assert(delays==unsigned(fault==4));++cases;
 }
 // Invalid pipe stays native. An owned deferred write also survives failed idle checks.
 put(d,0x3640,uint32_t(3));assert(!XE::tglClockOrderTarget(c,p,d,fb));
 put(d,0x3640,uint32_t(2));reg(0x70008+0x2000)=0x40000000;
 tglClockOffTransaction.thread=1;tglClockOffTransaction.controller=c;tglClockOffTransaction.port=p;
 tglClockOffTransaction.path=d;tglClockOffTransaction.access=a;tglClockOffTransaction.clockReg=0x46148;
 tglClockOffTransaction.value=0x123;tglClockOffTransaction.pending=true;
 writes=0;XE::wrapTGLDisableClocks(c,p,nullptr);assert(writes==1&&reg(0x46148)==0x123&&!tglClockOffTransaction.pending);
 printf("PASS %u actual clock-order port/pipe/idle cases; invalid pipe and deferred native-write fallback\n",cases);
}
'''.replace('TX',tx).replace('FUNCTIONS',f)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code);subprocess.run(['clang++','-std=c++14','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
