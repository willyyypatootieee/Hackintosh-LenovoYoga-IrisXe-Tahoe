#include "kern_xe.hpp"
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>

static void publishBytes(IOPCIDevice *pci,const char *key,const void *bytes,unsigned length) {
    auto data=OSData::withBytes(bytes,length);
    if(data){pci->setProperty(key,data);data->release();}
}

// Firmware is read once, before topology/hook selection. No PCI BAR/ROM scan,
// power acquisition, or MMIO probing occurs here. Explicit properties win later.
void XE::xelpReadFirmware(IOPCIDevice *pci) {
    if(!pci || !xeProfile.admitted())return;
    const uint32_t asls=pci->configRead32(0xFC);
    uint32_t source=0;
    if(asls && asls<=0xFFFFE000U) {
        auto memory=IOMemoryDescriptor::withPhysicalAddress(asls,8192,kIODirectionIn);
        auto mapping=memory ? memory->map(kIOMapReadOnly) : nullptr;
        if(mapping && mapping->getVirtualAddress() && mapping->getLength()>=8192) {
            const auto bytes=reinterpret_cast<const uint8_t *>(mapping->getVirtualAddress());
            const auto region=XELP::VBT::region(bytes,8192,asls);
            if(region.valid) {
                // Preserve existing diagnostics for collectors.
                pci->setProperty("TGLASLS",asls,32);
                publishBytes(pci,"TGLOpRegion",bytes,8192);
                if(region.address) {
                    auto vbtMemory=IOMemoryDescriptor::withPhysicalAddress(region.address,region.bytes,kIODirectionIn);
                    auto vbtMap=vbtMemory ? vbtMemory->map(kIOMapReadOnly) : nullptr;
                    if(vbtMap && vbtMap->getVirtualAddress() && vbtMap->getLength()>=region.bytes) {
                        const auto data=reinterpret_cast<const void *>(vbtMap->getVirtualAddress());
                        firmwarePorts=XELP::VBT::parse(data,region.bytes,xeProfile.family);
                        if(firmwarePorts.valid()){source=2;publishBytes(pci,"TGLVBT",data,firmwarePorts.bytes);}
                    }
                    if(vbtMap)vbtMap->release();
                    if(vbtMemory)vbtMemory->release();
                }
                if(!firmwarePorts.valid()) {
                    const auto embedded=XELP::VBT::parse(bytes+0x400,region.inlineBytes,xeProfile.family);
                    if(embedded.valid()) {
                        firmwarePorts=embedded;source=1;
                        publishBytes(pci,"TGLVBT",bytes+0x400,firmwarePorts.bytes);
                    } else if(firmwarePorts.status==XELP::VBT::Status::Missing)firmwarePorts=embedded;
                }
            }
        }
        if(mapping)mapping->release();
        if(memory)memory->release();
    }
    const auto hdmi=firmwarePorts.hdmi(3); // physical Combo A/B on both admitted families
    uint32_t summary[]={1,static_cast<uint32_t>(firmwarePorts.status),source,firmwarePorts.version,
        firmwarePorts.present,firmwarePorts.unmapped,firmwarePorts.ambiguous,hdmi.valid,hdmi.hdmi,
        hdmi.ddc[0],hdmi.ddc[1],hdmi.ddc[2]};
    pci->setProperty("WhateverXeFirmwarePorts",summary,sizeof(summary));
    pci->setProperty("WhateverXeFirmwarePortRecords",firmwarePorts.port,sizeof(firmwarePorts.port));
}
