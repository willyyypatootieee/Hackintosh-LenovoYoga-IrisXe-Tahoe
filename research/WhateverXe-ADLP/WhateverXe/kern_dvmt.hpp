// DVMT calculation repair extracted from WhateverGreen (vit9696 / FireWolf).
#pragma once
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_devinfo.hpp>
class DVMTCalcFix {
    uint32_t dvmt {};
    bool applied {};
public:
    bool available {}, enabled {};
    bool isApplied() const { return applied; }
    uint32_t bytes() const { return dvmt; }
    void processKernel(KernelPatcher &, DeviceInfo *);
    void processFramebufferKext(KernelPatcher &, size_t, mach_vm_address_t, size_t);
};
