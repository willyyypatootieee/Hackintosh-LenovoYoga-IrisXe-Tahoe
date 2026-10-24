// Exact donor's indirect eDRAM write, serialized without covering its
// registry lookups, forcewake calls, or subsequent IOSleep(1000).
#ifndef kern_tgl_pcode_edram_hpp
#define kern_tgl_pcode_edram_hpp
#include "kern_tgl_pcode_gate.hpp"
struct TGLPcodeEDRAM {
 enum class Status { Success, GateBusy, Unavailable, InitialTimeout, CompletionTimeout, FirmwareError };
 struct Result { Status status {Status::GateBusy};uint32_t mailbox {};bool submitted {}; };
 template<class IO> static Result write(IO &io,TGLPcodeGate &gate,uintptr_t who) {
  Result r;
  TGLPcodeGate::Lease<IO> lease(gate,io,who,TGLPcodeGate::Wait::Sleep1ms);
  if (!lease.acquired()) return r;
  for (unsigned phase=0;phase<2;++phase) {
   for (unsigned n=0;;++n) {
    r.mailbox=io.read(0x138124);
    if (r.mailbox==0xFFFFFFFFU) {r.status=Status::Unavailable;return r;}
    if (!(r.mailbox&0x80000000U)) break;
    if (n==500) {r.status=phase?Status::CompletionTimeout:Status::InitialTimeout;return r;}
    io.sleepMs(1);
   }
   if (!phase) {
    // Keep the donor's two writes, including its choice not to write DATA1.
    io.write(0x138128,0xB0000000U);
    io.write(0x138124,0x80591012U);r.submitted=true;
   }
  }
  r.status=(r.mailbox&0xFF)?Status::FirmwareError:Status::Success;return r;
 }
};
struct TGLPcodeEDRAMPatch {
 static constexpr unsigned Size=25;
 static bool make(uintptr_t callback,uint8_t *out) {
  if (!callback || !out) return false;
  // movabs callback,rax; mov r14,rdi; call rax; restore native rax/rbx;
  // nop. The following native call clobbers volatile registers/flags.
  const uint8_t bytes[Size] {
   0x48,0xb8,0,0,0,0,0,0,0,0,0x4c,0x89,0xf7,0xff,0xd0,
   0x49,0x8b,0x46,0x18,0xbb,0,0,0,0xb0,0x90};
  for(unsigned i=0;i<Size;++i)out[i]=bytes[i];
  for(unsigned i=0;i<8;++i)out[2+i]=static_cast<uint8_t>(callback>>(8*i));
  return true;
 }
};
#endif
