#!/usr/bin/env python3
"""Exercise plugin selection and argument-independent provider startup."""
from pathlib import Path
import re
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
xe = (root / 'WhateverXe/kern_xe.cpp').read_text()
provider = (root / 'AppleIntelTGLCompat/AppleIntelTGLCompat.cpp').read_text()
start = (root / 'WhateverXe/kern_start.cpp').read_text()
assert 'PE_parse_boot_arg' not in provider and 'checkKernelArgument' not in provider
# Guard the public namespace across every reader, including plugin arrays.
external = {'-igfxdvmt', 'gfxrst', '-allow3d', '-disablegfxfirmware'}
used = set()
for directory in ['WhateverXe', 'AppleIntelTGLCompat']:
    for path in (root / directory).glob('*.cpp'):
        used.update(re.findall(r'(?:checkKernelArgument|PE_parse_boot_argn)\("([^"]+)"', path.read_text()))
for array, expected in [('Off', '-wexoff'), ('Debug', '-wexdbg'), ('Beta', '-wexbeta')]:
    body = re.search(r'bootarg' + array + r'\[\] \{(.*?)\}', start, re.S).group(1)
    assert re.findall(r'"([^"]+)"', body) == [expected]
    used.add(expected)
assert external <= used
assert all(name.startswith('-wex') or name in external for name in used), used
assert not (root / 'WhateverXe/kern_boot_args.hpp').exists()
# Keep the complete diagnostic namespace regression independent of user docs.
names = json.loads((root / 'tests/fixtures/boot_argument_names.json').read_text())
assert set(names) == used - external
removed = [name for aliases in names.values() for name in aliases]
assert not set(removed) & used
assert not {"-wex-display13", "-wex-adln-native", "-wex-adln-rcs-profile"} & used
assert "const bool linearTest = display13FramebufferRequested;" in xe
assert "(callbackXE->adlnInternalPanel && !callbackXE->adlnFramebufferReady)" in xe
init = xe[xe.index('void XE::init() {'):xe.index('void XE::deinit()')]
assert 'BaseDeviceInfo' not in init and 'CPU_MODEL' not in init
entry = re.search(r'extern "C" kern_return_t tglStart\(.*?\n\}', provider, re.S).group()
fixture = r'''
#include <cassert>
#include <cstring>
#include <set>
#include <string>
#include <initializer_list>
#include "kern_xelp_platform.hpp"
static const char *qualified = "Darwin Kernel Version 20.6.0: Thu Jul  6 22:12:47 PDT 2023; root:xnu-7195.141.49.702.12~1/RELEASE_X86_64";
static const char *version = qualified;
static std::set<std::string> args;
bool checkKernelArgument(const char *name) { return args.count(name); }
#define SYSLOG(...) ((void)0)
#define IOLog(...) ((void)0)
static int kextIntelTGLFb, kextAppleIntelTGLCompat, kextIntelTGL;
static struct { void onKextLoadForce(int *) {} } lilu;
struct XE {
 XELP::Profile xeProfile;
 bool adlnRequested=false, adlnDetected=false, adlnKernelReady=false, adlnNativeRequested=false, adlnPanelObservation=false, adlnInternalPanel=false, adlnInternalPanelRequested=false, adlnFramebufferReady=false;
 bool display13FramebufferRequested=false, display13TraceEnabled=false;
 bool tglRequested=false, tglAcceleratorRequested=false, tglNoPanel=false;
 bool tglInactivePorts=false, tglPG1Poll=false, tglClockOrderRequested=false;
 bool tglProgramEnabled=false, tglTrainingLeaseRequested=false;
 struct { bool available=false; } modDVMTCalcFix;
 int *currentGraphics=nullptr, *currentFramebuffer=nullptr;
 void init();
 void selectProfile(const XELP::Profile &);
};
static XE *callbackXE;
static bool adlnNativeRCSProfileRequested;
using kern_return_t = int;
struct kmod_info_t {};
static constexpr int KERN_FAILURE=5, KERN_SUCCESS=0;
// ACTUAL_SOURCE
static XE boot(std::initializer_list<const char *> names, uint32_t pci=0x46D28086,uint32_t effective=0,uint32_t revision=0) {
 args.clear(); for (const auto name:names) args.insert(name);
 XE xe; xe.init(); xe.selectProfile(XELP::identify(pci,revision,effective?effective:pci)); assert(tglStart(nullptr,nullptr)==KERN_SUCCESS);
 return xe;
}
int main() {
 // Every implemented ADL-N/Twin ID enables the qualified normal path without args.
 XE xe;
 for(uint32_t pci:{0x46D08086U,0x46D18086U,0x46D28086U,0x46D38086U,0x46D48086U}) {
  xe=boot({},pci);
  assert(xe.adlnRequested && xe.adlnKernelReady && xe.adlnNativeRequested && xe.display13FramebufferRequested);
  assert(adlnNativeRCSProfileRequested && xe.tglAcceleratorRequested);
  assert(xe.currentGraphics==&kextIntelTGL && xe.currentFramebuffer==&kextIntelTGLFb);
 }
 xe=boot({"-wex-display13-trace"});
 assert(xe.display13TraceEnabled && xe.adlnNativeRequested && adlnNativeRCSProfileRequested);
 xe=boot({"-wex-adln-kernel-ctx"});
 assert(xe.adlnNativeRequested && xe.display13FramebufferRequested && !adlnNativeRCSProfileRequested);
 xe=boot({});assert(adlnNativeRCSProfileRequested);
 for(const char *obsolete:{"-igfxn97","-igfxadln","-wex-adln","-wex-tgl","-wex-display13","-wex-adln-native","-wex-adln-rcs-profile"}) {
  xe=boot({obsolete},0x12348086);
  assert(!xe.adlnRequested && !xe.tglRequested && !xe.adlnNativeRequested && !adlnNativeRCSProfileRequested);
 }
 xe=boot({},0x9A498086);
 assert(xe.tglRequested && xe.tglAcceleratorRequested && xe.tglProgramEnabled && !xe.tglNoPanel);
 xe=boot({"-wex-nopanel","-wex-tgl-prog-off"},0x9A408086);
 assert(xe.tglRequested && xe.tglNoPanel && !xe.tglProgramEnabled);
 xe=boot({"-wex-tgl-off"},0x9A498086);assert(!xe.tglRequested);
 xe=boot({},0x12348086,0x46D28086);
 assert(xe.adlnRequested && xe.adlnNativeRequested && xe.xeProfile.identity==0x12348086);
 xe=boot({},0x12348086,0x9A498086);assert(xe.tglRequested);
  xe=boot({},0x46A68086);assert(!xe.tglRequested && !xe.adlnRequested);
  xe=boot({},0x46A68086,0x46A68086,0x0c);
  assert(xe.adlnRequested && xe.adlnNativeRequested && xe.display13FramebufferRequested && xe.tglRequested);
  xe=boot({"-wex-adlp-edp"},0x46A68086,0x46A68086,0x0c);
  assert(xe.adlnInternalPanelRequested && xe.adlnNativeRequested);
  xe=boot({},0x46A68086,0,0x0d);assert(!xe.adlnRequested && !xe.adlnNativeRequested);
 for(uint32_t pci:{0x46A68086U,0x46808086U,0xA7A08086U}) {
  xe=boot({},pci);assert(!xe.adlnNativeRequested && !xe.display13FramebufferRequested && !adlnNativeRCSProfileRequested);
 }
 xe=XE{};xe.selectProfile(XELP::identify(0x46D28086,1));
 assert(!xe.adlnNativeRequested && !xe.display13FramebufferRequested && !adlnNativeRCSProfileRequested);
 auto malformed=XELP::identify(0x46D28086,0);malformed.display=XELP::Display::XeD;
 xe=XE{};xe.selectProfile(malformed);assert(!xe.adlnRequested && !adlnNativeRCSProfileRequested);
 args={};version="unqualified";
 xe=XE{};xe.init();xe.selectProfile(XELP::identify(0x46D28086,0));
 assert(xe.adlnRequested && !xe.adlnKernelReady && !xe.adlnNativeRequested && !xe.display13FramebufferRequested);
 assert(!adlnNativeRCSProfileRequested);
 assert(tglStart(nullptr,nullptr)==KERN_FAILURE);
 xe=XE{};xe.selectProfile(XELP::identify(0x9A498086,0));assert(!xe.tglRequested);

}
'''.replace('// ACTUAL_SOURCE', init + entry)
with tempfile.TemporaryDirectory(prefix='wex-boot-args-') as directory:
    directory = Path(directory)
    cpp, exe = directory / 'test.cpp', directory / 'test'
    cpp.write_text(fixture)
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I'+str(root/'WhateverXe'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print(f'boot namespace: {len(used-external)} options, {len(removed)} removed spellings; plugin/provider startup passed')
