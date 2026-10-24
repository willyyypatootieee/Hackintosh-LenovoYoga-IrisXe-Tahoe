#!/usr/bin/env python3
"""Execute the real wrapper and exact donor wake code with deterministic IRQ injection.
MMIO/ACK waits and native spinlocks are modeled; no GPU or kernel execution.
"""
import argparse,ctypes as C,hashlib,mmap,pathlib,struct,subprocess,tempfile,sys,re
r=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--donor',type=pathlib.Path);a=p.parse_args()
s=(r/'WhateverXe/kern_xe.cpp').read_text()
start=s.index('void xelpRegularForceWakeProtected(');end=s.index('bool xelpForceWakeIRQSerialize',start);body=s[start:end]
# Regression: a shared wrapper must actually be routed for TGL, not only ADL-N.
route=s[s.index(' const char *wake[]'):s.index(' // r107:',s.index(' const char *wake[]'))]
assert 'if(ok) {' in route and 'if(ok && adlnDetected)' not in route
assert 'protect {wake[0],xelpRegularForceWakeProtected,xelpRegularForceWakeOriginal}' in route
assert 'WhateverXeForceWakeIRQProtected' in route
assert all(x in route for x in ['0x28C2A','0x28C9A','0x28E14','routeMultipleShort'])
assert 'tglCSBForceWake[0](renderWakeOwner,true,1)' in s
assert 'tglCSBForceWake[0](renderWakeOwner,false,1)' in s
if a.donor is None:
 print('PASS: common TGL/ADL-N routing and exact-entry guards; use --donor for native reentry execution')
 sys.exit(0)
data=a.donor.read_bytes();assert hashlib.sha256(data).hexdigest() in ['890735a93a19d3ddda24ff31bda3075dbd145fb7b26c11769f33c4564a8027ac','bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0']
prologue=bytes.fromhex('554889e541574156415541545350');assert data[0x28c2a:0x28c2a+len(prologue)]==prologue
prefix=r'''
#include <cstdint>
#include <cstring>
using Wake=void(*)(void*,bool,unsigned);
using SetIRQ=bool(*)(bool);
static SetIRQ setIRQ;
static uint64_t xelpForceWakeIRQ[9] {};
using boolean_t=bool;
using TGLCSBForceWake=Wake;
static uintptr_t xelpRegularForceWakeOriginal;
#include "kern_xelp_forcewake_irq.hpp"
void xelpRegularForceWakeProtected(void*,bool,unsigned);
static uint64_t ticks;
static bool ml_set_interrupts_enabled(bool v){return setIRQ(v);}
static void clock_get_uptime(uint64_t *p){*p=++ticks;}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t *p){*p=t;}
extern "C" void setup(Wake w,SetIRQ f){xelpRegularForceWakeOriginal=reinterpret_cast<uintptr_t>(w);setIRQ=f;}
extern "C" void run(void *p,bool b,unsigned d){xelpRegularForceWakeProtected(p,b,d);}
extern "C" uint64_t metric(unsigned i){return xelpForceWakeIRQ[i];}
'''
with tempfile.TemporaryDirectory() as tmp:
 tmp=pathlib.Path(tmp);(tmp/'guard.cpp').write_text(prefix+body)
 subprocess.run(['clang++','-I'+str(r/'WhateverXe'),'-std=c++14','-arch','x86_64','-dynamiclib','-O2',str(tmp/'guard.cpp'),'-o',str(tmp/'guard.dylib')],check=True)
 lib=C.CDLL(str(tmp/'guard.dylib'))
 mem=mmap.mmap(-1,0x150000,prot=mmap.PROT_READ|mmap.PROT_WRITE|mmap.PROT_EXEC);base=C.addressof(C.c_char.from_buffer(mem))
 for x,y in [(0x28c2a,0x28e1a),(0x2da26,0x2da6e)]:mem[x:y]=data[x:y]
 mem[0x1000:0x1003]=bytes.fromhex('31c0c3')
 for x in range(0x28c2a,0x28e1a-4):
  if data[x:x+5] in (bytes.fromhex('e800000000'),bytes.fromhex('e900000000')):mem[x+1:x+5]=struct.pack('<i',0x1000-x-5)
 def jump(site,fn):mem[site:site+14]=bytes.fromhex('ff2500000000')+struct.pack('<Q',C.cast(fn,C.c_void_p).value)
 owner=C.create_string_buffer(0x1500);lock=C.create_string_buffer(0x30);C.c_void_p.from_buffer(owner,0x1468).value=C.addressof(lock)
 flags=C.c_uint64.from_buffer(owner,0x1190);flags.value=0x800
 refs=[C.c_int32.from_buffer(owner,0x1290+4*i) for i in range(3)]
 state=dict(enabled=True,depth=0,recursive=0,armed=False,pending=False,irq_calls=0,sets=0)
 calls=[];dispatch=None
 @C.CFUNCTYPE(None,C.c_void_p)
 def acquire(_):
  if state['depth']:state['recursive']+=1
  state['depth']+=1
 @C.CFUNCTYPE(None,C.c_void_p)
 def release(_):state['depth']-=1
 @C.CFUNCTYPE(None,C.c_void_p,C.c_bool,C.c_uint,C.c_uint)
 def low(o,on,domains,client):
  calls.append((on,domains,client))
  if state['armed'] and on:
   state['armed']=False
   if state['enabled']:dispatch()
   else:state['pending']=True
 @C.CFUNCTYPE(C.c_bool,C.c_bool)
 def set_irq(v):
  old=state['enabled'];state['enabled']=bool(v);state['sets']+=1
  if v and state['pending']:
   state['pending']=False;dispatch()
  return old
 for site,stub,cb in [(0x28c9a,0x1100,acquire),(0x28e14,0x1200,release)]:
  jump(stub,cb);mem[site+1:site+5]=struct.pack('<i',stub-site-5)
 jump(0x2d5aa,low)
 Wake=C.CFUNCTYPE(None,C.c_void_p,C.c_bool,C.c_uint)
 normal=Wake(base+0x28c2a);irq=Wake(base+0x2da26)
 lib.setup.argtypes=[Wake,type(set_irq)];lib.setup(normal,set_irq)
 lib.run.argtypes=[C.c_void_p,C.c_bool,C.c_uint];lib.metric.argtypes=[C.c_uint];lib.metric.restype=C.c_uint64
 def interrupt_using(fn):
  def invoke():
   state['irq_calls']+=1;old=state['enabled'];state['enabled']=False
   fn(owner,True,1);fn(owner,False,1);state['enabled']=old
  return invoke
 # Negative control: actual old normal wake is interrupted while owning its lock.
 dispatch=interrupt_using(normal);state['armed']=True
 normal(owner,True,7);normal(owner,False,7)
 # Both the nested request and release enter the same held lock.
 assert state['recursive']==2 and state['depth']==0 and all(x.value==0 for x in refs)
 print('PASS negative control: old path recursively acquires the held lock')
 # Real new wrapper defers that interrupt until lock release; nested normal refs unchanged.
 state['recursive']=0;dispatch=interrupt_using(lib.run);state['armed']=True
 lib.run(owner,True,7);assert state['enabled'] and not state['pending'];lib.run(owner,False,7)
 assert state['recursive']==0 and state['depth']==0 and all(x.value==0 for x in refs)
 # Existing IRQ client2 and normal client1 predecessor holds are not released by the guard.
 irq(owner,True,1);lib.run(owner,True,1);before=len(calls)
 lib.run(owner,True,1);lib.run(owner,False,1)
 assert refs[0].value==1 and len(calls)==before
 lib.run(owner,False,1);irq(owner,False,1)
 assert calls[-1]==(False,1,2) and state['recursive']==0
 # Entry with IRQ already masked must never enable it; delayed IRQ fires only on caller restore.
 state['enabled']=False;state['armed']=True
 lib.run(owner,True,1);lib.run(owner,False,1)
 assert not state['enabled'] and state['pending'];set_irq(True)
 assert state['enabled'] and not state['pending'] and state['recursive']==0
 # Skip flag/zero domains retain the original bypass behavior without changing IF.
 changes=state['sets'];flags.value=0x1800;before=len(calls)
 lib.run(owner,True,7);lib.run(owner,False,7);flags.value=0x800;lib.run(owner,True,0)
 assert state['sets']==changes and len(calls)==before and lib.metric(6)==3
 assert lib.metric(0)==lib.metric(2) and lib.metric(1)>0 and all(x.value==0 for x in refs)
 print('PASS real wrapper + donor: IRQ restoration, deferred interrupt, balanced 3-domain refs, predecessor/client2 hold, bypass; calls=%d masked-entry=%d'%(lib.metric(0),lib.metric(1)))

 # All native domains and both caller IF states: pending IRQ dispatch only after unlock.
 for domains in range(1,8):
  for prior in [False,True]:
   flags.value=0x800;state['enabled']=prior;state['armed']=True;before=state['irq_calls']
   lib.run(owner,True,domains);lib.run(owner,True,domains)
   assert state['enabled']==prior and [x.value for x in refs]==[2 if domains&(1<<i) else 0 for i in range(3)]
   lib.run(owner,False,domains);lib.run(owner,False,domains)
   assert state['enabled']==prior and all(x.value==0 for x in refs) and state['depth']==0
   if not prior:assert state['pending'];set_irq(True)
   assert state['irq_calls']==before+1 and not state['recursive'] and not state['pending']
 assert lib.metric(0)==lib.metric(2)
 print('PASS: 14 domain/prior-IF cases; telemetry reentry deferred until native unlock')
