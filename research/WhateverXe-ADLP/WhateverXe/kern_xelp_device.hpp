// Shared IOKit adapter. Call only after IOPCIDevice has been published.
#ifndef KERN_XELP_DEVICE_HPP
#define KERN_XELP_DEVICE_HPP
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/c++/OSData.h>
#include "kern_xelp_platform.hpp"
namespace XELP {
inline Profile deviceProfile(IOPCIDevice *pci) {
    if(!pci)return {};
    const auto property=pci->getProperty("device-id");
    const auto data=OSDynamicCast(OSData,property);
    return select(pci->configRead32(0),pci->configRead8(8),property!=nullptr,
        data ? data->getBytesNoCopy() : nullptr,data ? data->getLength() : 0);
}
}
#endif
