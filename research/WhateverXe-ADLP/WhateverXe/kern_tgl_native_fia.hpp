// Coordinate adapter for the qualified donor HAL mode transaction.
#ifndef kern_tgl_native_fia_hpp
#define kern_tgl_native_fia_hpp
#include "kern_tgl_port_map.hpp"
#include <stddef.h>

struct TGLNativeFiaContext {
 uintptr_t owner {}; void *port {}; uint32_t ddi {};
 bool matches(uintptr_t thread,void *p) const {
  return thread && __atomic_load_n(&owner,__ATOMIC_ACQUIRE)==thread && port==p;
 }
};
// The existing recursive mode mutex serializes writers. Only the owning
// thread reads port/ddi; other threads stop at the atomic owner check.
struct TGLNativeFiaScope {
 TGLNativeFiaContext &ctx; uintptr_t previousOwner; void *previousPort; uint32_t previousDDI; bool active;
 TGLNativeFiaScope(TGLNativeFiaContext &c,uintptr_t thread,void *p,uint32_t d,bool enable):
  ctx(c),previousOwner(0),previousPort(nullptr),previousDDI(0),active(enable) {
  if(!active)return;
  previousOwner=__atomic_load_n(&ctx.owner,__ATOMIC_RELAXED);previousPort=ctx.port;previousDDI=ctx.ddi;
  ctx.port=p;ctx.ddi=d;__atomic_store_n(&ctx.owner,thread,__ATOMIC_RELEASE);
 }
 void finish(){if(!active)return;__atomic_store_n(&ctx.owner,0,__ATOMIC_RELEASE);
  ctx.port=previousPort;ctx.ddi=previousDDI;__atomic_store_n(&ctx.owner,previousOwner,__ATOMIC_RELEASE);active=false;}
 ~TGLNativeFiaScope(){finish();}
};

struct TGLNativeFia {
 TGLFiaPort fia {}; uint32_t donorMask {};
 // The caller has already observed modular FIA in the powered mode4 probe.
 // TC5/6 remain excluded: the native ready switch has no case for them.
 bool resolve(uint32_t ddi){TGLTypeCPort tc;
  if(ddi<3 || ddi>6 || !TGLTypeCPort::resolve(ddi,tc) || !tc.resolveFia(0x10,fia))return false;
  donorMask=1U<<tc.tcIndex;return true;}
 uint32_t logical(uint32_t raw)const{return (raw&~donorMask)|((raw&fia.readyMask)?donorMask:0);}
 uint32_t physical(uint32_t old,uint32_t logicalValue)const{
  return (old&~fia.readyMask)|((logicalValue&donorMask)?fia.readyMask:0);}
 bool identity()const{return fia.instance==0 && fia.readyMask==donorMask;}
 static bool readSite(uintptr_t pc,uint32_t reg){
  return (reg==0x163890 && pc==0x5FEEA) ||
   (reg==0x163894 && (pc==0x5FD2D || pc==0x60010));}
 static bool writeSite(uintptr_t pc,uint32_t reg){
  return reg==0x163894 && (pc==0x5FDF9 || pc==0x600D4);}
};
#endif
