// Tiger Lake display register coordinates, not connector availability.
// Derived from Linux v6.12 intel_dkl_phy_regs.h, intel_mg_phy_regs.h,
// i915_reg.h and intel_tc.c. TGL only: do not
// reuse this numbering for ICL, ADL-P, RKL or DG1.
#ifndef kern_tgl_port_map_hpp
#define kern_tgl_port_map_hpp

#include <stdint.h>

struct TGLFiaPort {
 uint32_t instance {}, index {}, live {}, ready {}, owned {}, laneSelect {};
 uint32_t readyMask {}, altMask {}, laneSelectMask {}, laneSelectShift {};
 // Classify values already sampled under the caller's power contract.
 bool readyForLegacy(uint32_t liveValue, uint32_t readyValue) const {
  return readyMask && liveValue != 0xFFFFFFFFU && readyValue != 0xFFFFFFFFU &&
   (readyValue & readyMask) && !(liveValue & altMask);
 }
};

struct TGLTypeCPort {
 uint32_t ddi {}, tcIndex {}, phyBase {}, hipRegister {}, hipShift {};
 uint32_t pllEnable {}, ddiBuffer {};

 static bool resolve(uint32_t physicalDDI, TGLTypeCPort &out) {
  out = {};
  // Apple donor DDI3..8 map to TGL TC1..6. This does not qualify any PHY
  // backend or imply that a connector is wired on a particular machine.
  if (physicalDDI < 3 || physicalDDI > 8) return false;
  const auto tc = physicalDDI - 3;
  out.ddi = physicalDDI; out.tcIndex = tc;
  out.phyBase = 0x168000 + 0x1000 * tc;
  out.hipRegister = tc < 4 ? 0x1010A0 : 0x1010A4;
  out.hipShift = 8 * (tc % 4);
  out.pllEnable = 0x46030 + 4 * tc;
  out.ddiBuffer = 0x64000 + 0x100 * physicalDDI;
  return true;
 }

 bool legacyHPD(uint32_t sdeisr) const {
  return ddi >= 3 && ddi <= 8 && tcIndex == ddi - 3 &&
   sdeisr != 0xFFFFFFFFU && (sdeisr & (1U << (24 + tcIndex)));
 }

 uint32_t phyRegister(uint32_t internal) const {
  return phyBase + (internal & 0xFFF);
 }
 uint32_t selectBank(uint32_t previous, uint32_t internal) const {
  return (previous & ~(0xFU << hipShift)) |
   (((internal >> 12) & 0xF) << hipShift);
 }

 // The caller must sample FIA1 under its own TC-cold/power contract. Never
 // discover FIA topology by poking unpowered registers in this resolver.
 bool resolveFia(uint32_t fia1Live, TGLFiaPort &out) const {
  out = {};
  if (ddi < 3 || ddi > 8 || tcIndex != ddi - 3 || fia1Live == 0xFFFFFFFFU)
   return false;
  const bool modular = fia1Live & (1U << 4);
  const auto instance = modular ? tcIndex / 2 : 0;
  const auto index = modular ? tcIndex % 2 : tcIndex;
  // A non-modular 32-bit register has only four eight-bit port fields.
  if (index >= 4) return false;
  const uint32_t bases[] {0x163000, 0x16E000, 0x16F000};
  const auto base = bases[instance];
  out.instance = instance; out.index = index;
  out.live = base + 0x8A0; out.ready = base + 0x890;
  out.owned = base + 0x894; out.laneSelect = base + 0x8C0;
  out.readyMask = 1U << index;
  out.altMask = 0x60U << (8 * index);
  out.laneSelectShift = 4 * index; out.laneSelectMask = 0xFU << out.laneSelectShift;
  return true;
 }
};

#endif
