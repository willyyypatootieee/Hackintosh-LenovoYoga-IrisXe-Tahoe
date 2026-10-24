#include "NativeIokitMapping.hpp"
#include <libkern/OSAtomic.h>
#include <libkern/OSByteOrder.h>
#include <libkern/c++/OSMetaClass.h>
#include <kern/task.h>
#include <string.h>

namespace YogaTypec {
namespace {
template<class T> T field(const void *object, size_t offset) {
    T value;
    memcpy(&value, static_cast<const uint8_t *>(object) + offset, sizeof(value));
    return value;
}
bool objectClass(const OSObject *object, const char *name, size_t minimum) {
    return object && object->metaCast(name) && object->getMetaClass() &&
        object->getMetaClass()->getClassSize() >= minimum;
}
uintptr_t address(const void *p) { return reinterpret_cast<uintptr_t>(p); }
}

bool NativeIokitMapping::lockMapping() {
    if (held || !lifetime.exactBuildVerified() || !lifetime.tryEnter()) return false;
    held = true;
    return true;
}
void NativeIokitMapping::unlockMapping() {
    // Do not release the lifecycle boundary with live references left behind.
    // The generic DSSM wrapper calls releaseMapping first on all paths.
    if (!held || controller || provider || manager || mapping || descriptor || bar) return;
    held = false;
    lifetime.leave();
}
bool NativeIokitMapping::authorized(Route r, uint64_t e) {
    return held && r.valid() && r.tcIndex == 2 && e &&
        lifetime.exactBuildVerified() && lifetime.current(source, r, e);
}
bool NativeIokitMapping::gpuIdentity() const {
    if (!provider || provider->isInactive() || provider->getBusNumber() != 0 ||
        provider->getDeviceNumber() != 2 || provider->getFunctionNumber() != 0) return false;
    const uint16_t vendor = provider->configRead16(uint8_t(0));
    const uint16_t device = provider->configRead16(uint8_t(2));
    const uint16_t command = provider->configRead16(uint8_t(4));
    const uint16_t ggc = provider->configRead16(uint8_t(0x50));
    const uint32_t classRevision = provider->configRead32(uint8_t(8));
    // Device-ID may have been corrected to9a49, but BDF, actual ADL-P CPUID,
    // display class/revision and the working Yoga GMS correction must agree.
    uint32_t a = 1, b = 0, c = 0, d = 0;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    const unsigned family = (a >> 8) & 15;
    const unsigned model = ((a >> 4) & 15) | ((a >> 12) & 0xf0);
    return family == 6 && model == 0x9a && vendor == 0x8086 &&
        (device == 0x46a6 || device == 0x9a49) && ggc == 0xfec1 &&
        command != UINT16_MAX && (command & 2) &&
        (classRevision >> 8) == 0x030000 && uint8_t(classRevision) == 0x0c;
}
bool NativeIokitMapping::nativeIdentity() const {
    if (!held || !controller || !lifetime.exactBuildVerified() ||
        !lifetime.current(controller, route, epoch) || controller->isInactive() ||
        !objectClass(controller, "AppleIntelBaseController", 0x1bf8) ||
        !lifetime.controllerVptr() || field<uintptr_t>(controller, 0) != lifetime.controllerVptr()) return false;
    if (field<IOPCIDevice *>(controller, 0xc60) != provider ||
        field<OSObject *>(controller, 0xc40) != manager ||
        field<IOMemoryMap *>(controller, 0xc70) != mapping ||
        field<uint8_t>(controller, 0x918) || field<uint64_t>(controller, 0x1be0) ||
        field<uint64_t>(controller, 0x1bf0)) return false;
    return manager && objectClass(manager, "AppleIntelRegisterAccessManager", 0x64) &&
        lifetime.managerVptr() && field<uintptr_t>(manager, 0) == lifetime.managerVptr();
}
bool NativeIokitMapping::retainMapping(Route r, uint64_t e) {
    if (!authorized(r,e) || controller || provider || manager || mapping || descriptor || bar ||
        !objectClass(source, "AppleIntelBaseController", 0x1bf8) || source->isInactive() ||
        !lifetime.controllerVptr() || field<uintptr_t>(source,0) != lifetime.controllerVptr()) return false;
    controller = source; controller->retain(); route = r; epoch = e;
    // These are exact captured native fields, read ONLY after class/build/size
    // validation and with the enforced native lifetime boundary held.
    provider = OSDynamicCast(IOPCIDevice, field<OSObject *>(controller,0xc60));
    if (provider) provider->retain();
    manager = field<OSObject *>(controller,0xc40);
    if (manager) manager->retain();
    mapping = OSDynamicCast(IOMemoryMap, field<OSObject *>(controller,0xc70));
    if (mapping) mapping->retain();
    bool valid = provider && manager && mapping && nativeIdentity() && gpuIdentity() &&
        controller->getProvider() == provider;
    if (valid) {
        descriptor = mapping->getMemoryDescriptor();
        if (descriptor) descriptor->retain();
        bar = provider->getDeviceMemoryWithRegister(uint8_t(0x10));
        if (bar) bar->retain();
    }
    NativeMmioSnapshot checked{};
    if (!valid || !snapshot(checked) || !validNativeDssmMapping(checked,e)) {
        releaseMapping();
        return false;
    }
    return true;
}
void NativeIokitMapping::releaseMapping() {
    if (!held) return; // references must never be accessed after lifecycle leave
    if (bar) { bar->release(); bar = nullptr; }
    if (descriptor) { descriptor->release(); descriptor = nullptr; }
    if (mapping) { mapping->release(); mapping = nullptr; }
    if (manager) { manager->release(); manager = nullptr; }
    if (provider) { provider->release(); provider = nullptr; }
    if (controller) { controller->release(); controller = nullptr; }
    epoch = 0; route = {0xff};
}
bool NativeIokitMapping::snapshot(NativeMmioSnapshot &out) const {
    if (!nativeIdentity() || !gpuIdentity() || !descriptor || !bar ||
        controller->getProvider() != provider || mapping->getMemoryDescriptor() != descriptor ||
        provider->getDeviceMemoryWithRegister(uint8_t(0x10)) != bar ||
        descriptor != bar || mapping->getAddressTask() != kernel_task) return false;
    const IOOptionBits options = mapping->getMapOptions();
    // The constructor used options0, not necessarily the final fOptions. XNU
    // xnu-12377.1.9 doMap records resolved physical cache bits in fOptions after
    // successful pager population; getMapOptions returns those final bits.
    // Accept only explicit resolved UC here; unresolved0 still fails closed.
    if ((options & kIOMapCacheMask) != kIOMapInhibitCache || (options & kIOMapReadOnly)) return false;
    IOByteCount mapSegment = 0, barSegment = 0;
    const auto physical = mapping->getPhysicalSegment(0,&mapSegment);
    const auto barPhysical = bar->getPhysicalSegment(0,&barSegment,kIOMemoryMapperNone);
    const uint64_t length = mapping->getLength(), barLength = bar->getLength();
    if (!length || mapSegment < length || barSegment < barLength || descriptor->getLength() != length) return false;
    // Decode PCI64 memory BAR directly and compare to typed IOKit provenance.
    const uint32_t low = provider->configRead32(uint8_t(0x10));
    const uint32_t high = provider->configRead32(uint8_t(0x14));
    if (low == UINT32_MAX || high == UINT32_MAX || (low & 15) != 4) return false;
    const uint64_t configPhysical = (uint64_t(high) << 32) | (low & ~uint32_t(15));
    if (!configPhysical || configPhysical != barPhysical) return false;
    NativeMmioSnapshot s{};
    s.controller = address(controller); s.provider = address(provider);
    s.manager = address(manager); s.mapping = address(mapping); s.descriptor = address(descriptor);
    s.virtualAddress = mapping->getVirtualAddress(); s.physicalAddress = physical;
    s.mappingLength = length; s.barAddress = barPhysical; s.barLength = barLength;
    s.controllerAddress = field<uintptr_t>(controller,0x9b0);
    s.controllerLength = field<uint32_t>(controller,0xc78);
    s.managerAddress = field<uintptr_t>(manager,0x50);
    s.managerLength = field<uint32_t>(manager,0x60);
    s.epoch = epoch;
    s.exactBuild = true; s.yogaIgpu = true; s.started = true; s.stopping = false;
    s.emulated = false; s.kernelTask = true; s.mmioAttributesVerified = true;
    s.descriptorMatchesBar = true;
    if (!validNativeDssmMapping(s,epoch)) return false;
    out = s;
    return true;
}
bool NativeIokitMapping::snapshotRetained(NativeMmioSnapshot &out) { return snapshot(out); }
bool NativeIokitMapping::readRetained32(uint32_t offset, uint32_t &value) {
    // Read-only narrow register surface for initial clock/mapping integration.
    // Power validity is supplied by the outer native coordinator's current().
    if (offset != 0x51004u) return false;
    return readActual32Checked(offset,value);
}
bool NativeIokitMapping::readAux32(uint32_t offset, uint32_t &value) {
    switch (offset) {
        case 0x44470: // TC3 raw HPD DP18/TBT2
        case 0x45444: // AUX2 driver request/state11/10
        case 0x64500: // TC3 PHY ownership6
        case 0x64510: // TC3 AUX_CTL
        case 0x64514: case 0x64518: case 0x6451c: case 0x64520: case 0x64524:
            return readActual32Checked(offset,value);
        default: return false;
    }
}
bool NativeIokitMapping::readActual32Checked(uint32_t offset, uint32_t &value) {
    NativeMmioSnapshot before{}, after{};
    if (!snapshot(before) || before.mappingLength < uint64_t(offset) + 4) return false;
    const auto *base = reinterpret_cast<const volatile void *>(before.virtualAddress);
    OSSynchronizeIO();
    const uint32_t actual = OSReadLittleInt32(base,offset);
    OSSynchronizeIO();
    if (!snapshot(after) || !sameNativeMapping(before,after)) return false;
    value = actual;
    return true;
}
} // namespace YogaTypec
