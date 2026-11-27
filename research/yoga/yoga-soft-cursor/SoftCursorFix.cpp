// Experimental opt-in Lilu plugin that routes AppleIntelFramebuffer::setCursorImage()
// onto Apple's own native software-cursor fallback path (kIOReturnUnsupported /
// 0xE00002C7), allowing WindowServer to composite the cursor in Metal instead of
// using the ADL-P hardware cursor plane.
//
// Disabled unless explicitly opted in via boot-arg: -swcursor
// Disable override: -swcursoroff
// No function hooks, pixel reads, or direct MMIO/PCI writes.
#include <Headers/kern_api.hpp>
#include <Headers/plugin_start.hpp>

static bool enabled = false;
static const char *disableArgs[] = {"-swcursoroff"};
static const char *pathsFB[] = {
    "/Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelTGLGraphicsFramebuffer"
};
static KernelPatcher::KextInfo targets[] = {
    {"com.xxxxx.driver.AppleIntelTGLGraphicsFramebuffer", pathsFB, 1, {true, false, false, true}}
};

// AppleIntelFramebuffer::setCursorImage(void *) + 0x154 (Tahoe 25G241 TGL FB):
//   +0x154: 84 c0        testb %al, %al
//   +0x156: 74 58        je    +0x58  (-> +0x1b0: sets controller+0xd54,
//                                         releases controller+0xd0 lock,
//                                         returns kIOReturnUnsupported / 0xE00002C7)
//   +0x158: 8b 45 a4     movl  -0x5c(%rbp), %eax
//   +0x15b: 8b 4d a8     movl  -0x58(%rbp), %ecx
//
// Patch changes only 1 byte: 0x74 (je) -> 0xeb (jmp), guarded by a 10-byte exact match.
static constexpr size_t kPatchOffset = 0x154;
static const uint8_t kOldBytes[] = {0x84, 0xc0, 0x74, 0x58, 0x8b, 0x45, 0xa4, 0x8b, 0x4d, 0xa8};
static const uint8_t kNewBytes[] = {0x84, 0xc0, 0xeb, 0x58, 0x8b, 0x45, 0xa4, 0x8b, 0x4d, 0xa8};
static_assert(sizeof(kOldBytes) == sizeof(kNewBytes), "same-length instruction patch");

static void patchLoaded(void *, KernelPatcher &patcher, size_t index,
                        mach_vm_address_t address, size_t size) {
    if (!enabled || targets[0].loadIndex == KernelPatcher::KextInfo::Unloaded ||
        index != targets[0].loadIndex) return;

    patcher.clearError();
    auto fn = patcher.solveSymbol(index, "__ZN21AppleIntelFramebuffer14setCursorImageEPv", address, size);
    bool resolved = fn != 0 && patcher.getError() == KernelPatcher::Error::NoError;
    patcher.clearError();
    if (!resolved || size < kPatchOffset + sizeof(kOldBytes) + 1 ||
        fn < address || fn - address > size - kPatchOffset - sizeof(kOldBytes) - 1) {
        SYSLOG("swcursor", "symbol/range guard failed; no write");
        return;
    }

    auto *at = reinterpret_cast<uint8_t *>(fn + kPatchOffset);
    if (!memcmp(at, kNewBytes, sizeof(kNewBytes))) {
        SYSLOG("swcursor", "software-cursor fallback already patched");
        return;
    }
    if (memcmp(at, kOldBytes, sizeof(kOldBytes))) {
        SYSLOG("swcursor", "exact-byte guard failed; no write");
        return;
    }

    KernelPatcher::LookupPatch patch {&targets[0], kOldBytes, kNewBytes, sizeof(kOldBytes), 1};
    patcher.applyLookupPatch(&patch, at, sizeof(kOldBytes) + 1);
    auto err = patcher.getError();
    bool verified = err == KernelPatcher::Error::NoError && !memcmp(at, kNewBytes, sizeof(kNewBytes));
    patcher.clearError();
    SYSLOG("swcursor", "setCursorImage -> native SW fallback verified=%d error=%d",
           verified, static_cast<int>(err));
}

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME), parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal, disableArgs, 1, nullptr, 0, nullptr, 0,
    KernelVersion::Tahoe, KernelVersion::Tahoe,
    []() {
        enabled = checkKernelArgument("-swcursor");
        if (!enabled) return;
        lilu.onKextLoadForce(targets, 1, patchLoaded);
    }
};
