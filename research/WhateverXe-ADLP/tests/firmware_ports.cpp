#include "kern_vbt.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
#include <iterator>
using namespace XELP;
using namespace XELP::VBT;
void w16(std::vector<uint8_t>&b,size_t at,uint16_t v){b[at]=v;b[at+1]=v>>8;}
void w32(std::vector<uint8_t>&b,size_t at,uint32_t v){w16(b,at,v);w16(b,at+2,v>>16);}
void checksum(std::vector<uint8_t>&b){b[26]=0;uint8_t sum=0;for(auto v:b)sum+=v;b[26]=-sum;}
std::vector<uint8_t> table(const std::vector<Port>&ports){
 std::vector<uint8_t>b(48+22+3+5+39*ports.size());memcpy(b.data(),"$VBT",4);
 w16(b,20,100);w16(b,22,48);w16(b,24,b.size());w32(b,28,48);
 memcpy(b.data()+48,"BIOS_DATA_BLOCK ",16);w16(b,64,250);w16(b,66,22);w16(b,68,b.size()-48);
 b[70]=2;w16(b,71,b.size()-73);b[77]=39;
 for(size_t i=0;i<ports.size();++i){auto at=78+39*i;const auto &p=ports[i];w16(b,at+2,p.type);
  b[at+16]=p.dvo;b[at+19]=p.ddc;b[at+25]=p.aux;b[at+23]=p.flags;b[at+33]=p.capabilities;}
 checksum(b);return b;
}
int main(int argc,char **argv){
 if(argc==3){std::ifstream f(argv[2],std::ios::binary);assert(f);std::vector<uint8_t>b{std::istreambuf_iterator<char>(f),{}};
  auto r=parse(b.data(),b.size(),!strcmp(argv[1],"12")?Family::TigerLake:Family::AlderLakeN);auto w=r.hdmi(3);
  printf("status=%u bdb=%u present=%x unmapped=%u ambiguous=%x hdmi=%x pins=%u,%u,%u valid=%u\n",unsigned(r.status),r.version,r.present,r.unmapped,r.ambiguous,w.hdmi,w.ddc[0],w.ddc[1],w.ddc[2],w.valid);
  for(unsigned d=0;d<9;++d)if(r.present&(1U<<d)){auto q=r.port[d];printf("ddi=%u type=%x dvo=%u ddc=%u aux=%x flags=%x caps=%x\n",d,q.type,q.dvo,q.ddc,q.aux,q.flags,q.capabilities);}
  return !r.valid();
 }
 // HDMI capabilities are authoritative even if firmware's DVO label is DP.
 for(auto family:{Family::TigerLake,Family::AlderLakeN,Family::AlderLakeP})for(unsigned mask=1;mask<4;++mask)
 for(unsigned a=1;a<=3;++a)for(unsigned b=1;b<=3;++b){
  std::vector<Port> p;if(mask&1)p.push_back({0x60d2,10,a,0,0,0});if(mask&2)p.push_back({0x60d2,7,b,0,0,0});
  auto bytes=table(p);auto r=parse(bytes.data(),bytes.size(),family);assert(r.valid());auto w=r.hdmi(3);
  if(mask==3 && a==b)assert(!w.valid);else {assert(w.valid&&w.hdmi==mask);assert(w.ddc[0]==((mask&1)?a:0));assert(w.ddc[1]==((mask&2)?b:0));}
 }
 auto bytes=table({{0x60d2,1,2,0,0xc0,0},{0x60d6,11,5,0x50,0xc0,3}});
 auto r=parse(bytes.data(),bytes.size(),Family::TigerLake);assert(r.valid()&&r.present==0x12&&r.hdmi(3).hdmi==2&&r.ambiguous==0x10);
 // Type-C capability never becomes an automatic fixed-DP mode decision.
 assert(r.port[4].capabilities==3&&r.port[4].dp());
 // All six port coordinates and capability combinations, independent of
 // CPU model. Current runtime auto backend is TC1/2 only (native FIA1).
 for(unsigned d=3;d<9;++d)for(unsigned caps=0;caps<4;++caps){
  const unsigned dvo[]={9,11,13,15,17,19};
  auto b=table({{0x60d6,dvo[d-3],d+1,(d+1)*0x10,0xc0,caps}});
  auto q=parse(b.data(),b.size(),Family::TigerLake);
  assert(q.tglDPCandidates(0x1f8)==(1U<<d));
  assert(q.tglDPCandidates(0x18)==(d<5?1U<<d:0));
  for(unsigned aux:{0u,0x30u,0xa0u}){q.port[d].aux=aux;assert(!q.tglDPCandidates(0x1f8));}
 }
 for(auto p:{Port{0x60d2,11,5,0x50},Port{0x78c6,11,5,0x50},Port{0x60d6,11,5,0x50,2},
              Port{0x60d6,11,5,0x50,4},Port{0x60d6,11,5,0x50,1},Port{0x60d6,11,5,0x50,0x10}}){
  auto b=table({p});auto q=parse(b.data(),b.size(),Family::TigerLake);assert(!q.tglDPCandidates(0x18));
 }
 assert(r.tglDPCandidates(0x18)==0x10);
 auto shared=r;shared.present|=8;shared.port[3]={0x68c6,9,0,0x50};
 assert(!shared.tglDPCandidates(0x18));
  auto adl=table({{0x60d2,10,1},{0x60d2,7,2},{0x60d2,14,3}});
  auto a=parse(adl.data(),adl.size(),Family::AlderLakeN);assert(a.valid()&&a.present==0xB&&a.hdmi(3).hdmi==3);
  auto p=parse(adl.data(),adl.size(),Family::AlderLakeP);assert(p.valid()&&p.present==0xB&&p.hdmi(3).hdmi==3);
  assert(ddi(Family::TigerLake,9)==3&&ddi(Family::AlderLakeN,13)==3&&ddi(Family::AlderLakeP,13)==3&&ddi(Family::AlderLakeN,19)==6);
  assert(ddi(Family::AlderLakeN,9)==PortCount&&ddi(Family::TigerLake,8)==PortCount);
  auto panel=table({{0x1806,10,3,0x40,0xc0,0},{0x68c6,13,3,0x60,0xc0,3}});
   auto panelPorts=parse(panel.data(),panel.size(),Family::AlderLakeP);auto route=panelPorts.internalPanel();
   assert(route.valid&&route.ddi==0&&route.ddc==3&&route.aux==0x40);
   panelPorts.port[0].aux=0x41;assert(!panelPorts.internalPanel().valid);
   panelPorts=parse(panel.data(),panel.size(),Family::AlderLakeP);panelPorts.present|=2;
   panelPorts.port[1]={0x1806,7,3,0x50,0xc0,0};assert(!panelPorts.internalPanel().valid);
   panelPorts=parse(panel.data(),panel.size(),Family::AlderLakeP);panelPorts.present|=2;
   panelPorts.port[1]={0x68c6,7,3,0x40,0xc0,0};assert(!panelPorts.internalPanel().valid);
 for(auto p:{Port{0x60d6,1,2},Port{0x78c6,10,1},Port{0x60d2,1,2,0,4},Port{0x60d2,1,2,0,2},Port{0x60d2,1,2,0,0,1}}){
  auto b=table({p});auto q=parse(b.data(),b.size(),Family::TigerLake);assert(q.valid()&&!q.hdmi(3).hdmi);
 }
 auto dup=table({{0x60d2,1,2},{0x60d2,7,2}});assert(parse(dup.data(),dup.size(),Family::TigerLake).status==Status::Conflict);
 for(size_t len=0;len<bytes.size();++len)assert(!parse(bytes.data(),len,Family::TigerLake).valid());
 auto bad=bytes;bad.back()^=1;assert(parse(bad.data(),bad.size(),Family::TigerLake).status==Status::Checksum);
 for(unsigned off:{22u,24u,28u,66u,68u,71u,77u}){auto b=bytes;b[off]=0xff;b[off+1]=0xff;checksum(b);assert(!parse(b.data(),b.size(),Family::TigerLake).valid());}
 for(unsigned version:{215u,257u}){auto b=bytes;w16(b,64,version);checksum(b);assert(parse(b.data(),b.size(),Family::TigerLake).status==Status::Version);}
 auto empty=table({});assert(parse(empty.data(),empty.size(),Family::TigerLake).valid());
 assert(!selectHDMI(true,nullptr,0,r,3).valid);assert(selectHDMI(false,nullptr,0,r,3).hdmi==2);
 uint32_t explicitWiring[]={1,1,3,0,0};assert(selectHDMI(true,explicitWiring,20,r,3).hdmi==1);
 std::vector<uint8_t> op(8192);memcpy(op.data(),"IntelGraphicsMem",16);w32(op,16,8);op[23]=2;op[22]=1;w32(op,0x58,4);
 w32(op,0x3BA,8192);w32(op,0x3C2,4096);auto loc=region(op.data(),op.size(),0x80000000);
 assert(loc.valid&&loc.address==0x80002000&&loc.bytes==4096&&loc.inlineBytes==7168);
 op[22]=0;assert(region(op.data(),op.size(),0x80000000).address==8192);
 op[22]=1;w32(op,0x3BA,4096);assert(!region(op.data(),op.size(),0x80000000).address);
 w32(op,0x3BA,0xffffffff);w32(op,0x3BE,0xffffffff);assert(!region(op.data(),op.size(),0x80000000).address);
 w32(op,0x58,0x10);assert(region(op.data(),op.size(),0x80000000).inlineBytes==6144);
 assert(!region(op.data(),8191,0x80000000).valid);
 // Deterministic bounded mutation sweep: no reads outside declared buffers.
 unsigned seed=7;for(unsigned n=0;n<2000;++n){auto b=bytes;seed=seed*1664525+1013904223;b[seed%b.size()]^=uint8_t(seed>>24);checksum(b);(void)parse(b.data(),b.size(),Family::TigerLake);}
 puts("PASS firmware bounds/checksum/versions, DVO generation maps, HDMI pin permutations, capability vs mode, overrides and OpRegion addresses");
}
