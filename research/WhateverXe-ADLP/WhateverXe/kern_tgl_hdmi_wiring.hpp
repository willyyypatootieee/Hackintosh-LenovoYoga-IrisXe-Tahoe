// Explicit combo-HDMI wiring intent. Physical DDI and DDC pin selection are
// board data; donor ABI and backend qualification remain independent guards.
#ifndef kern_tgl_hdmi_wiring_hpp
#define kern_tgl_hdmi_wiring_hpp
#include <stdint.h>
#include <string.h>
struct TGLHDMIWiring {
 static constexpr uint32_t Mask=7; // donor combo DDI0..2, not framebuffer IDs
 uint32_t hdmi {},ddc[3] {};bool valid {};
 static TGLHDMIWiring empty(){TGLHDMIWiring w;w.valid=true;return w;}
 // Data: version1, HDMI mask, DDC pin for DDI0, DDI1, DDI2 (five LE words).
 // This interface covers combo pins1..3 only. TC HDMI/Type-C/LSPCON routing
 // requires a different backend; accepting a pin number is not its support.
 static TGLHDMIWiring fromProperty(const void *bytes,uint32_t length){
  TGLHDMIWiring w;if(!bytes || length!=20)return w;
  auto b=static_cast<const uint8_t *>(bytes);uint32_t q[5] {};
  for(unsigned i=0;i<5;++i)for(unsigned j=0;j<4;++j)q[i]|=uint32_t(b[4*i+j])<<(8*j);
  if(q[0]!=1 || (q[1]&~Mask))return w;
  uint32_t used=0;
  for(unsigned i=0;i<3;++i){
   const auto pin=q[2+i];
   if(q[1]&(1u<<i)){if(pin<1 || pin>3 || (used&(1u<<pin)))return w;used|=1u<<pin;}
   else if(pin)return w;
  }
  w.hdmi=q[1];for(unsigned i=0;i<3;++i)w.ddc[i]=q[2+i];w.valid=true;return w;
 }
 bool supported(uint32_t backend,bool featureEnabled) const {
  return valid && !(hdmi&~backend) && (!hdmi || featureEnabled);
 }
 bool usesDDC(uint32_t pin) const {
  if(!valid)return false;
  for(unsigned d=0;d<3;++d)if((hdmi&(1u<<d)) && ddc[d]==pin)return true;
  return false;
 }
 bool matches(const uint32_t *config) const {
  if(!valid || !config || config[0]>=3 || !(hdmi&(1u<<config[0])))return false;
  return config[1]==ddc[config[0]] && config[2]==2 && config[3]==0 &&
   config[4]==0x800 && (config[5]&0xB)==2;
 }
 // Mutate only the existing donor connector, never synthesize a port. Check
 // every selected record before any write; a partial profile is not success.
 bool apply(void *platform,uint32_t length) const {
  if(!valid || !platform || length<0x1C0)return false;
  auto p=static_cast<uint8_t *>(platform);
  if(p[0x12]!=9)return false;
  for(unsigned d=0;d<3;++d)if(hdmi&(1u<<d)){
   const uint32_t expected[]={d,d+1,1,0,0x400,0x201};
   const uint32_t panel[]={0,0,0,0,2,0x18};
   if(memcmp(p+0x20+24*d,expected,sizeof(expected)) &&
      (d!=0 || memcmp(p+0x20,panel,sizeof(panel))))return false;
  }
  for(unsigned d=0;d<3;++d)if(hdmi&(1u<<d)){
   auto config=reinterpret_cast<uint32_t *>(p+0x20+24*d);
   const bool wasPanel=d==0 && config[2]==0;
   config[1]=ddc[d];config[2]=2;config[4]=0x800;config[5]=wasPanel?0x202:(config[5]&~0x9U)|2U;
  }
  return true;
 }
};
#endif
