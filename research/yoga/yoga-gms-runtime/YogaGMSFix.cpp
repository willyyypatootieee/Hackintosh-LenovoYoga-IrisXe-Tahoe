// Narrow runtime delivery of the two exact 25G241/TGL GMS corrections.
// No MMIO writes, PCI writes, task hooks, display routing or matching changes.
#include <Headers/kern_api.hpp>
#include <Headers/kern_devinfo.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/plugin_start.hpp>

static bool eligible = false;
static const char *disableArg[] = {"-yogagmsoff"};
static const char *pathsHW[] = {
    "/Library/Extensions/AppleIntelTGLGraphics.kext/Contents/MacOS/AppleIntelTGLGraphics"
};
static const char *pathsFB[] = {
    "/Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelTGLGraphicsFramebuffer"
};
static KernelPatcher::KextInfo targets[] = {
    {"com.xxxxx.driver.AppleIntelTGLGraphics", pathsHW, 1, {true, false, false, true}},
    {"com.xxxxx.driver.AppleIntelTGLGraphicsFramebuffer", pathsFB, 1, {true, false, false, true}}
};

static const uint8_t hwOld[] = {0x44,0x0f,0xb7,0xf0,0x41,0x81,0xe6,0x00,0xff,0x00,0x00,0x49,0xc1,0xe6,0x11};
static const uint8_t hwNew[] = {0x49,0xc7,0xc6,0x00,0x00,0xc0,0x03,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
static const uint8_t fbOld[] = {0xc1,0xe0,0x11,0x25,0x00,0x00,0x00,0xfe};
static const uint8_t fbNew[] = {0xb8,0x00,0x00,0xc0,0x03,0x90,0x90,0x90};

static void checkHardware(void *, KernelPatcher &) {
    auto *info = DeviceInfo::create();
    if (!info || !info->videoBuiltin) {
        SYSLOG("yogagms", "hardware guard unavailable; no changes");
        if (info) DeviceInfo::deleter(info);
        return;
    }
    auto *gpu = info->videoBuiltin;
    uint32_t vendor = WIOKit::readPCIConfigValue(gpu, WIOKit::kIOPCIConfigVendorID, 0, 16);
    uint32_t device = WIOKit::readPCIConfigValue(gpu, WIOKit::kIOPCIConfigDeviceID, 0, 16);
    uint32_t ggc = WIOKit::readPCIConfigValue(gpu, WIOKit::kIOPCIConfigGraphicsControl, 0, 16);
    // NootedGreen may expose the existing 9A49 spoof instead of physical 46A6.
    eligible = vendor == 0x8086 && (device == 0x46a6 || device == 0x9a49) && ggc == 0xfec1;
    SYSLOG("yogagms", "guard vendor=%04x device=%04x GGC=%04x eligible=%d", vendor, device, ggc, eligible);
    DeviceInfo::deleter(info);
}

static void patchLoaded(void *, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    if (!eligible) return;
    for (size_t i = 0; i < 2; i++) {
        if (targets[i].loadIndex == KernelPatcher::KextInfo::Unloaded || index != targets[i].loadIndex) continue;
        const bool hw = i == 0;
        const char *symbol = hw ? "__ZN15IGMemoryManager12initSegmentsEv" : "__ZN24AppleIntelBaseController13FBMemMgr_InitEv";
        const uint8_t *oldBytes = hw ? hwOld : fbOld;
        const uint8_t *newBytes = hw ? hwNew : fbNew;
        size_t count = hw ? sizeof(hwOld) : sizeof(fbOld);
        size_t offset = hw ? 0x40 : 0xad;
        patcher.clearError();
        auto function = patcher.solveSymbol(index, symbol, address, size);
        bool resolved = function != 0 && patcher.getError() == KernelPatcher::Error::NoError;
        patcher.clearError();
        if (!resolved || size < offset + count + 1 || function < address || function - address > size - offset - count - 1) {
            SYSLOG("yogagms", "%s symbol/range guard failed; no write", hw ? "HW" : "FB");
            return;
        }
        auto *at = reinterpret_cast<uint8_t *>(function + offset);
        if (!memcmp(at, newBytes, count)) {
            SYSLOG("yogagms", "%s GMS already corrected", hw ? "HW" : "FB");
            return;
        }
        if (memcmp(at, oldBytes, count)) {
            SYSLOG("yogagms", "%s exact-byte guard failed; no write", hw ? "HW" : "FB");
            return;
        }
        KernelPatcher::LookupPatch patch {&targets[i], oldBytes, newBytes, count, 1};
        // Lilu's loop uses currentAddress < endingAddress; include one extra byte.
        patcher.applyLookupPatch(&patch, at, count + 1);
        auto error = patcher.getError();
        bool verified = error == KernelPatcher::Error::NoError && !memcmp(at, newBytes, count);
        patcher.clearError();
        SYSLOG("yogagms", "%s GMS FE=60MiB runtime patch verified=%d error=%d", hw ? "HW" : "FB", verified, static_cast<int>(error));
        return;
    }
}

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME), parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal, disableArg, 1, nullptr, 0, nullptr, 0,
    KernelVersion::Tahoe, KernelVersion::Tahoe,
    []() {
        lilu.onPatcherLoadForce(checkHardware);
        lilu.onKextLoadForce(targets, 2, patchLoaded);
    }
};
