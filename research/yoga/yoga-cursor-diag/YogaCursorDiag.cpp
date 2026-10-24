// Observational call wrappers only. Lilu routing DOES modify executable code.
// No cursor repair, MMIO/PCI writes, mapping changes or replacement GPU binaries.
#include <Headers/kern_api.hpp>
#include <Headers/kern_devinfo.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/plugin_start.hpp>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include "CursorMath.hpp"
#include "CursorGuards.hpp"

static_assert(sizeof(void *) == 8, "x86_64 only");
static_assert(sizeof(IOHardwareCursorInfo) == 56, "cursor ABI");
static_assert(offsetof(IOHardwareCursorInfo, cursorHeight) == 4, "height ABI");
static_assert(offsetof(IOHardwareCursorInfo, cursorWidth) == 8, "width ABI");
static_assert(offsetof(IOHardwareCursorInfo, colorMap) == 16, "map ABI");
static_assert(offsetof(IOHardwareCursorInfo, hardwareCursorData) == 24, "data ABI");
static_assert(offsetof(IOHardwareCursorInfo, cursorHotSpotX) == 32, "hotspot ABI");

using ImageFn = IOReturn (*)(void *, void *, IOHardwareCursorInfo *);
using ScaleFn = bool (*)(void *, void *, IOHardwareCursorInfo *, volatile uint32_t *, uint32_t *, uint32_t *);
using StateFn = IOReturn (*)(void *, void *, int, int, bool, bool);
using MemoryFn = void (*)(void *, void *);
using ParamsFn = void (*)(void *, void *, void *);
using NoFaultFn = vm_size_t (*)(vm_offset_t, vm_offset_t, vm_size_t);
static ImageFn orgImage;
static ScaleFn orgScale;
static StateFn orgState;
static MemoryFn orgMemory;
static ParamsFn orgParams;
static NoFaultFn noFault;
static bool eligible, imageBusy;
static uint32_t imageCalls, scaleCalls, stateCalls, memoryCalls, paramsCalls;
static const char *disableArgs[] = {"-yogacursoroff"};
static const char *paths[] = {"/Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelTGLGraphicsFramebuffer"};
static KernelPatcher::KextInfo targets[] = {
    {"com.xxxxx.driver.AppleIntelTGLGraphicsFramebuffer", paths, 1, {true, false, false, true}}
};

// Saturating counters: log ceilings remain valid even after >2^32 calls.
static uint32_t ticket(uint32_t &counter) {
    uint32_t old = __atomic_load_n(&counter, __ATOMIC_RELAXED);
    while (old != UINT32_MAX && !__atomic_compare_exchange_n(&counter, &old, old + 1,
            false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
    return old;
}
static bool copyAt(uintptr_t source, void *destination, size_t length) {
    return noFault && CursorMath::range(source, length) &&
        noFault(source, reinterpret_cast<vm_offset_t>(destination), length) == length;
}
template <typename T> static bool field(void *object, size_t offset, T &value) {
    uintptr_t base = reinterpret_cast<uintptr_t>(object);
    return offset <= UINTPTR_MAX - base && copyAt(base + offset, &value, sizeof(value));
}

struct Snapshot {
    uint32_t fb, bases[2], index, requested, dimension, width, height, mode;
    int32_t selector, x, y;
    uint8_t enabled;
    uint32_t count, regOffset, pipe, control, allocationBase, allocationLength;
    uintptr_t path, cpu;
    uint32_t scaling[4];
};
static bool snapshot(void *controller, void *fb, Snapshot &s) {
    if (!field(fb, 0x1dc, s.fb) || s.fb != 0) return false;
    if (!field(fb, 0x1f4, s.bases) || !field(fb, 0x4ad8, s.index) ||
        !field(fb, 0x4274, s.requested) || !field(fb, 0x4278, s.dimension) ||
        !field(fb, 0x4ad0, s.width) || !field(fb, 0x4ad4, s.height) ||
        !field(fb, 0x4270, s.enabled) || !field(fb, 0x427c, s.x) || !field(fb, 0x4280, s.y) ||
        !field(fb, 0x3fd8, s.selector) || !field(fb, 0x4250, s.mode) ||
        !field(fb, 0x430c, s.scaling[0]) || !field(fb, 0x4310, s.scaling[1]) ||
        !field(fb, 0x4320, s.scaling[2]) || !field(fb, 0x4324, s.scaling[3]) ||
        !field(controller, 0xdc0, s.allocationBase) || !field(controller, 0xdc8, s.allocationLength) ||
        !field(controller, 0xc98, s.cpu)) return false;
    if (!field(fb, s.selector == -1 ? 0x49e8 : 0x4a00, s.count) ||
        !s.count || s.count > 3 || !field(fb, s.selector == -1 ? 0x49f0 : 0x4a08, s.path)) return false;
    auto *path = reinterpret_cast<void *>(s.path);
    return field(path, 0x3640, s.regOffset) && field(path, 0x3644, s.pipe) &&
        field(path, 0x170, s.control) && s.index <= 1;
}
static void logSnapshot(const char *event, uint32_t seq, const Snapshot &s) {
    SYSLOG("yogacur", "%s n=%u fb=%u enable=%u xy=%d,%d request=%u side=%u image=%ux%u idx=%u bases=%08x,%08x", event, seq,
        s.fb, s.enabled, s.x, s.y, s.requested, s.dimension, s.width, s.height, s.index, s.bases[0], s.bases[1]);
    SYSLOG("yogacur", "%s n=%u selector=%d paths=%u pipe=%u regoff=%x ctlShadow=%08x alloc=%08x/%x mapped=%d mode=%x scale=%x,%x,%x,%x", event, seq,
        s.selector, s.count, s.pipe, s.regOffset, s.control, s.allocationBase, s.allocationLength,
        s.cpu != 0, s.mode, s.scaling[0], s.scaling[1], s.scaling[2], s.scaling[3]);
}

struct Pixels {
    uint32_t words[CursorMath::SampleRows][CursorMath::SampleColumns];
    uint32_t width, height, columns, validRows, digest;
};
static void sample(uintptr_t address, uint32_t stride, uint32_t width, uint32_t height, Pixels &p) {
    p = {};
    if (!CursorMath::dimensions(width, height) || stride < width || stride > 256) return;
    p.width = width; p.height = height;
    p.columns = width < CursorMath::SampleColumns ? width : CursorMath::SampleColumns;
    p.digest = 2166136261U;
    for (uint32_t i = 0; i < CursorMath::SampleRows; i++) {
        size_t offset = CursorMath::row(i, height) * stride * 4;
        if (offset > UINTPTR_MAX - address || !copyAt(address + offset, p.words[i], p.columns * 4)) continue;
        p.validRows |= 1U << i;
        for (uint32_t j = 0; j < p.columns; j++)
            p.digest = (p.digest ^ p.words[i][j]) * 16777619U;
    }
}
static void compare(uint32_t seq, uint32_t buffer, const Pixels &src, const Pixels &dst) {
    uint32_t compared = 0, mismatches = 0;
    if (src.width == dst.width && src.height == dst.height && src.columns == dst.columns) {
        for (uint32_t i = 0; i < CursorMath::SampleRows; i++) {
            if (!(src.validRows & dst.validRows & (1U << i))) continue;
            for (uint32_t j = 0; j < src.columns; j++) {
                compared++;
                mismatches += CursorMath::convert(src.words[i][j]) != dst.words[i][j];
            }
        }
    }
    SYSLOG("yogacur", "pixels n=%u buffer=%u srcRows=%02x dstRows=%02x srcHash=%08x dstHash=%08x cpuCompared=%u cpuMismatch=%u (comparison valid only for non-scaled fallback)",
        seq, buffer, src.validRows, dst.validRows, src.digest, dst.digest, compared, mismatches);
}
static void logPixels(const char *kind, uint32_t seq, uint32_t buffer, const Pixels &p) {
    for (uint32_t r = 0; r < CursorMath::SampleRows; r++) {
        if (!(p.validRows & (1U << r))) continue;
        auto *w = p.words[r];
        SYSLOG("yogacur", "%s n=%u buffer=%u row=%u cols=%u argb=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x", kind, seq, buffer,
            CursorMath::row(r, p.height), p.columns, w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7],w[8],w[9],w[10],w[11],w[12],w[13],w[14],w[15]);
    }
}

static IOReturn wrapImage(void *controller, void *fb, IOHardwareCursorInfo *cursor) {
    uint32_t n = ticket(imageCalls), id = UINT32_MAX;
    bool expected = false;
    bool capture = CursorMath::selectedImage(n) && field(fb, 0x1dc, id) && id == 0 &&
        __atomic_compare_exchange_n(&imageBusy, &expected, true, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
    if (!capture) return orgImage(controller, fb, cursor);
    IOHardwareCursorInfo info {};
    Pixels source {};
    bool valid = copyAt(reinterpret_cast<uintptr_t>(cursor), &info, sizeof(info)) &&
        CursorMath::dimensions(info.cursorWidth, info.cursorHeight);
    Snapshot before {}, after {};
    if (snapshot(controller, fb, before)) logSnapshot("image-before", n, before);
    if (valid) sample(reinterpret_cast<uintptr_t>(info.hardwareCursorData), info.cursorWidth,
        info.cursorWidth, info.cursorHeight, source);
    // No driver lock is acquired, no pointer retained, original exactly once.
    uint32_t scalesBefore = __atomic_load_n(&scaleCalls, __ATOMIC_RELAXED);
    IOReturn result = orgImage(controller, fb, cursor);
    uint32_t scalesAfter = __atomic_load_n(&scaleCalls, __ATOMIC_RELAXED);
    SYSLOG("yogacur", "image-return n=%u ret=%08x infoValid=%d version=%u/%u size=%ux%u hotspot=%u,%u colorMap=%d",
        n, result, valid, info.majorVersion, info.minorVersion, info.cursorWidth, info.cursorHeight,
        info.cursorHotSpotX, info.cursorHotSpotY, info.colorMap != nullptr);
    SYSLOG("yogacur", "image-scale-window n=%u globalScaleCalls=%u,%u (not a per-thread correlation)", n, scalesBefore, scalesAfter);
    if (snapshot(controller, fb, after)) {
        logSnapshot("image-after", n, after);
        // Both candidates are sampled AFTER the call, never guessed from before.index.
        for (uint32_t b = 0; b < 2 && valid; b++) {
            uint32_t offset = 0;
            Pixels destination {};
            bool bounded = info.cursorWidth <= after.dimension && info.cursorHeight <= after.dimension &&
                CursorMath::bufferOffset(after.bases[b], after.allocationBase, after.allocationLength, after.dimension, offset) &&
                offset <= UINTPTR_MAX - after.cpu;
            if (bounded) sample(after.cpu + offset, after.dimension, info.cursorWidth, info.cursorHeight, destination);
            SYSLOG("yogacur", "buffer n=%u candidate=%u selected=%d bounded=%d offset=%x", n, b, b == after.index, bounded, offset);
            compare(n, b, source, destination);
            logPixels("destination", n, b, destination);
        }
    } else SYSLOG("yogacur", "image-after n=%u snapshot unavailable", n);
    // Small raw input windows; padding is zero, validRows marks actual copied rows.
    logPixels("source", n, 0, source);
    __atomic_store_n(&imageBusy, false, __ATOMIC_RELEASE);
    return result;
}
static bool wrapScale(void *controller, void *fb, IOHardwareCursorInfo *info, volatile uint32_t *destination, uint32_t *special, uint32_t *alpha) {
    uint32_t n = ticket(scaleCalls);
    bool result = orgScale(controller, fb, info, destination, special, alpha);
    uint32_t id, a = 0, b = 0;
    if (n < 64 && field(fb, 0x1dc, id) && id == 0) {
        bool counts = copyAt(reinterpret_cast<uintptr_t>(special), &a, 4) && copyAt(reinterpret_cast<uintptr_t>(alpha), &b, 4);
        SYSLOG("yogacur", "scale n=%u ret=%d countsValid=%d special=%u alpha=%u", n, result, counts, a, b);
    }
    return result;
}
static IOReturn wrapState(void *controller, void *fb, int x, int y, bool enable, bool force) {
    uint32_t n = ticket(stateCalls);
    IOReturn result = orgState(controller, fb, x, y, enable, force);
    Snapshot s {};
    if ((n < 32 || (n < 4096 && n % 128 == 0)) && snapshot(controller, fb, s)) {
        SYSLOG("yogacur", "state-return n=%u ret=%08x requestedXY=%d,%d enable=%d force=%d", n, result, x, y, enable, force);
        logSnapshot("state-after", n, s);
    }
    return result;
}
static void wrapMemory(void *controller, void *fb) {
    uint32_t n = ticket(memoryCalls);
    orgMemory(controller, fb);
    Snapshot s {};
    if (n < 16 && snapshot(controller, fb, s)) logSnapshot("memory-after", n, s);
}
static void wrapParams(void *controller, void *fb, void *path) {
    uint32_t n = ticket(paramsCalls);
    orgParams(controller, fb, path);
    uint32_t id, control = 0, pipe = 0;
    if (n < 32 && field(fb, 0x1dc, id) && id == 0 && field(path, 0x170, control) && field(path, 0x3644, pipe))
        SYSLOG("yogacur", "params n=%u pipe=%u ctlShadow=%08x (not live MMIO)", n, pipe, control);
}

static void checkHardware(void *, KernelPatcher &patcher) {
    if (!checkKernelArgument("-yogacursordiag")) return;
    auto *info = DeviceInfo::create();
    if (!info || !info->videoBuiltin) {
        if (info) DeviceInfo::deleter(info);
        SYSLOG("yogacur", "hardware unavailable; inert");
        return;
    }
    auto *gpu = info->videoBuiltin;
    uint32_t vendor = WIOKit::readPCIConfigValue(gpu, WIOKit::kIOPCIConfigVendorID, 0, 16);
    uint32_t device = WIOKit::readPCIConfigValue(gpu, WIOKit::kIOPCIConfigDeviceID, 0, 16);
    uint32_t ggc = WIOKit::readPCIConfigValue(gpu, WIOKit::kIOPCIConfigGraphicsControl, 0, 16);
    // Board identity is NOT proven by the GPU guard; require acknowledgement.
    // SMBIOS model may be spoofed; do NOT claim that it identifies physical Yoga.
    bool acknowledged = checkKernelArgument("-yogacursor82ut");
    eligible = acknowledged && vendor == 0x8086 && (device == 0x46a6 || device == 0x9a49) && ggc == 0xfec1;
    SYSLOG("yogacur", "guard vendor=%04x device=%04x GGC=%04x acknowledged82UT=%d eligible=%d", vendor, device, ggc, acknowledged, eligible);
    DeviceInfo::deleter(info);
    if (!eligible) return;
    patcher.clearError();
    auto address = patcher.solveSymbol(KernelPatcher::KernelID, "_ml_nofault_copy");
    bool resolved = address && patcher.getError() == KernelPatcher::Error::NoError;
    patcher.clearError();
    if (!resolved || !CursorMath::matches(reinterpret_cast<const uint8_t *>(address), guardNoFault, sizeof(guardNoFault))) {
        eligible = false;
        SYSLOG("yogacur", "nofault resolution/byte guard failed; inert");
        return;
    }
    noFault = reinterpret_cast<NoFaultFn>(address);
    uint32_t probe = 0x59c0a17e, copied = 0;
    if (!copyAt(reinterpret_cast<uintptr_t>(&probe), &copied, sizeof(copied)) || copied != probe) {
        noFault = nullptr;
        eligible = false;
        SYSLOG("yogacur", "nofault local-RAM self-test failed; inert");
    }
}
static void loaded(void *, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
    if (!eligible || index != targets[0].loadIndex || targets[0].loadIndex == KernelPatcher::KextInfo::Unloaded) return;
    const char *symbols[] = {symImage, symScale, symState, symMemory, symParams};
    const uint8_t *guards[] = {guardImage, guardScale, guardState, guardMemory, guardParams};
    for (size_t i = 0; i < 5; i++) {
        patcher.clearError();
        auto function = patcher.solveSymbol(index, symbols[i], address, size);
        bool ok = function && patcher.getError() == KernelPatcher::Error::NoError && size >= 48 &&
            function >= address && function - address <= size - 48;
        patcher.clearError();
        if (!ok || !CursorMath::matches(reinterpret_cast<const uint8_t *>(function), guards[i], 48)) {
            SYSLOG("yogacur", "cursor site %lu guard failed; no routes installed", i);
            return;
        }
    }
    KernelPatcher::RouteRequest routes[] = {
        {symImage, wrapImage, orgImage}, {symScale, wrapScale, orgScale},
        {symState, wrapState, orgState}, {symMemory, wrapMemory, orgMemory},
        {symParams, wrapParams, orgParams}
    };
    bool ok = patcher.routeMultiple(index, routes, address, size);
    SYSLOG("yogacur", "routes result=%d error=%d; partial routing is possible on failure; no repair enabled", ok, static_cast<int>(patcher.getError()));
    patcher.clearError();
}
PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME), parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal, disableArgs, 1, nullptr, 0, nullptr, 0,
    KernelVersion::Tahoe, KernelVersion::Tahoe,
    []() {
        if (!checkKernelArgument("-yogacursordiag") || !checkKernelArgument("-yogacursor82ut")) return;
        lilu.onPatcherLoadForce(checkHardware);
        lilu.onKextLoadForce(targets, 1, loaded);
    }
};
