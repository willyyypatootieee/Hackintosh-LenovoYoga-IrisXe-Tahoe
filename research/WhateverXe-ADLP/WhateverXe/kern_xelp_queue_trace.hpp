// Read-only ownership checks for a bounded, opt-in application queue trace.
#ifndef kern_xelp_queue_trace_hpp
#define kern_xelp_queue_trace_hpp
#include <stdint.h>
namespace XELPQueueTrace {
struct Target { uint64_t pid {}, queue {}, context {}, id {}, descriptor {}, task {}; };
template <typename Read>
bool identify(uint64_t queue,uint64_t owner,Target &t,Read read) {
 uint64_t pid=0,ctx=0,back=0,id=0,desc=0,task=0;
 if(!queue || !read(queue+0x578,pid) || !(uint32_t)pid ||
    !read(queue+0x6A8,ctx) || !ctx || !read(ctx+0x50,back) || back!=owner ||
    !read(ctx+0x7C,id) || !read(ctx+0x89,desc) || !(desc&1) || !read(ctx+0x58,task) || !task)return false;
 t={uint32_t(pid),queue,ctx,uint32_t(id),desc,task};return true;
}
template <typename Read>
bool matches(const Target &t,uint64_t context,uint64_t owner,Read read) {
 uint64_t back=0,id=0,desc=0,task=0;
 return t.context==context && read(context+0x50,back) && back==owner &&
  read(context+0x7C,id) && uint32_t(id)==t.id &&
  read(context+0x89,desc) && (desc&~4ULL)==(t.descriptor&~4ULL) &&
  read(context+0x58,task) && task==t.task;
}
}
#endif
