// Experimental Wa_22012358565 delivery for this captured ADL-P framebuffer.
// Disabled unless explicitly opted in. No function hooks, pixel reads, or direct
// MMIO/PCI writes. Apple retains cursor enable/disable and register programming.
#include <Headers/kern_api.hpp>
#include <Headers/kern_devinfo.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/plugin_start.hpp>
#include "CursorArbGuard.hpp"

static bool eligible;
static const char *disableArgs[] = {"-yogacursorarboff"};
static const char *paths[] = {
    "/Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelTGLGraphicsFramebuffer"
};
static KernelPatcher::KextInfo targets[] = {
    {"com.xxxxx.driver.AppleIntelTGLGraphicsFramebuffer", paths, 1, {true, false, false, true}}
};

static void checkHardware(void *, KernelPatcher &) {
    eligible = false;
    if (!checkKernelArgument("-yogacursorarb")) return;
    // Actual CPUID, not the kernel's corrected/spoofed CPU model.
    uint32_t a = 1, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    uint32_t family = (a >> 8) & 0xf;
    uint32_t model = ((a >> 4) & 0xf) | ((a >> 12) & 0xf0);
    if (family != 6 || model != 0x9a) {
        SYSLOG("yogaarb", "ADL-P CPU guard failed; no changes");
        return;
    }
    auto *info = DeviceInfo::create();
    if (!info || !info->videoBuiltin) {
        if (info) DeviceInfo::deleter(info);
        return;
    }
    auto *gpu = info->videoBuiltin;
    uint32_t vendor = WIOKit::readPCIConfigValue(gpu, WIOKit::kIOPCIConfigVendorID, 0, 16);
    uint32_t device = WIOKit::readPCIConfigValue(gpu, WIOKit::kIOPCIConfigDeviceID, 0, 16);
    uint32_t ggc = WIOKit::readPCIConfigValue(gpu, WIOKit::kIOPCIConfigGraphicsControl, 0, 16);
    eligible = vendor == 0x8086 && (device == 0x46a6 || device == 0x9a49) && ggc == 0xfec1;
    SYSLOG("yogaarb", "CPU=%x vendor=%04x device=%04x GGC=%04x eligible=%d", a, vendor, device, ggc, eligible);
    DeviceInfo::deleter(info);
}

static void patchLoaded(void *, KernelPatcher &patcher, size_t index,
                        mach_vm_address_t address, size_t size) {
    if (!eligible || targets[0].loadIndex == KernelPatcher::KextInfo::Unloaded ||
        index != targets[0].loadIndex) return;
    patcher.clearError();
    auto function = patcher.solveSymbol(index, cursorArbSymbol, address, size);
    bool resolved = function && patcher.getError() == KernelPatcher::Error::NoError;
    patcher.clearError();
    constexpr size_t length = sizeof(cursorArbOld);
    static_assert(length == sizeof(cursorArbNew), "same-length instruction patch");
    if (!resolved || size < length + 1 || function < address ||
        function - address > size - length - 1) {
        SYSLOG("yogaarb", "symbol/range guard failed; no write");
        return;
    }
    auto *at = reinterpret_cast<uint8_t *>(function);
    if (!memcmp(at, cursorArbNew, length)) {
        SYSLOG("yogaarb", "cursor arbitration candidate already present");
        return;
    }
    if (memcmp(at, cursorArbOld, length)) {
        SYSLOG("yogaarb", "whole-function byte guard failed; no write");
        return;
    }
    // One lookup/replacement contains all three constants. No partial sequence
    // of separate patches; the disable and invalid-size branches stay unchanged.
    KernelPatcher::LookupPatch patch {&targets[0], cursorArbOld, cursorArbNew, length, 1};
    patcher.applyLookupPatch(&patch, at, length + 1);
    auto error = patcher.getError();
    bool verified = error == KernelPatcher::Error::NoError && !memcmp(at, cursorArbNew, length);
    patcher.clearError();
    SYSLOG("yogaarb", "experimental Wa_22012358565 slots=1 bytesVerified=%d error=%d; visual result unvalidated",
           verified, static_cast<int>(error));
}

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME), parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal, disableArgs, 1, nullptr, 0, nullptr, 0,
    KernelVersion::Tahoe, KernelVersion::Tahoe,
    []() {
        lilu.onPatcherLoadForce(checkHardware);
        lilu.onKextLoadForce(targets, 1, patchLoaded);
    }
};
