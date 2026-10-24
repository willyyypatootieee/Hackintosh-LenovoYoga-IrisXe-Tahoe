#include "../WhateverXe/kern_xelp_queue_trace.hpp"
#include <cassert>
#include <map>
int main() {
 using namespace XELPQueueTrace;
 const uint64_t queue=0xffff900000010000ULL,ctx=queue+0x1000,owner=queue+0x2000,task=queue+0x3000;
 std::map<uint64_t,uint64_t> mem {{queue+0x578,0xAABBCCDD00001234ULL},
  {queue+0x6A8,ctx},{ctx+0x50,owner},{ctx+0x7C,0x1122334400000017ULL},
  {ctx+0x89,0x1746651019ULL},{ctx+0x58,task}};
 auto read=[&](uint64_t p,uint64_t &v){auto i=mem.find(p);if(i==mem.end())return false;v=i->second;return true;};
 Target t;
 assert(identify(queue,owner,t,read));assert(t.pid==0x1234 && t.id==0x17 && t.task==task);
 assert(matches(t,ctx,owner,read));assert(!matches(t,ctx+0x100,owner,read));
 mem[ctx+0x89]^=4;assert(matches(t,ctx,owner,read));
 mem[ctx+0x89]^=0x1000;assert(!matches(t,ctx,owner,read));mem[ctx+0x89]^=0x1004;
 mem[ctx+0x58]=task+8;assert(!matches(t,ctx,owner,read));mem[ctx+0x58]=task;
 mem[ctx+0x7C]++;assert(!matches(t,ctx,owner,read));mem[ctx+0x7C]--;
 assert(!identify(queue,owner+8,t,read));assert(!matches(t,ctx,owner+8,read));
 mem[queue+0x578]=0;assert(!identify(queue,owner,t,read));mem[queue+0x578]=0x1234;
 mem[ctx+0x89]&=~1ULL;assert(!identify(queue,owner,t,read));mem[ctx+0x89]|=1;
 mem.erase(ctx+0x58);assert(!identify(queue,owner,t,read));assert(!matches(t,ctx,owner,read));
}
