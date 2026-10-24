"""Tests run locally, against exact installed drivers and the real plugin source."""
from pathlib import Path
import json
import plistlib
import re
import subprocess
import tempfile
import unittest
from yoga_fb_match_patch import ROOT, FB, NG, PATCHES, symbol, file_offset, sha
from decode_panic_efivars import decode, unpack

STAGE = ROOT / 'cross-build/yoga-gms-runtime-v2'
ACTIVE = ROOT / 'postmatch-efi-nvram-readonly-20261024-011020/copies/external-esp/EFI/OC/config.plist'
SOURCE = ROOT / 'yoga-gms-runtime/YogaGMSFix.cpp'


def arrays():
    return {name: bytes(int(x.strip(), 16) for x in contents.split(','))
            for name, contents in re.findall(r'uint8_t (\w+)\[\] = \{([^}]+)\}', SOURCE.read_text())}


class RuntimeDeliveryTests(unittest.TestCase):
    def test_panic_is_latest_recurrence(self):
        text, report = decode(ACTIVE.parents[3] / 'panic-efivars')
        self.assertEqual(report['uptime_ns'], 130316154851)
        self.assertIn('__ZNK11IOAccelTask7releaseEv + 0x95', text)
        self.assertIn('@0xffffff7f97527000', text)

    def test_pack_round_trip_and_partial_rejection(self):
        plain = b'panic\nAB01234567'
        packed = b''.join(sum(c << (7*j) for j, c in enumerate(plain[i:i+8])).to_bytes(7, 'little')
                          for i in range(0, len(plain), 8))
        self.assertEqual(unpack(packed), plain)
        with self.assertRaises(ValueError): unpack(packed[:-1])

    def test_patch_arrays_match_prior_exact_specs(self):
        config = plistlib.loads(ACTIVE.read_bytes())
        hw = next(p for p in config['Kernel']['Patch'] if 'GMS' in p['Comment'] and p['Base'].startswith('__ZN15'))
        a = arrays()
        self.assertEqual((a['hwOld'], a['hwNew']), (hw['Find'], hw['Replace']))
        self.assertEqual((a['fbOld'], a['fbNew']), (PATCHES[0]['Find'], PATCHES[0]['Replace']))

    def test_config_adds_only_companion_and_keeps_matching(self):
        from deploy_yoga_gms_runtime import build_config
        original, payload = build_config()
        old, new = plistlib.loads(original), plistlib.loads(payload)
        position = next(i for i, x in enumerate(new['Kernel']['Add']) if x['BundlePath'] == 'YogaGMSFix.kext')
        self.assertEqual(new['Kernel']['Add'][position - 1]['BundlePath'], 'NootedGreen.kext')
        self.assertEqual(new['Kernel']['Add'][position]['MinKernel'], '25.0.0')
        del new['Kernel']['Add'][position]
        self.assertEqual(old, new)

    def test_new_plugin_signature_and_dependency_order(self):
        from audit_signature_pages import audit_file
        from deploy_yoga_gms_runtime import PLUGIN_HASH
        binary = STAGE / 'YogaGMSFix.kext/Contents/MacOS/YogaGMSFix'
        self.assertEqual(sha(binary.read_bytes()), PLUGIN_HASH)
        a = audit_file(binary)
        self.assertEqual(a['slices'][0]['filetype'], 11)
        for d in a['slices'][0]['directories']:
            self.assertEqual(d['page_shift'], 12)
            self.assertTrue(d['code_hashes_valid'])
        info = plistlib.loads((binary.parent.parent / 'Info.plist').read_bytes())
        self.assertIn('com.StezzaPilot.NootedGreen', info['OSBundleLibraries'])
        self.assertIn('com.apple.kpi.dsep', info['OSBundleLibraries'])

    def test_exact_auxkc_and_symbol_relative_locations(self):
        kc = (ROOT / 'yoga-fb-match-apply-20261023-235655/AuxiliaryKernelExtensions.readonly.kc').read_bytes()
        self.assertEqual(sha(kc), '018133be9c1b2c92fff6b51e3cae5f3324bbaa15ba3e896f7635fbc6444849e9')
        hw = ROOT / 'installed-signatures-readonly-20261023-233323/copies/data/Library/Extensions/AppleIntelTGLGraphics.kext/Contents/MacOS/AppleIntelTGLGraphics'
        for binary, name, offset, old in [(hw, '__ZN15IGMemoryManager12initSegmentsEv', 0x40, arrays()['hwOld']),
                                         (FB, '__ZN24AppleIntelBaseController13FBMemMgr_InitEv', 0xad, arrays()['fbOld'])]:
            data = binary.read_bytes()
            at = file_offset(data, symbol(data, name)) + offset
            self.assertEqual(data[at:at+len(old)], old)
            self.assertEqual(data.count(old), 1)
            self.assertEqual(kc.count(old), 1)

    def test_imports_subset_of_active_nootedgreen(self):
        def names(path, kind):
            return {l.split()[-1] for l in subprocess.check_output(['llvm-nm', kind, str(path)], text=True).splitlines() if l.split()}
        new = names(STAGE / 'YogaGMSFix.kext/Contents/MacOS/YogaGMSFix', '--undefined-only')
        active = names(NG, '--undefined-only')
        self.assertFalse(new - active)
        self.assertFalse(any('atexit' in x for x in new))
        exports = names(NG.parents[3] / 'Lilu.kext/Contents/MacOS/Lilu', '--defined-only')
        api = {x for x in new if any(s in x for s in ['KernelPatcher', 'DeviceInfo', 'LiluAPI', 'WIOKit'])}
        self.assertFalse(api - exports)

    def test_actual_source_with_mocked_runtime(self):
        # Execute the unmodified source's guard/callback logic. This is NOT a
        # macOS runtime test; the Mach write API and loader are mocked here.
        stub = r'''
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
using mach_vm_address_t = uint64_t;
#define SYSLOG(...) ((void)0)
#define ADDPR(x) x
#define xStringify(x) "YogaGMSFix"
constexpr size_t parseModuleVersion(const char *) { return 100; }
enum class KernelVersion { Tahoe };
struct IORegistryEntry {};
namespace WIOKit {
enum {kIOPCIConfigVendorID, kIOPCIConfigDeviceID, kIOPCIConfigGraphicsControl};
static uint32_t vendor=0x8086, device=0x46a6, ggc=0xfec1;
uint32_t readPCIConfigValue(IORegistryEntry *, uint32_t reg, uint32_t, uint32_t) {
return reg==0?vendor:reg==1?device:ggc;
}}
struct DeviceInfo {
IORegistryEntry *videoBuiltin;
static DeviceInfo *create() { static IORegistryEntry gpu; static DeviceInfo info{&gpu}; return &info; }
static void deleter(DeviceInfo *) {}
};
struct KernelPatcher {
enum class Error { NoError, NoSymbolFound, MemoryProtection };
struct KextInfo { static constexpr size_t Unloaded=0; const char *id; const char **paths; size_t pathNum; bool sys[6]; size_t loadIndex=0; };
struct LookupPatch { KextInfo *kext; const uint8_t *find; const uint8_t *replace; size_t size; size_t count; };
Error error=Error::NoError;
uint64_t function=0;
int writes=0;
bool failWrite=false;
void clearError() { error=Error::NoError; }
Error getError() { return error; }
uint64_t solveSymbol(size_t, const char *) { return function; }
uint64_t solveSymbol(size_t i, const char *s, uint64_t, size_t) { return solveSymbol(i,s); }
void applyLookupPatch(LookupPatch *p, uint8_t *at, size_t maxSize) {
assert(maxSize==p->size+1 && p->count==1);
if(failWrite) {error=Error::MemoryProtection; return;}
assert(!memcmp(at,p->find,p->size)); memcpy(at,p->replace,p->size); writes++;
}
};
struct LiluAPI {
static constexpr int AllowNormal=1;
void onPatcherLoadForce(void (*)(void*,KernelPatcher&)) {}
void onKextLoadForce(KernelPatcher::KextInfo*,int,void (*)(void*,KernelPatcher&,size_t,uint64_t,size_t)) {}
} lilu;
struct PluginConfiguration {
const char *product; size_t version; uint32_t mode; const char **disable; size_t ndisable;
const char **debug; size_t ndebug; const char **beta; size_t nbeta;
KernelVersion min,max; void (*start)();
};
'''
        harness = r'''
int main() {
KernelPatcher p; checkHardware(nullptr,p); assert(eligible);
WIOKit::device=0x9a49; checkHardware(nullptr,p); assert(eligible);
for(auto device : {0x25a9u,0xa80au,0x9a40u}) {WIOKit::device=device; checkHardware(nullptr,p); assert(!eligible);}
WIOKit::device=0x46a6; WIOKit::vendor=0x10de; checkHardware(nullptr,p); assert(!eligible);
WIOKit::vendor=0x8086; WIOKit::ggc=0x100; checkHardware(nullptr,p); assert(!eligible);
WIOKit::ggc=0xfec1; checkHardware(nullptr,p); assert(eligible);
targets[0].loadIndex=6; targets[1].loadIndex=4;
for(size_t i=0;i<2;i++) {
uint8_t memory[512]; memset(memory,0xcc,sizeof(memory));
auto *oldBytes=i?fbOld:hwOld; auto *newBytes=i?fbNew:hwNew;
size_t count=i?sizeof(fbOld):sizeof(hwOld), offset=i?0xad:0x40;
memcpy(memory+offset,oldBytes,count); p.function=reinterpret_cast<uint64_t>(memory);
int before=p.writes;
patchLoaded(nullptr,p,99,p.function,sizeof(memory)); assert(p.writes==before);
eligible=false; patchLoaded(nullptr,p,targets[i].loadIndex,p.function,sizeof(memory)); assert(p.writes==before); eligible=true;
patchLoaded(nullptr,p,targets[i].loadIndex,p.function,offset+count); assert(p.writes==before);
memory[offset]^=1; patchLoaded(nullptr,p,targets[i].loadIndex,p.function,sizeof(memory)); assert(p.writes==before); memory[offset]^=1;
p.failWrite=true; patchLoaded(nullptr,p,targets[i].loadIndex,p.function,sizeof(memory)); assert(p.writes==before && !memcmp(memory+offset,oldBytes,count)); p.failWrite=false;
patchLoaded(nullptr,p,targets[i].loadIndex,p.function,sizeof(memory)); assert(p.writes==before+1 && !memcmp(memory+offset,newBytes,count));
for(size_t n=0;n<sizeof(memory);n++) if(n<offset||n>=offset+count) assert(memory[n]==0xcc);
patchLoaded(nullptr,p,targets[i].loadIndex,p.function,sizeof(memory)); assert(p.writes==before+1);
}
}
'''
        with tempfile.TemporaryDirectory(prefix='gms-tests-', dir='/tmp/opencode') as tmp:
            tmp = Path(tmp)
            (tmp / 'Headers').mkdir()
            for name in ['kern_api.hpp', 'kern_devinfo.hpp', 'kern_iokit.hpp', 'plugin_start.hpp']:
                (tmp / 'Headers' / name).write_text('')
            cpp = tmp / 'test.cpp'
            cpp.write_text('#include <initializer_list>\n' + stub + '\n#include "' + str(SOURCE) + '"\n' + harness)
            subprocess.run(['clang++', '-std=c++17', '-I' + str(tmp), str(cpp), '-o', str(tmp / 'test')], check=True, capture_output=True)
            subprocess.run([str(tmp / 'test')], check=True, capture_output=True)


if __name__ == '__main__': unittest.main()
