#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <sys/mman.h>
// Execute the complete native controller disableDisplay, not a transcription.
// The selected path is external DP/SST, no panel, no clone/MST/plane. Audio,
// HDCP, DSC, FEC, scaler, interrupt, clock and port callees are boundaries.
// This is NOT physical idle / actual kernel shutdown qualification.
template<size_t N> struct Raw {
 alignas(8) uint8_t bytes[N] {};
 template<class T> T &at(size_t p){assert(p+sizeof(T)<=N);return *reinterpret_cast<T *>(bytes+p);}
};
Raw<0xe00> controller;Raw<0x4b00> fb;Raw<0x700> port;Raw<0x3800> path;Raw<0x100> manager;
uintptr_t managerVT[64],portVT[64];uint32_t config[8],mmio[0x100000/4];
uint64_t canary=0x384756;unsigned pipe,ddi,sleeps,locks,clockDrops,portDrops;
bool pipeStuck,bufferPending;
uint32_t &reg(uint32_t p){assert(p%4==0 && p<sizeof(mmio));return mmio[p/4];}
uint32_t readReg(void *p,uint64_t r){assert(p==manager.bytes);return reg(r);}
void writeReg(void *p,uint64_t r,uint32_t v){
 assert(p==manager.bytes);
 if(r==0x70008+0x1000*pipe) {
  assert(!(v&0x80000000));v=(v&~0xC0000000u)|(pipeStuck?0x40000000u:0);
 }
 reg(r)=v;
}
void lock(void*){assert(!locks);++locks;}void unlock(void*){assert(locks==1);--locks;}
void sleepMock(unsigned n){assert(n==2);++sleeps;}void delayMock(unsigned n){assert(n==8);}
uint32_t noOp(){return 0;}
uint32_t disablePort(void *p){
 assert(p==port.bytes);++portDrops;
 reg(0x64000+0x100*ddi)=bufferPending?0:0x80;
 reg(0x60540+0x1000*pipe)=0;
 return 0; // native API cannot communicate these pending states
}
void clocks(void *c,void *f,void *d){assert(c==controller.bytes && f==fb.bytes && d==path.bytes);++clockDrops;}
void *endPort(uint32_t index){assert(index==ddi);return port.bytes;}
void endClocks(void *c,void *p,void *other){assert(c==controller.bytes && p==port.bytes && !other);++clockDrops;}
void jump(uint8_t *p,uintptr_t target){p[0]=0x48;p[1]=0xb8;memcpy(p+2,&target,8);p[10]=0xff;p[11]=0xe0;}
unsigned nextStub=0x300000;
void call(uint8_t *m,unsigned at,uintptr_t target){
 assert(m[at]==0xe8);jump(m+nextStub,target);int32_t relative=nextStub-at-5;
 memcpy(m+at+1,&relative,4);nextStub+=32;
}
int main(int argc,char **argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);
 std::vector<char> donor((std::istreambuf_iterator<char>(f)),{});
 const size_t size=4*1024*1024;assert(!donor.empty() && donor.size()<size);
 auto m=static_cast<uint8_t*>(mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0));
 assert(m!=MAP_FAILED);memcpy(m,donor.data(),donor.size());
 for(auto at:{0x4d4e2u,0x4d4eau,0x4d5a3u,0x4d5d4u,0x4d5edu,0x4d95au,0x4d99cu,0x4d9aau,0x4db58u,0x4db64u,0x4db6cu,0x4dba2u})call(m,at,(uintptr_t)noOp);
 call(m,0x4d506,(uintptr_t)lock);call(m,0x4dbd8,(uintptr_t)unlock);
 call(m,0x4d76f,(uintptr_t)readReg);call(m,0x4d932,(uintptr_t)readReg);
 call(m,0x4d77f,(uintptr_t)sleepMock);call(m,0x4dbac,(uintptr_t)delayMock);
 call(m,0x4dbba,(uintptr_t)clocks);
 *reinterpret_cast<uint64_t **>(m+0xc80a8)=&canary;
 using Disable=uint32_t(*)(void*,void*,void*,uint8_t);auto disable=(Disable)(m+0x4d34c);
 unsigned cases=0;
 for(ddi=3;ddi<=8;++ddi)for(pipe=0;pipe<3;++pipe)for(unsigned fault=0;fault<3;++fault){
  controller={};fb={};port={};path={};manager={};memset(mmio,0,sizeof(mmio));memset(config,0,sizeof(config));
  sleeps=locks=clockDrops=portDrops=0;pipeStuck=fault==1;bufferPending=fault==2;
  managerVT[0x130/8]=(uintptr_t)writeReg;portVT[0x130/8]=(uintptr_t)disablePort;
  manager.at<void*>(0)=managerVT;port.at<void*>(0)=portVT;
  controller.at<void*>(0xc40)=manager.bytes;port.at<void*>(0x548)=config;config[0]=ddi;config[2]=1;
  path.at<uint32_t>(0x3640)=pipe;path.at<uint32_t>(0x3644)=2;path.at<void*>(0x3648)=port.bytes;
  path.at<uint32_t>(0x154)=0x80000000;
  reg(0x70008+pipe*0x1000)=0xC0000000;
  reg(0x60400+pipe*0x1000)=0x80000000|((ddi+1)<<27);
  reg(0x60540+pipe*0x1000)=0x80000000;reg(0x64000+ddi*0x100)=0x80000000;
  reg(0x46140+pipe*4)=(ddi+1)<<28;
  auto result=disable(controller.bytes,fb.bytes,path.bytes,0);
  assert(result==0 && path.at<uint32_t>(0x3644)==1 && !locks);
  assert(sleeps==(pipeStuck?20u:0u) && clockDrops==1 && portDrops==1);
  assert(reg(0x70008+pipe*0x1000)==(pipeStuck?0x40000000u:0u));
  assert(reg(0x64000+ddi*0x100)==(bufferPending?0u:0x80u));
  assert(!reg(0x60400+pipe*0x1000) && !reg(0x60540+pipe*0x1000) && !reg(0x46140+pipe*4));
  // The old release predicate saw ENABLE bits clear and would accept both
  // modeled failures. The strengthened actual lease fixture rejects them.
  assert(!((reg(0x64000+ddi*0x100)|reg(0x60400+pipe*0x1000)|reg(0x60540+pipe*0x1000))&0x80000000));
  ++cases;
 }
 // Native abort-End does not remember an already-released clock reference.
 call(m,0x6ad27,(uintptr_t)endPort);call(m,0x6ad5b,(uintptr_t)endClocks);
 using End=uint32_t(*)(void*,void*);auto end=(End)(m+0x6ad06);
 for(ddi=3;ddi<=8;++ddi) {
  uint32_t request[3]={ddi+1,0,0};clockDrops=portDrops=0;
  assert(end(controller.bytes,request)==0 && end(controller.bytes,request)==0);
  assert(clockDrops==2 && portDrops==2);
  request[2]=1;assert(end(controller.bytes,request)==0 && clockDrops==2 && portDrops==2);
 }
 puts("PASS native abort-End replay: repeated End invokes disablePort/disableClocks twice on all 6 DDIs; successful-training End invokes neither (lower calls mocked)");
 munmap(m,size);
 printf("PASS %u native disableDisplay cases (6 DDIs x 3 pipes x normal/pipe-active/buffer-pending): return=0 and path state=1 in every case; timed-out pipe retains ACTIVE after 20 sleeps, pending buffer lacks idle; native continues clock teardown.\n",cases);
 puts("CPU replay of qualified controller function; hardware state and listed callees mocked. Counterexample to return/software-state idle proof, not a live fault observation.");
}
