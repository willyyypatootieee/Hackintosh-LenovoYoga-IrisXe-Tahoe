// Qualified donor ABI bridge for TGLTCMode. This binds native objects/functions;
// it does not create lifecycle admission, retain objects, or prove quiescence.
// The host owns those contracts and holds the r358 Mode lock for a transaction.
#ifndef kern_tgl_tc_native_hpp
#define kern_tgl_tc_native_hpp
#include "kern_tgl_tc_mode.hpp"
struct TGLTCNativeFunctions {
 uint32_t (*baseMode)(void *,uint32_t) {};
 void (*getAux)(void *) {},(*putAux)(void *) {};
 void (*getPG)(void *,uint32_t) {},(*putPG)(void *,uint32_t) {};
 void *(*portByDDI)(uint32_t) {};
 void **controllerSlot {};bool qualified {};
 bool complete() const {
  return qualified && baseMode && getAux && putAux && getPG && putPG && portByDDI && controllerSlot;
 }
};
struct TGLTCNativeBinding {
 enum class Status:uint32_t {Unqualified,InvalidPort,WrongController,WrongPower,WrongManager,WrongMap,WrongBuffer,Ready};
 TGLTCNativeFunctions functions;
 void *controller {},*port {},*power {},*manager {};
 volatile uint8_t *base {};uint32_t length {},ddi {};
 Status status {Status::Unqualified};
 template<class T> static T field(const void *object,uint32_t offset) {
  return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(object)+offset);
 }
 // Objects must already be live and protected by the host's lifecycle exclusion.
 // mapBase/mapLength come from IOMemoryMap, not the manager being checked.
 static TGLTCNativeBinding bind(const TGLTCNativeFunctions &f,void *owner,void *object,
                               uint32_t d,uintptr_t mapBase,uint64_t mapLength) {
  TGLTCNativeBinding b;b.functions=f;b.ddi=d;
  if(!f.complete())return b;
  b.status=Status::InvalidPort;TGLTypeCPort coords;
  if(!object || !TGLTypeCPort::resolve(d,coords))return b;
  b.status=Status::WrongController;
  if(!owner || *f.controllerSlot!=owner || field<uint8_t>(owner,0x918))return b;
  if(f.portByDDI(d)!=object)return b;
  auto config=field<uint32_t *>(object,0x548);
  b.status=Status::InvalidPort;if(!config || config[0]!=d)return b;
  b.status=Status::WrongPower;auto power=field<void *>(owner,0xC8);
  if(!power || field<void *>(power,0x10)!=owner || field<uint8_t>(power,0x18))return b;
  b.status=Status::WrongManager;auto manager=field<void *>(owner,0xC40);
  if(!manager || field<void *>(object,0x578)!=manager || field<void *>(power,0x78)!=manager)return b;
  auto base=field<volatile uint8_t *>(manager,0x50);const auto length=field<uint32_t>(manager,0x60);
  b.status=Status::WrongMap;
  if(!base || reinterpret_cast<uintptr_t>(base)!=mapBase || length<0x138130 || mapLength<length)return b;
  b.status=Status::WrongBuffer;if(field<uint32_t>(object,0x590)!=coords.ddiBuffer)return b;
  b.controller=owner;b.port=object;b.power=power;b.manager=manager;b.base=base;b.length=length;
  b.status=Status::Ready;return b;
 }
 bool ready() const{return status==Status::Ready;}
 uint32_t nativeMode() const{return ready()?field<uint32_t>(port,0x550):UINT32_MAX;}
};
// Transport supplies cold/PCODE/FIA locking + MMIO/time, and HOST proofs for
// admissionReady/firmwareVerified/inheritedVerified/quiescent. In particular,
// quiescent must keep admission closed across the entire disconnect. It is
// never synthesized from mode==0, the global native AUX count, or HPD-low.
// Bind once under lifecycle exclusion, retain the objects while a slot is
// bound, and discard this IO before native stop destroys its port/power/map.
// No extra OSObject retain can make the donor's deleting destructor harmless.
template<class Transport> struct TGLTCNativeModeIO:Transport {
 const TGLTCNativeBinding &binding;
 TGLTCNativeModeIO(const Transport &transport,const TGLTCNativeBinding &b):Transport(transport),binding(b){}
 bool admissionReady(){return binding.ready() && Transport::admissionReady();}
 bool firmwareVerified(){return binding.ready() && Transport::firmwareVerified();}
 bool inheritedVerified(){return binding.ready() && Transport::inheritedVerified();}
 bool quiescent(){return binding.ready() && Transport::quiescent();}
 uint32_t nativeMode(){return binding.nativeMode();}
 uint32_t nativeSetMode(uint32_t mode){return binding.functions.baseMode(binding.port,mode);}
 bool nativeNeedsAux(){return !(TGLTCNativeBinding::field<uint32_t>(binding.controller,0xC58)&2);}
 void nativeGetAux(){binding.functions.getAux(binding.port);}
 void nativePutAux(){binding.functions.putAux(binding.port);}
 void nativeGetLanes(){binding.functions.getPG(binding.power,3);}
 void nativePutLanes(){binding.functions.putPG(binding.power,3);}
 bool lanesReady(){
  if(!binding.ready() || !this->readable(0x45404) || !this->readable(0x42000))return false;
  // The core owns a separate PG3 token even when C58 suppresses native AUX.
  // PW1..3 state bits plus their fuse distribution bits; PW4 is not needed.
  const auto wells=this->read(0x45404),fuse=this->read(0x42000);
  return wells!=UINT32_MAX && fuse!=UINT32_MAX && (wells&0x15)==0x15 && (fuse&0x07000000)==0x07000000;
 }
};
#endif
