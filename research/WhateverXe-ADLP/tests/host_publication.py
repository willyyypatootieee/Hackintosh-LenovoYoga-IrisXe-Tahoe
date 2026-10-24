#!/usr/bin/env python3
"""Replay actual host admission: no PCI reads before successful publication."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
adapter=(r/'WhateverXe/kern_xelp_device.hpp').read_text();adapter=adapter[adapter.index('namespace XELP {'):adapter.rindex('#endif')]
s=(r/'WhateverXe/kern_host.cpp').read_text();body=s[s.index('void XeHost::processKernel'):s.index('void XeHost::processKext')]
code=r'''
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <vector>
#include "kern_xelp_platform.hpp"
std::vector<int> events;bool publish=true,published=false;uint32_t identity=0x9a498086;
struct Object {virtual ~Object()=default;};
struct OSData:Object {uint32_t value=0;void *getBytesNoCopy(){return &value;}size_t getLength(){return 4;}};
OSData fake;Object customModel;bool hasModel=false;unsigned modelWrites=0;Object *deviceProperty=nullptr;
struct IOPCIDevice {
 uint32_t configRead32(int){assert(published);events.push_back(2);return identity;}
 uint8_t configRead8(int){assert(published);events.push_back(3);return 0;}
 uint16_t configRead16(int){assert(published);events.push_back(4);return 0x300;}
 const char *getName(){return "GFX0";}Object *getProperty(const char *key){return !strcmp(key,"device-id")?deviceProperty:!strcmp(key,"model")&&hasModel?&customModel:nullptr;}
 void setProperty(const char *,const char *){}void setProperty(const char *key,const void *value,unsigned size){if(!strcmp(key,"model")){assert(value && size>1);++modelWrites;}}void setProperty(const char *,uint64_t,unsigned){}
};
#define OSDynamicCast(type,p) dynamic_cast<type *>(p)
struct DeviceInfo {
 enum class FirmwareVendor{Apple,Other};FirmwareVendor firmwareVendor=FirmwareVendor::Other;
 IOPCIDevice *videoBuiltin=nullptr;bool reportedFramebufferIsConnectorLess=false;
 static DeviceInfo value;static DeviceInfo *create(){return &value;}static void deleter(DeviceInfo *){events.push_back(9);}
};DeviceInfo DeviceInfo::value;
namespace WIOKit {bool awaitPublishing(IOPCIDevice *){events.push_back(1);published=publish;return publish;}void renameDevice(IOPCIDevice *,const char *){events.push_back(5);}}
// DEVICE_ADAPTER
struct KernelPatcher {static constexpr int KernelID=0;uintptr_t solveSymbol(int,const char *){return 0;}void clearError(){}};
struct Graphics {bool off=false;void switchOff(){off=true;}} graphics;
struct XeHost {
 bool active=false;enum {FB_DETECT,FB_RESET,FB_COPY,FB_ZEROFILL,FB_NONE,FB_TOTAL};uint32_t resetFramebuffer=FB_NONE;
 struct vc_info{};vc_info consoleVinfo;bool gotConsoleVinfo=false;
 struct XE {void processKernel(KernelPatcher &,DeviceInfo *){events.push_back(8);}} xe;
 void processKernel(KernelPatcher &);
};
'''.replace('// DEVICE_ADAPTER',adapter)+body+r'''
int main(){KernelPatcher patcher;IOPCIDevice gpu;
for(unsigned test=0;test<6;++test){
 events.clear();published=false;publish=test!=1;identity=(test==2 || test>=4)?0x12348086:0x9a498086;deviceProperty=test>=4?&fake:nullptr;fake.value=test==4?0x46D2:0xFFFF;graphics.off=false;
 hasModel=test==3;modelWrites=0;
 DeviceInfo::value.videoBuiltin=test==0?nullptr:&gpu;XeHost host;host.processKernel(patcher);
 assert(events.back()==9);assert(modelWrites==(test==4?1U:0U));
 if(test<2){assert(!host.active&&graphics.off);assert(events.size()==(test==0?1:2));}
 else if(test==2 || test==5){assert(!host.active&&graphics.off);assert(events.size()==4);}
 else {assert(host.active&&!graphics.off);assert(events.front()==1);assert(events[events.size()-2]==8);}
}
puts("PASS actual host admission: absent/unpublished/unsupported GPUs never enter Xe; published PCI reads precede selected-driver dispatch");}
'''
with tempfile.TemporaryDirectory(prefix='wxe-host-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
