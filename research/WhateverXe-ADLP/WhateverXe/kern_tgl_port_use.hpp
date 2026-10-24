// Port lifetime admission. The host retains the controller/port/MMIO before
// binding, intercepts ALL relevant entries, and supplies a short useLock.
// No native/power/MMIO call or wait occurs with that lock held. This does not
// prove hardware idle: an exclusive caller must still shut down/read back
// hardware and balance its cold/native references before releasing objects.
#ifndef kern_tgl_port_use_hpp
#define kern_tgl_port_use_hpp
#include <stdint.h>
#include "kern_tgl_tc_cold.hpp"

struct TGLPortUse {
 enum class Kind:uint32_t { Work, Exclusive, Training, Scanout, Cleanup };
 enum class Status:uint32_t { Success, Invalid, WrongIdentity, Closed, Busy, Full, Stale, Overflow };
 // Keep a drain owner plus the qualified I2C -> AUX depth available even
 // when all ordinary slots are resident reservations. Deeper calls may fail
 // Full; the host retains resources and must never bypass admission.
 static constexpr uint32_t WorkCapacity=64,DrainSlots=3,Capacity=WorkCapacity+6*DrainSlots;
 struct Token { uint64_t serial {}; uint32_t slot {Capacity}; bool valid() const {return serial && slot<Capacity;} };
 struct Result { Status status {Status::Invalid};Token token {};bool ok() const {return status==Status::Success;} };
 struct Port { uintptr_t identity {};bool closing {},retired {}; };
 struct Entry { uint64_t serial {};uintptr_t thread {};uint32_t ddi {};Kind kind {Kind::Work}; };
 uintptr_t owner {};bool stopping {};uint64_t sequence {};
 Port ports[6] {};Entry entries[Capacity] {};
 TGLPortUse()=default;TGLPortUse(const TGLPortUse&)=delete;TGLPortUse& operator=(const TGLPortUse&)=delete;
 // IO useLock/useUnlock protects all fields, separately from cold/FIA locks.
 // Thread identity is current_thread(), never an inferred hardware owner.
 template<class IO> Status bind(IO &io,uintptr_t who,uintptr_t port,uint32_t d) {
  Lock<IO> lock(io);if(!valid(who,port,d))return Status::Invalid;
  if(owner && owner!=who)return Status::WrongIdentity;
  if(stopping)return Status::Closed;
  auto &p=ports[d-3];if(p.identity && p.identity!=port)return Status::WrongIdentity;
  for(const auto &other:ports)if(&other!=&p && other.identity==port)return Status::WrongIdentity;
  if(p.retired)return Status::Closed;
  owner=who;p.identity=port;return Status::Success;
 }
 // Existing synchronous descendants may finish after close/stop. They must
 // match BOTH the physical port and thread; a different port is a new use.
 template<class IO> Result enter(IO &io,uintptr_t who,uintptr_t port,uint32_t d,uintptr_t thread) {
  Lock<IO> lock(io);Result r;r.status=identity(who,port,d);if(!r.ok()||!thread)return invalidThread(r,thread);
  bool nested=false,draining=false;
  for(const auto &e:entries)if(e.serial && e.ddi==d && active(e.kind)) {
   if(e.kind==Kind::Exclusive && e.thread!=thread){r.status=Status::Busy;return r;}
   if(e.thread==thread){nested=true;draining|=e.kind==Kind::Exclusive;}
  }
  if((stopping||ports[d-3].closing)&&!nested){r.status=Status::Closed;return r;}
  return allocate(d,thread,Kind::Work,draining);
 }
 template<class IO> Status leave(IO &io,uintptr_t who,uintptr_t thread,Token &token) {
  Lock<IO> lock(io);auto e=find(who,token);
  if(!e)return Status::Stale;
  if(e->kind!=Kind::Work || !thread || e->thread!=thread)return Status::WrongIdentity;
  *e={};token={};return Status::Success;
 }
 // Turn an admitted call into a persistent reservation BEFORE returning.
 // No allocation is needed, including on a failed hardware cleanup path.
 // Resident reservations do not authorize new work after closing.
 template<class IO> Status hold(IO &io,uintptr_t who,uintptr_t thread,Token &token,Kind kind) {
  Lock<IO> lock(io);auto e=find(who,token);
  if(!e)return Status::Stale;
  if((kind!=Kind::Training && kind!=Kind::Scanout && kind!=Kind::Cleanup) || e->kind!=Kind::Work || !thread || e->thread!=thread)return Status::Invalid;
  e->kind=kind;e->thread=0;return Status::Success;
 }
 // Completion/adoption may run on a different thread. It needs its own
 // currently admitted operation or exclusive drain token for the SAME port.
 // Caller releases/transfers the actual resource first; this is bookkeeping.
 template<class IO> Status drop(IO &io,uintptr_t who,uintptr_t thread,const Token &operation,Token &resident) {
  Lock<IO> lock(io);auto op=find(who,operation),e=find(who,resident);
  if(!op||!e)return Status::Stale;
  if(!thread || op->thread!=thread || !active(op->kind) || active(e->kind) || op->ddi!=e->ddi)return Status::WrongIdentity;
  *e={};resident={};return Status::Success;
 }
 template<class IO> Status close(IO &io,uintptr_t who,uintptr_t port,uint32_t d) {
  Lock<IO> lock(io);auto s=identity(who,port,d);if(s==Status::Success)ports[d-3].closing=true;return s;
 }
 template<class IO> Status stop(IO &io,uintptr_t who) {
  Lock<IO> lock(io);if(!who || owner!=who)return Status::WrongIdentity;
  stopping=true;for(auto &p:ports)p.closing=true;return Status::Success;
 }
 // Nonblocking claim: the host can wait without any gate/native lock held.
 // A pending resident lease is allowed so this owner can explicitly drain it.
 template<class IO> Result claim(IO &io,uintptr_t who,uintptr_t port,uint32_t d,uintptr_t thread) {
  Lock<IO> lock(io);Result r;r.status=identity(who,port,d);if(!r.ok()||!thread)return invalidThread(r,thread);
  if(ports[d-3].retired){r.status=Status::Closed;return r;}
  if(!ports[d-3].closing){r.status=Status::Invalid;return r;}
  for(const auto &e:entries)if(e.serial && e.ddi==d && active(e.kind)){r.status=Status::Busy;return r;}
  return allocate(d,thread,Kind::Exclusive);
 }
 // Preserve exclusion on failure. A caller must not accidentally reopen a
 // stopping controller, destroy a port with a resident lease, or retire an
 // exclusive owner while a synchronous AUX child is still running.
 // reopen=false retires the port permanently; no later drain can claim it.
 template<class IO> Status finish(IO &io,uintptr_t who,uintptr_t thread,Token &exclusive,bool reopen) {
  Lock<IO> lock(io);auto e=find(who,exclusive);
  if(!e)return Status::Stale;
  if(!thread || e->thread!=thread || e->kind!=Kind::Exclusive)return Status::WrongIdentity;
  for(const auto &other:entries)if(other.serial && other.ddi==e->ddi && &other!=e)return Status::Busy;
  if(reopen&&stopping)return Status::Closed;
  ports[e->ddi-3].closing=!reopen;ports[e->ddi-3].retired=!reopen;
  *e={};exclusive={};return Status::Success;
 }
 // Upper AUX adapter, used for read/write/I2C, including synchronous nested
 // calls. A completed native write keeps its native result even when cleanup
 // fails. The host stores the returned reservation and call details until
 // explicit release (executed) or idle reconciliation (acquire failed).
 // Gate refusal never falls through to an untracked native call.
 struct AUXResult {
  Result admission {};TGLTCCold::AUXCall call {};
  Status retirement {Status::Invalid};Token retained {};
 };
 template<class IO,class Operation> AUXResult invokeAUX(IO &io,TGLTCCold &cold,uintptr_t who,
                          uintptr_t port,uint32_t d,uintptr_t thread,Operation operation) {
  AUXResult r;r.admission=enter(io,who,port,d,thread);if(!r.admission.ok())return r;
  r.call=cold.invokeAUX(io,who,d,operation);
  if(r.call.retainedReference() ||
     (!r.call.executed && r.call.acquire.status==TGLTCCold::Status::TransportFailed)) {
   r.retirement=hold(io,who,thread,r.admission.token,Kind::Cleanup);
  }else r.retirement=leave(io,who,thread,r.admission.token);
  if(r.admission.token.valid())r.retained=r.admission.token;
  return r;
 }
 // Gate-side condition only. Host additionally proves hardware idle,
 // no cold/native debt, and no untracked entry before object destruction.
 template<class IO> bool emptyClosed(IO &io,uintptr_t who,uintptr_t port,uint32_t d) {
  Lock<IO> lock(io);if(identity(who,port,d)!=Status::Success || !ports[d-3].retired)return false;
  for(const auto &e:entries)if(e.serial && e.ddi==d)return false;
  return true;
 }
private:
 template<class IO> struct Lock {IO &io;explicit Lock(IO &i):io(i){io.useLock();}~Lock(){io.useUnlock();}};
 static bool valid(uintptr_t who,uintptr_t port,uint32_t d){return who && port && d>=3 && d<=8;}
 static bool active(Kind k){return k==Kind::Work || k==Kind::Exclusive;}
 Status identity(uintptr_t who,uintptr_t port,uint32_t d) const {
  if(!valid(who,port,d))return Status::Invalid;
  return who==owner && ports[d-3].identity==port ? Status::Success : Status::WrongIdentity;
 }
 static Result invalidThread(Result r,uintptr_t thread){if(!thread)r.status=Status::Invalid;return r;}
 Entry *find(uintptr_t who,const Token &t){
  if(!who || who!=owner || !t.valid() || entries[t.slot].serial!=t.serial)return nullptr;
  return &entries[t.slot];
 }
 Result allocate(uint32_t d,uintptr_t thread,Kind kind,bool draining=false){
  Result r;if(sequence==UINT64_MAX){r.status=Status::Overflow;return r;}
  const uint32_t drain=WorkCapacity+(d-3)*DrainSlots;
  const uint32_t begin=kind==Kind::Exclusive?drain:0;
  const uint32_t end=kind==Kind::Exclusive?begin+1:WorkCapacity;
  for(uint32_t i=begin;i<end;++i)if(!entries[i].serial){
   entries[i]={++sequence,thread,d,kind};r.status=Status::Success;r.token={sequence,i};return r;
  }
  if(draining)for(uint32_t i=drain+1;i<drain+DrainSlots;++i)if(!entries[i].serial){
   entries[i]={++sequence,thread,d,kind};r.status=Status::Success;r.token={sequence,i};return r;
  }
  r.status=Status::Full;return r;
 }
};
#endif
