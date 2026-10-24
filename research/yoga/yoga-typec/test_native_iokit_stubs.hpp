// Host-only IOKit doubles. Never included by the native kernel build.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
using UInt8 = uint8_t;
using IOByteCount = uint64_t;
using IOOptionBits = uint32_t;
using IODeviceMemory = struct IOMemoryDescriptor;
constexpr uint32_t kIOMapCacheMask=0xf00, kIOMapInhibitCache=0x100, kIOMapReadOnly=0x1000;
constexpr uint32_t kIOMemoryMapperNone=0x800;
static void *kernel_task = reinterpret_cast<void *>(uintptr_t(0x1000));
struct OSMetaClass { unsigned getClassSize() const { return 0x2400; } };
struct OSObject {
    // Allow exact native field offsets without touching fake method state.
    uint8_t nativeStorage[0x2400] = {};
    int refs = 1;
    const char *className = "OSObject";
    OSMetaClass meta;
    virtual ~OSObject() = default;
    const OSObject *metaCast(const char *n) const { return !strcmp(n,className) ? this : nullptr; }
    const OSMetaClass *getMetaClass() const { return &meta; }
    void retain() { ++refs; }
    void release() { --refs; }
};
struct IOService : OSObject {
    IOService *parent = nullptr;
    bool inactive = false;
    bool isInactive() const { return inactive; }
    IOService *getProvider() const { return parent; }
};
struct IOMemoryDescriptor : OSObject {
    uint64_t physical = 0x6000000000, length = 0x200000;
    IOByteCount getLength() const { return length; }
    uint64_t getPhysicalSegment(IOByteCount, IOByteCount *n, IOOptionBits) { *n=length; return physical; }
};
struct IOMemoryMap : OSObject {
    IOMemoryDescriptor *descriptor = nullptr;
    void *task = kernel_task;
    uintptr_t virtualAddress = 0;
    uint32_t options = kIOMapInhibitCache;
    uint64_t length = 0x200000, physical = 0x6000000000;
    IOByteCount getLength() { return length; }
    uintptr_t getVirtualAddress() { return virtualAddress; }
    uint64_t getPhysicalSegment(IOByteCount,IOByteCount *n) { *n=length; return physical; }
    void *getAddressTask() { return task; }
    IOMemoryDescriptor *getMemoryDescriptor() { return descriptor; }
    IOOptionBits getMapOptions() { return options; }
};
struct IOPCIDevice : IOService {
    IODeviceMemory *bar = nullptr;
    uint16_t device = 0x46a6, command = 2;
    uint8_t getBusNumber() { return 0; }
    uint8_t getDeviceNumber() { return 2; }
    uint8_t getFunctionNumber() { return 0; }
    uint16_t configRead16(uint8_t offset) {
        return offset == 0 ? 0x8086 : offset == 2 ? device : offset == 4 ? command : 0xfec1;
    }
    uint32_t configRead32(uint8_t offset) {
        return offset == 8 ? 0x0300000c : offset == 0x10 ? 4 : 0x60;
    }
    IODeviceMemory *getDeviceMemoryWithRegister(uint8_t) { return bar; }
};
#define OSDynamicCast(type, object) dynamic_cast<type *>(object)
inline void OSSynchronizeIO() { __asm__ volatile("" ::: "memory"); }
inline uint32_t OSReadLittleInt32(const volatile void *p, size_t offset) {
    return *reinterpret_cast<const volatile uint32_t *>(static_cast<const volatile uint8_t *>(p)+offset);
}
struct IORecursiveLock { unsigned depth=0, acquisitions=0, releases=0; bool available=true; };
inline bool IORecursiveLockTryLock(IORecursiveLock *lock) {
    if (!lock->available) return false;
    ++lock->depth; ++lock->acquisitions; return true;
}
inline void IORecursiveLockUnlock(IORecursiveLock *lock) { --lock->depth; ++lock->releases; }
static uint64_t testUptimeMicros=0;
static unsigned testSleeps=0;
static IORecursiveLock *testSleepLock=nullptr;
static bool testSleptLocked=false;
inline void clock_get_uptime(uint64_t *value) { *value=testUptimeMicros*1000; }
inline void absolutetime_to_nanoseconds(uint64_t value,uint64_t *nanos) { *nanos=value; }
inline void IOSleep(unsigned milliseconds) {
    if (testSleepLock && testSleepLock->depth) testSleptLocked=true;
    ++testSleeps; testUptimeMicros+=uint64_t(milliseconds)*1000;
}
