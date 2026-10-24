// PCI families verified against Linux v6.12 i915_pciids.h.
// Display-name suggestions and limitations: see docs/device-selection.md.
// No EU, board wiring or transport assumptions belong in this table.
#ifndef KERN_XELP_DEVICES_HPP
#define KERN_XELP_DEVICES_HPP
#include <stdint.h>
#include <stddef.h>
namespace XELP {
enum class Family : uint32_t { Unknown, TigerLake, AlderLakeN, RocketLake, AlderLakeS, AlderLakeP, RaptorLakeS, RaptorLakeP };
enum class Display : uint32_t { Unknown, XeD = 12, XeLPD = 13 };
enum class GT : uint32_t { Unknown, TGLGT2, TGLG4, ADLNSmall, ADLPGT2 };
enum class Donor : uint32_t { Unknown, C004FC1A = 1 };
struct DeviceEntry {
    uint16_t device; Family family; Display display; GT backend;
    const char *model;
};
// GT::Unknown means catalogued, without an implemented automatic backend.
constexpr DeviceEntry DeviceTable[] {
    {0x4680, Family::AlderLakeS, Display::XeD, GT::Unknown, "Intel UHD Graphics 770"},
    {0x4682, Family::AlderLakeS, Display::XeD, GT::Unknown, "Intel UHD Graphics 730"},
    {0x4688, Family::AlderLakeS, Display::XeD, GT::Unknown, "Intel UHD Graphics"},
    {0x468B, Family::AlderLakeS, Display::XeD, GT::Unknown, "Intel UHD Graphics"},
    {0x4690, Family::AlderLakeS, Display::XeD, GT::Unknown, "Intel UHD Graphics 770"},
    {0x4692, Family::AlderLakeS, Display::XeD, GT::Unknown, "Intel UHD Graphics 730"},
    {0x4693, Family::AlderLakeS, Display::XeD, GT::Unknown, "Intel UHD Graphics 710"},
    {0x46A3, Family::AlderLakeP, Display::XeLPD, GT::Unknown, "Intel UHD Graphics"},
    // ADL-P GT2 is qualified separately from ADL-N: it uses the Display 13
    // backend but has the full six-DSS Gen12 topology and its own revision
    // guard. Do not use this entry as a TGL donor-ID substitute.
    {0x46A6, Family::AlderLakeP, Display::XeLPD, GT::ADLPGT2, "Intel Iris Xe Graphics"},
    {0x46A8, Family::AlderLakeP, Display::XeLPD, GT::Unknown, "Intel Iris Xe Graphics"},
    {0x46AA, Family::AlderLakeP, Display::XeLPD, GT::Unknown, "Intel Iris Xe Graphics"},
    {0x46B3, Family::AlderLakeP, Display::XeLPD, GT::Unknown, "Intel UHD Graphics"},
    {0x46C3, Family::AlderLakeP, Display::XeLPD, GT::Unknown, "Intel UHD Graphics"},
    {0x46D0, Family::AlderLakeN, Display::XeLPD, GT::ADLNSmall, "Intel UHD Graphics"},
    {0x46D1, Family::AlderLakeN, Display::XeLPD, GT::ADLNSmall, "Intel UHD Graphics"},
    {0x46D2, Family::AlderLakeN, Display::XeLPD, GT::ADLNSmall, "Intel UHD Graphics"},
    {0x46D3, Family::AlderLakeN, Display::XeLPD, GT::ADLNSmall, "Intel Graphics"},
    {0x46D4, Family::AlderLakeN, Display::XeLPD, GT::ADLNSmall, "Intel Graphics"},
    {0x4C8A, Family::RocketLake, Display::XeD, GT::Unknown, "Intel UHD Graphics 750"},
    {0x4C8B, Family::RocketLake, Display::XeD, GT::Unknown, "Intel UHD Graphics 730"},
    {0x9A40, Family::TigerLake, Display::XeD, GT::TGLGT2, "Intel Iris Xe Graphics"},
    {0x9A49, Family::TigerLake, Display::XeD, GT::TGLGT2, "Intel Iris Xe Graphics"},
    {0x9A60, Family::TigerLake, Display::XeD, GT::Unknown, "Intel UHD Graphics"},
    {0x9A78, Family::TigerLake, Display::XeD, GT::TGLG4, "Intel UHD Graphics"},
    {0xA780, Family::RaptorLakeS, Display::XeD, GT::Unknown, "Intel UHD Graphics 770"},
    {0xA782, Family::RaptorLakeS, Display::XeD, GT::Unknown, "Intel UHD Graphics 730"},
    {0xA788, Family::RaptorLakeS, Display::XeD, GT::Unknown, "Intel UHD Graphics"},
    {0xA78B, Family::RaptorLakeS, Display::XeD, GT::Unknown, "Intel UHD Graphics"},
    {0xA7A0, Family::RaptorLakeP, Display::XeLPD, GT::Unknown, "Intel Iris Xe Graphics"},
    {0xA7A1, Family::RaptorLakeP, Display::XeLPD, GT::Unknown, "Intel Iris Xe Graphics"},
    {0xA7A8, Family::RaptorLakeP, Display::XeLPD, GT::Unknown, "Intel UHD Graphics"},
    {0xA7A9, Family::RaptorLakeP, Display::XeLPD, GT::Unknown, "Intel UHD Graphics"},
    {0xA7AA, Family::RaptorLakeP, Display::XeLPD, GT::Unknown, "Intel Graphics"},
    {0xA7AB, Family::RaptorLakeP, Display::XeLPD, GT::Unknown, "Intel Graphics"},
    {0xA7AC, Family::RaptorLakeP, Display::XeLPD, GT::Unknown, "Intel Graphics"},
    {0xA7AD, Family::RaptorLakeP, Display::XeLPD, GT::Unknown, "Intel Graphics"},
};
inline const DeviceEntry *deviceEntry(uint32_t pci) {
    if((pci&0xFFFF)!=0x8086)return nullptr;
    for(const auto &entry:DeviceTable)if(entry.device==(pci>>16))return &entry;
    return nullptr;
}
}
#endif
