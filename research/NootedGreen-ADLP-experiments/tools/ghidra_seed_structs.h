/*
 * ghidra_seed_structs.h
 *
 * Seed file for Ghidra "File -> Parse C Source..." import. Loading this header
 * into Ghidra will auto-create the AppleIntelTGLGraphicsFramebuffer parameter
 * structs in the Data Type Manager with the named offsets we already know.
 *
 * Workflow:
 *   1. In Ghidra: File -> Parse C Source...
 *   2. Source files: add this file
 *   3. Parse Options: defaults are fine (no -D / -I needed)
 *   4. Click "Parse to Program" (binds to current program's DTM).
 *   5. Open Data Type Manager -> the structs appear ready to use.
 *   6. As you reverse more fields, edit the existing DTM entries in Ghidra
 *      (the *.gdt is the source of truth). Re-run tools/extract_apple_params.py
 *      to regenerate NootedGreen/AppleIntelParams.hpp.
 *
 * Notes:
 *  - Ghidra accepts plain C; no C++ keywords. No namespace, no static_assert.
 *  - Field offsets are produced by listing fields in order with explicit
 *    padding arrays — total struct sizes match what disasm shows.
 *  - Padding gaps named `_pad_NNNN` will round-trip through the extractor
 *    script as `_pad_NNNN` (so newly named fields inside a gap can be added
 *    incrementally without disturbing other offsets).
 */

#ifndef GHIDRA_SEED_STRUCTS_H
#define GHIDRA_SEED_STRUCTS_H

/*
 * NO #include <stdint.h> — Ghidra's C parser tries to resolve <stdint.h>
 * against macOS system headers and dies in cdefs.h on `__attribute__((...))`.
 *
 * Ghidra's C parser does NOT inherit DataTypeManager BuiltInTypes (uint32_t
 * etc.) — it needs explicit typedefs in the source. Provide them here.
 */
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

/* ---------------------------------------------------------------------------
 * CRTCParams (0xF0 bytes) — built by AppleIntelBaseController::SetupParams
 * Consumed by 299 sites: hwSetMode, hwSetupMemory, paramsSurfCompare,
 * setupPipeScaler, setupPipeWatermarks, setupDSCEngineParams, SetupTimings,
 * SetupDPSSTTimings, SetupMSTDPTimings, SetupDPTimings, hwRegsNeedUpdate,
 * hwCRTCToIODetailedTimingInformation, hwGetCRTC, etc.
 * ------------------------------------------------------------------------- */
typedef struct CRTCParams {
    uint32_t TRANS_CLK_SEL;        /* +0x00 */
    uint32_t TRANS_DDI_FUNC_CTL;   /* +0x04  (V97P clears bit16 here) */
    uint32_t TRANS_DDI_FUNC_CTL2;  /* +0x08 */
    uint32_t TRANS_MSA_MISC;       /* +0x0C */
    uint32_t TRANS_HTOTAL;         /* +0x10 */
    uint32_t TRANS_HBLANK;         /* +0x14 */
    uint32_t TRANS_HSYNC;          /* +0x18 */
    uint32_t TRANS_VTOTAL;         /* +0x1C */
    uint32_t TRANS_VBLANK;         /* +0x20 */
    uint32_t TRANS_VSYNC;          /* +0x24 */
    uint32_t PIPE_SRCSZ;           /* +0x28 */
    uint32_t TRANS_CONF;           /* +0x2C  (V97C aligns to live HW) */
    uint8_t  _pad_0030[0x18];      /* +0x30..+0x47  TBD (setupPipeScaler reads [+0x30] as ecx) */
    uint32_t PS_PS_WIN_POS;        /* +0x48  (setupPipeScaler: pipe scaler window position) */
    uint32_t PS_PS_WIN_SZ;         /* +0x4C  (setupPipeScaler: pipe scaler window size) */
    uint32_t PIPE_SEAM_EXCESS;     /* +0x50  (setupPipeScaler: dual-pipe seam join offset) */
    uint32_t PS_HPHASE;            /* +0x54  (setupPipeScaler: horizontal phase) */
    uint8_t  _pad_0058[0x30];      /* +0x58..+0x87  TBD: setupPipeWatermarks / SetupDPSSTTimings fields */
    uint32_t PPS_0;                /* +0x88 */
    uint32_t PPS_1;                /* +0x8C */
    uint32_t PPS_2;                /* +0x90 */
    uint32_t PPS_3;                /* +0x94 */
    uint32_t PPS_4;                /* +0x98 */
    uint32_t PPS_5;                /* +0x9C */
    uint32_t PPS_6;                /* +0xA0 */
    uint32_t PPS_7;                /* +0xA4 */
    uint32_t PPS_8;                /* +0xA8 */
    uint32_t PPS_9;                /* +0xAC */
    uint32_t PPS_10;               /* +0xB0 */
    uint32_t PPS_16;               /* +0xB4 */
    uint8_t  _pad_00B8[0x30];      /* +0xB8..+0xE7  TBD: identify via setupDSCEngineParams (DSC slice/bpp/line buffer) */
    uint32_t DSC_ENGINE_SEL;       /* +0xE8 */
    uint32_t DSC_JOINER_CTL;       /* +0xEC */
} CRTCParams;

/* ---------------------------------------------------------------------------
 * PLANEPARAMS — verified from paramsSurfCompare disasm. Apple's internal
 * representation, NOT the i915 PLANE_CTL register layout. paramsSurfCompare
 * directly reads:
 *   [+0x00] PLANE_CTL    (tiling field at bits[27:23], mask 0xF800000)
 *   [+0x18] PLANE_STRIDE
 *   [+0x20] PLANE_SURF
 * Other offsets between these are unknown — leave padded until further
 * disasm reveals them (likely candidates: PLANE_POS, PLANE_SIZE, PLANE_OFFSET,
 * PLANE_COLOR_CTL written by setupPlane, but offsets unverified).
 * ------------------------------------------------------------------------- */
typedef struct PLANEPARAMS {
    uint32_t PLANE_CTL;            /* +0x00  (tiling bits[27:23] = 0xF800000) */
    uint8_t  _pad_0004[0x14];      /* +0x04..+0x17  TBD */
    uint32_t PLANE_STRIDE;         /* +0x18 */
    uint8_t  _pad_001C[0x4];       /* +0x1C..+0x1F  TBD */
    uint32_t PLANE_SURF;           /* +0x20 */
    uint8_t  _pad_0024[0x8];       /* +0x24..+0x2B  TBD (struct size assumed 0x2C; may extend) */
} PLANEPARAMS;

/* ---------------------------------------------------------------------------
 * SCALERPARAMS — used by setupPipeScaler and AppleIntelScaler::syncScalerUpdate.
 * Friend's Ghidra dump showed 3 fields; structure may extend further.
 * ------------------------------------------------------------------------- */
typedef struct SCALERPARAMS {
    uint32_t PS_CTRL;              /* +0x00 */
    uint32_t PS_WIN_POS;           /* +0x04 */
    uint32_t PS_WIN_SZ;            /* +0x08 */
} SCALERPARAMS;

/* ---------------------------------------------------------------------------
 * DSC param staging structs (consumed by computePps in setupDSCEngineParams).
 * Inferred from disasm: r15 = ppsOpt_t* lives in stack, written via [r15+8],
 *                       [r15+0x24], [r15+0x28], [r15+0x3C], etc.
 *                       r13 = ppsConfig_t* read via [r13+0x40]..[r13+0x94].
 * Sizes from memset calls inside setupDSCEngineParams:
 *   memset(ppsOpt_t,    0, 0x50)
 *   memset(ppsConfig_t, 0, 0x18C)
 * ------------------------------------------------------------------------- */
typedef struct ppsOpt_t {
    uint8_t  _opaque[0x50];        /* total 0x50 bytes — fill in fields as identified */
} ppsOpt_t;

typedef struct ppsConfig_t {
    uint8_t  _opaque[0x18C];       /* total 0x18C bytes — fill in fields as identified */
} ppsConfig_t;

/* ---------------------------------------------------------------------------
 * LinkConfig — per-port DP link config (lane count, link rate, vswing,
 * pre-emph). Used by setupOptimalLaneCount / computeLaneCount / setPortMode.
 * Size and field layout TBD — reverse from AppleIntelPort cache offsets.
 * ------------------------------------------------------------------------- */
typedef struct LinkConfig {
    uint8_t  _opaque[0x80];        /* placeholder; reverse-engineer actual size */
} LinkConfig;

#endif /* GHIDRA_SEED_STRUCTS_H */
