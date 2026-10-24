# -*- coding: utf-8 -*-
# Extract AppleIntel framebuffer parameter structs to a C++ header
# @author NootedGreen
# @category AppleIntel
# @keybinding
# @menupath Tools.Apple Intel.Extract Params Header
# @toolbar
# @runtime Jython
#
# What it does:
#   Scans the currently-open Ghidra program (an AppleIntelTGLGraphicsFramebuffer
#   kext disassembly) for ALL structs, unions, and enums in the DataTypeManager,
#   function signatures, local variables, and defined data in memory.  For each
#   type with real content it emits a C++ definition with static_assert offset
#   guards.  For whitelist structs that are still empty it emits a stub driven
#   by EXPECTED_OFFSETS so the build at least compiles with placeholders.
#
# How to use:
#   1. Open the kext in Ghidra and let auto-analysis finish.
#   2. (Optional) In the Data Type Manager, define fields in the interesting
#      structs.  Unnamed slots become _pad_NNNN arrays in the output.
#   3. Drop this file into ~/ghidra_scripts/.
#   4. Run it from Tools -> Apple Intel -> Extract Params Header.
#
# Also runnable from pyghidra (Python 3 outside Ghidra):
#   pyghidra-run --project-name <name> --project-path <path> extract_apple_params.py

from ghidra.program.model.data import (
    Structure, Union, Enum, Pointer, Array,
    TypeDef, AbstractIntegerDataType,
    StructureDataType, CategoryPath, DataTypeConflictHandler,
)
from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.program.model.pcode import PcodeOp
import os, datetime

# ----------------------------------------------------------------------------
# Configuration
# ----------------------------------------------------------------------------

STRUCT_WHITELIST = [
    "CRTCParams",
    "PLANEPARAMS",
    "SCALERPARAMS",
    "ppsConfig_t",
    "ppsOpt_t",
    "LinkConfig",
    "AppleIntelScaler",
    "AppleIntelPlane",
    "AppleIntelBaseController",
    "AppleIntelFramebuffer",
    "AppleIntelDisplayPath",
    "AppleIntelPowerWell",
    "FlipTransactionArgs",
    "AppleIntelMMIO",
    "AppleIntelPlaneRegCache",
    "AppleIntelScalerRegCache",
    # Accelerator / 3D path structs
    "IGHardwareExtendedContextParams",
    "blit3d_params_t",
    "IGAccelTask",
    "IGHardwareBlit3DContext",
    "IGAccelSysMemory",
    "IGAccelSegmentResourceList",
    "IntelAccelerator",
    # Note: IOService and IOGraphicsAccelerator2 are kernel/IOKit base classes —
    # not defined in the kext binary; Ghidra produces only 1-byte stubs for them.
]

# ---------------------------------------------------------------------------
# Function signature validation
# ---------------------------------------------------------------------------
# Each entry: (demangled_fragment, expected_mangled_symbol, hook_label)
# The script finds each function by demangled fragment, reads its actual
# mangled name from the symbol table, and compares with expected.
# Run discover_function_signatures() in main() for a validation report.
SIG_TARGETS = [
    ("IntelAccelerator::populateResetRegisterList",
     "__ZN16IntelAccelerator25populateResetRegisterListEv",
     "populateResetRegisterList"),
    ("IntelAccelerator::createUserGPUTask",
     "__ZN16IntelAccelerator17createUserGPUTaskEv",
     "createUserGPUTask"),
    ("IGHardwareExtendedContext::initWithOptions",
     "__ZN25IGHardwareExtendedContext15initWithOptionsEP11IGAccelTaskRK31IGHardwareExtendedContextParams",
     "IGHardwareExtendedContext::initWithOptions"),
    ("IGAccelTask::getBlit3DContext",
     "__ZN11IGAccelTask16getBlit3DContextEb",
     "getBlit3DContext"),
    ("blit3d_init_ctx",
     "__Z15blit3d_init_ctxP23IGHardwareBlit3DContext",
     "blit3d_init_ctx"),
    ("blit3d_initialize_scratch_space",
     "__Z31blit3d_initialize_scratch_spaceP16IGAccelSysMemory",
     "blit3d_initialize_scratch_space"),
    ("IntelAccelerator::startGraphicsEngine",
     "__ZN16IntelAccelerator19startGraphicsEngineEv",
     "startGraphicsEngine"),
    ("IGAccelTask::withOptions",
     "__ZN11IGAccelTask11withOptionsEP16IntelAccelerator",
     "IGAccelTask::withOptions"),
    ("IGHardwareBlit3DContext::initialize",
     "__ZN23IGHardwareBlit3DContext10initializeEv",
     "IGHardwareBlit3DContext::initialize"),
    ("IGAccelSegmentResourceList::initBlitUsage",
     "__ZN26IGAccelSegmentResourceList13initBlitUsageEv",
     "initBlitUsage"),
    ("IntelAccelerator::submitBlit",
     "__ZN16IntelAccelerator10submitBlitEP15blit3d_params_tRK8IGVectorI11rect_pair_t25IGIOMallocAllocatorPolicyEP11IGAccelTaskb",
     "submitBlit"),
    ("IntelAccelerator::start",
     "__ZN16IntelAccelerator5startEP9IOService",
     "IntelAccelerator::start"),
]

# PlaneParams / ScalerParams were old alias names — removed; use AppleIntelPlane / AppleIntelScaler.

# ---------------------------------------------------------------------------
# Init-function struct discovery
# ---------------------------------------------------------------------------

# Ordered discovery targets.
# Each entry is `(function_name_fragment, struct_name, param_index)`.
# `param_index=0` means the C++ `this` pointer for instance methods.
INIT_FUNC_TARGETS = [
    # ========== AppleIntelScaler (`this`) ==========
    ("AppleIntelScaler::init", "AppleIntelScaler", 0),
    ("AppleIntelScaler::~AppleIntelScaler", "AppleIntelScaler", 0),
    ("AppleIntelScaler::syncScalerUpdate", "AppleIntelScaler", 0),
    ("AppleIntelScaler::updateRegisterCache", "AppleIntelScaler", 0),
    ("AppleIntelScaler::getScalerType", "AppleIntelScaler", 0),
    ("AppleIntelScaler::getScalerIndex", "AppleIntelScaler", 0),
    ("AppleIntelScaler::getController", "AppleIntelScaler", 0),
    ("AppleIntelScaler::getPath", "AppleIntelScaler", 0),
    ("AppleIntelScaler::isEnabled", "AppleIntelScaler", 0),
    ("AppleIntelScaler::setupPipeScaler", "AppleIntelScaler", 0),
    ("AppleIntelScaler::programPipeScaler", "AppleIntelScaler", 0),
    ("AppleIntelScaler::getPipeIndex", "AppleIntelScaler", 0),

    # ========== AppleIntelPlane (`this`) ==========
    ("AppleIntelPlane::init", "AppleIntelPlane", 0),
    ("AppleIntelPlane::~AppleIntelPlane", "AppleIntelPlane", 0),
    ("AppleIntelPlane::syncPlaneUpdate", "AppleIntelPlane", 0),
    ("AppleIntelPlane::updateRegisterCache", "AppleIntelPlane", 0),
    ("AppleIntelPlane::getPlaneIndex", "AppleIntelPlane", 0),
    ("AppleIntelPlane::getController", "AppleIntelPlane", 0),
    ("AppleIntelPlane::getPath", "AppleIntelPlane", 0),
    ("AppleIntelPlane::isEnabled", "AppleIntelPlane", 0),
    ("AppleIntelPlane::getPipeIndex", "AppleIntelPlane", 0),

    # ========== AppleIntelBaseController (`this`) ==========
    ("AppleIntelBaseController::init", "AppleIntelBaseController", 0),
    ("AppleIntelBaseController::~AppleIntelBaseController", "AppleIntelBaseController", 0),
    ("AppleIntelBaseController::setupParams", "AppleIntelBaseController", 0),
    ("AppleIntelBaseController::SetupParams", "AppleIntelBaseController", 0),
    ("AppleIntelBaseController::setupPipeWatermarks", "AppleIntelBaseController", 0),
    ("AppleIntelBaseController::setupDSCEngineParams", "AppleIntelBaseController", 0),
    ("AppleIntelBaseController::paramsSurfCompare", "AppleIntelBaseController", 0),
    ("AppleIntelBaseController::readReg32", "AppleIntelBaseController", 0),
    ("AppleIntelBaseController::writeReg32", "AppleIntelBaseController", 0),
    ("AppleIntelBaseController::readRegPtr", "AppleIntelBaseController", 0),
    ("AppleIntelBaseController::writeRegPtr", "AppleIntelBaseController", 0),
    ("AppleIntelBaseController::getMMIOBase", "AppleIntelBaseController", 0),
    ("AppleIntelBaseController::setMMIOBase", "AppleIntelBaseController", 0),

    # ========== AppleIntelFramebuffer (`this`) ==========
    ("AppleIntelFramebuffer::init", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::~AppleIntelFramebuffer", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::prepareToEnterWake", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::prepareToExitWake", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::prepareToEnterSleep", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::prepareToExitSleep", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::setPanelPowerState", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::getPanelPowerState", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::setBacklightLevel", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::getBacklightLevel", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::setDisplayMode", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::getDisplayMode", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::getOnlineInfo", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::getController", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::getPath", "AppleIntelFramebuffer", 0),
    ("AppleIntelFramebuffer::getPipeIndex", "AppleIntelFramebuffer", 0),

    # ========== AppleIntelDisplayPath (`this`) ==========
    ("AppleIntelDisplayPath::init", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::~AppleIntelDisplayPath", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::getConnectorID", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::setConnectorID", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::getConnectorType", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::getDisplayPort", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::getAuxChannel", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::isConnected", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::setConnected", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::updateConnectStatus", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::configurePath", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::setupPath", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::resetPath", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::getFreeJoinablePathCount", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::joinPath", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::unjoinPath", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::getEDID", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::getCapabilities", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::trainLink", "AppleIntelDisplayPath", 0),
    ("AppleIntelDisplayPath::untrain", "AppleIntelDisplayPath", 0),

    # ========== AppleIntelPowerWell (`this`) ==========
    ("AppleIntelPowerWell::init", "AppleIntelPowerWell", 0),
    ("AppleIntelPowerWell::~AppleIntelPowerWell", "AppleIntelPowerWell", 0),
    ("AppleIntelPowerWell::enable", "AppleIntelPowerWell", 0),
    ("AppleIntelPowerWell::disable", "AppleIntelPowerWell", 0),
    ("AppleIntelPowerWell::isEnabled", "AppleIntelPowerWell", 0),
    ("AppleIntelPowerWell::isSupported", "AppleIntelPowerWell", 0),
    ("AppleIntelPowerWell::getController", "AppleIntelPowerWell", 0),
    ("AppleIntelPowerWell::getIndex", "AppleIntelPowerWell", 0),
    ("AppleIntelPowerWell::setPowerWellState", "AppleIntelPowerWell", 0),
    ("AppleIntelPowerWell::getPowerWellState", "AppleIntelPowerWell", 0),

    # ========== Non-`this` parameter structs ==========
    ("AppleIntelPlane::configurePlane", "FlipTransactionArgs", 1),
    ("AppleIntelPlane::configureColorPipeLine", "FlipTransactionArgs", 1),
    ("AppleIntelPlane::preparePlaneFlip", "FlipTransactionArgs", 1),
    ("AppleIntelPlane::commitPlaneFlip", "FlipTransactionArgs", 1),
    ("AppleIntelPlane::configurePipePostCSCGamma8Bit", "FlipTransactionArgs", 1),
    ("AppleIntelPlane::configurePipePostCSCGamma10Bit", "FlipTransactionArgs", 1),
    ("AppleIntelPlane::configurePipePostCSCGamma12Bit", "FlipTransactionArgs", 1),
    ("AppleIntelPlane::configurePipePostCSCGamma12SEGBit", "FlipTransactionArgs", 1),

    ("AppleIntelScaler::setupPipeScaler", "AppleIntelDisplayPath", 1),
    ("AppleIntelScaler::programPipeScaler", "AppleIntelDisplayPath", 1),
    ("AppleIntelBaseController::setupParams", "AppleIntelDisplayPath", 2),
    ("AppleIntelBaseController::SetupParams", "AppleIntelDisplayPath", 2),
    ("AppleIntelBaseController::setupPipeWatermarks", "AppleIntelDisplayPath", 2),
    ("AppleIntelBaseController::setupDSCEngineParams", "AppleIntelDisplayPath", 3),
    ("AppleIntelFramebuffer::getOnlineInfo", "AppleIntelDisplayPath", 1),

    ("AppleIntelScaler::setupPipeScaler", "CRTCParams", 2),
    ("AppleIntelBaseController::setupParams", "CRTCParams", 3),
    ("AppleIntelBaseController::SetupParams", "CRTCParams", 3),
    ("AppleIntelBaseController::setupPipeWatermarks", "CRTCParams", 3),
    ("AppleIntelBaseController::setupDSCEngineParams", "CRTCParams", 2),
    ("AppleIntelBaseController::paramsSurfCompare", "CRTCParams", 1),
    ("AppleIntelBaseController::paramsSurfCompare", "CRTCParams", 2),
    ("AppleIntelBaseController::paramsSurfCompare", "PLANEPARAMS", 3),
    ("AppleIntelBaseController::paramsSurfCompare", "PLANEPARAMS", 4),

    # ========== Accelerator / 3D-path structs ==========
    # IGHardwareExtendedContextParams (const& param_2 of initWithOptions)
    ("IGHardwareExtendedContext::initWithOptions", "IGHardwareExtendedContextParams", 2),
    # IGAccelTask (this of getBlit3DContext / withOptions)
    ("IGAccelTask::getBlit3DContext", "IGAccelTask", 0),
    ("IGAccelTask::withOptions", "IGAccelTask", 0),
    # IGHardwareBlit3DContext (this of initialize; param_1 of blit3d_init_ctx)
    ("IGHardwareBlit3DContext::initialize", "IGHardwareBlit3DContext", 0),
    ("blit3d_init_ctx", "IGHardwareBlit3DContext", 0),
    # IGAccelSysMemory (param_1 of blit3d_initialize_scratch_space)
    ("blit3d_initialize_scratch_space", "IGAccelSysMemory", 0),
    # blit3d_params_t (param_1 of submitBlit)
    ("IntelAccelerator::submitBlit", "blit3d_params_t", 1),
    # IGAccelSegmentResourceList (this of initBlitUsage)
    ("IGAccelSegmentResourceList::initBlitUsage", "IGAccelSegmentResourceList", 0),
    # IntelAccelerator (param_1 of IGAccelTask::withOptions; this of start/submitBlit/createUserGPUTask)
    # Also probe getGPUInfo/getGPUInfoICL to discover topology field offsets via PCode.
    ("IGAccelTask::withOptions", "IntelAccelerator", 1),
    ("IntelAccelerator::start", "IntelAccelerator", 0),
    ("IntelAccelerator::createUserGPUTask", "IntelAccelerator", 0),
    ("IntelAccelerator::submitBlit", "IntelAccelerator", 0),
    ("IntelAccelerator::getGPUInfo", "IntelAccelerator", 0),
    ("IntelAccelerator::getGPUInfoICL", "IntelAccelerator", 0),
    # IGAccelSysMemory — confirmed function names from Ghidra symbol table.
    # ::init(IOGraphicsAccelerator2*) stores the accelerator ref and initialises fields.
    # ::wire / ::unwire map/unmap the backing IOBufferMemoryDescriptor → heavy this-access.
    # ::getPhysicalSegment reads base address and size fields.
    ("IGAccelSysMemory::init", "IGAccelSysMemory", 0),
    ("IGAccelSysMemory::wire", "IGAccelSysMemory", 0),
    ("IGAccelSysMemory::unwire", "IGAccelSysMemory", 0),
    ("IGAccelSysMemory::getPhysicalSegment", "IGAccelSysMemory", 0),
    # IGAccelSegmentResourceList — confirmed function names from Ghidra symbol table.
    # initWithSharedResourceList is the real init (two overloads); free/prepare/complete
    # are the operational methods most likely to touch all this-pointer fields.
    ("IGAccelSegmentResourceList::initWithSharedResourceList", "IGAccelSegmentResourceList", 0),
    ("IGAccelSegmentResourceList::free", "IGAccelSegmentResourceList", 0),
    ("IGAccelSegmentResourceList::prepare", "IGAccelSegmentResourceList", 0),
    ("IGAccelSegmentResourceList::complete", "IGAccelSegmentResourceList", 0),
    ("IGAccelSegmentResourceList::markBlitUsage", "IGAccelSegmentResourceList", 0),
    ("IGAccelSegmentResourceList::removeFromChannel", "IGAccelSegmentResourceList", 0),
]

# Nested-pointer discovery targets.
# Each entry is:
#   (function_name_fragment, parent_param_index, pointer_field_offset, child_struct_name)
NESTED_POINTER_TARGETS = [
    ("AppleIntelBaseController::readReg32", 0, 0x78, "AppleIntelMMIO"),
    ("AppleIntelBaseController::writeReg32", 0, 0x78, "AppleIntelMMIO"),
    ("AppleIntelBaseController::readRegPtr", 0, 0x78, "AppleIntelMMIO"),
    ("AppleIntelBaseController::writeRegPtr", 0, 0x78, "AppleIntelMMIO"),
    ("AppleIntelBaseController::getMMIOBase", 0, 0x78, "AppleIntelMMIO"),
    ("AppleIntelBaseController::setMMIOBase", 0, 0x78, "AppleIntelMMIO"),

    ("AppleIntelPlane::updateRegisterCache", 0, 0x90, "AppleIntelPlaneRegCache"),
    ("AppleIntelPlane::configurePlane", 0, 0x90, "AppleIntelPlaneRegCache"),

    ("AppleIntelScaler::updateRegisterCache", 0, 0x28, "AppleIntelScalerRegCache"),
    ("AppleIntelScaler::setupPipeScaler", 0, 0x28, "AppleIntelScalerRegCache"),
    ("AppleIntelScaler::programPipeScaler", 0, 0x28, "AppleIntelScalerRegCache"),

    # unk_0C40 is a RegCache pool/allocator captured as ccont — follow it through
    # enable/disable to discover what struct lives at AppleIntelBaseController+0xC40.
    # Struct name TBD; removed until the actual type is confirmed in Ghidra.
]

# Accessor-driven naming hints.
# If a function below resolves to exactly one recovered offset for the target
# parameter, we assign the hinted field name to that offset automatically.
# This helps promote partial PCode structs (especially AppleIntelDisplayPath)
# without hardcoding raw offsets upfront.
# Format:
#   (struct_name, function_fragment, param_index, field_name, c_type_or_None)
AUTO_FIELD_HINTS = [
    ("AppleIntelDisplayPath", "AppleIntelDisplayPath::getConnectorID", 0, "fConnectorID", None),
    ("AppleIntelDisplayPath", "AppleIntelDisplayPath::setConnectorID", 0, "fConnectorID", None),
    ("AppleIntelDisplayPath", "AppleIntelDisplayPath::getConnectorType", 0, "fConnectorType", None),
    ("AppleIntelDisplayPath", "AppleIntelDisplayPath::isConnected", 0, "fConnected", None),
    ("AppleIntelDisplayPath", "AppleIntelDisplayPath::setConnected", 0, "fConnected", None),
    ("AppleIntelDisplayPath", "AppleIntelDisplayPath::getDisplayPort", 0, "fDisplayPort", None),
    ("AppleIntelDisplayPath", "AppleIntelDisplayPath::getAuxChannel", 0, "fAuxChannel", None),
    # AppleIntelPowerWell accessor hints
    ("AppleIntelPowerWell", "AppleIntelPowerWell::getController", 0, "fController", "AppleIntelBaseController*"),
    ("AppleIntelPowerWell", "AppleIntelPowerWell::getIndex",      0, "fIndex",      None),
    ("AppleIntelPowerWell", "AppleIntelPowerWell::isEnabled",     0, "fEnabled",    None),
    ("AppleIntelPowerWell", "AppleIntelPowerWell::isSupported",   0, "fSupported",  None),
]

# Human-readable names for offsets we already know from reverse-engineering.
# Format: struct_name -> {offset: (field_name, c_type_override_or_None)}
# When the PCode walker discovers an offset that appears here the field gets
# the given name instead of the generic field_NNNN auto-name.
# KNOWN_FIELD_NAMES drives two things:
#   1. Naming: when PCode discovers an offset listed here, that field gets the human name.
#   2. Seeding: after PCode analysis, every entry here is MERGED into pcode_structs so
#      known fields are always emitted even when PCode misses the access (e.g. inlined
#      super-calls or compiler-transformed stores).
# Format: struct_name -> {offset: (field_name, c_type_or_None)}
#   c_type_or_None: explicit C type string (e.g. "void*") or None → infer from access size
#   Pointer types (containing "*") are always treated as 8 bytes on x86_64.
KNOWN_FIELD_NAMES = {
    "AppleIntelScaler": {
        0x00: ("fPipeIndex",    None),        # uint32_t pipe index (set in ::init, param_2)
        0x08: ("fController",  "AppleIntelBaseController*"),  # stored at [this+8]
        0x10: ("fPath",        "AppleIntelDisplayPath*"),     # stored at [this+0x10]
        0x18: ("fScalerIndex", None),         # scaler HW index (uint32_t)
        0x20: ("fEnabled",     None),         # bool/uint32_t enable flag
        0x28: ("fRegCache",    "AppleIntelScalerRegCache*"),  # getMember<void*>(this,0x28)
    },
    "AppleIntelPlane": {
        0x00: ("fPipeIndex",   None),         # uint32_t pipe index
        0x08: ("fController",  "AppleIntelBaseController*"),  # controller object
        0x10: ("fPath",        "AppleIntelDisplayPath*"),     # display path object
        0x18: ("fPlaneIndex",  None),         # uint32_t hardware plane index
        0x84: ("fEnabled",     None),         # bool/uint8_t plane-enabled flag
        0x90: ("fRegCache",    "AppleIntelPlaneRegCache*"),   # getMember<void*>(this,0x90)
    },
    "AppleIntelBaseController": {
        0x78:   ("fMMIO",      "AppleIntelMMIO*"),            # MMIO register accessor
        0xC40:  ("unk_0C40",   None),                         # RegCache pool/allocator — captured as ccont in PowerWell::init hook; actual type TBD
        0xC58:  ("flags_ig",   None),                         # boot info flags; FB_FLAG_BOOST_PIXEL_FREQUENCY_LIMIT etc.
        0xC5C:  ("fInfoFlags2",None),                         # display feature flags; FB_FLAG_ALTERNATE_PWM_INCREMENT* etc.
        # fGPUIsAwake is at +0x1A00 but cannot be named in the struct due to alignment drift
        # from unaligned unk_ fields before it. Access via getMember<uint32_t>(ctrl, 0x1A00).
    },
    # AppleIntelPowerWell: fields discovered from PCode analysis of enable/disable/isEnabled/init.
    # Exact offsets TBD by Ghidra; entries will be populated after running the script.
    "AppleIntelPowerWell": {
        # Seed with what we can infer from the init hook:
        # init(this, AppleIntelBaseController*) — the controller pointer must be stored somewhere.
        # Placeholder; PCode analysis will populate the real offsets.
    },
    # AppleIntelFramebuffer inherits IOFramebuffer; Apple-private ivars start well past
    # the IOFramebuffer vtable region (~0x200). Offsets below are from disasm of
    # prepareToEnterWake / prepareToExitWake / setPanelPowerState in TGL kext.
    "AppleIntelFramebuffer": {
        0x1A8: ("fController",     "AppleIntelBaseController*"),  # stored in ::init param_2
        0x1B0: ("fPipeIndex",      None),     # uint32_t (init param_3)
        0x1B8: ("fPath",           "AppleIntelDisplayPath*"),     # display path object
        0x1C0: ("fPanelPower",     None),     # bool/uint32_t panel power state
        0x1C8: ("fBacklightLevel", None),     # uint32_t backlight level
    },
    # AppleIntelDisplayPath: used in setupPipeScaler, setupParams, etc.
    # Offsets discovered from PCode analysis of accessor methods.
    "AppleIntelDisplayPath": {
        # These will be filled by PCode analysis; add known entries here as discovered
    },
    # FlipTransactionArgs: used in configurePlane, configureColorPipeLine
    # Known offsets from NootedGreen's hook implementations:
    "FlipTransactionArgs": {
        0x1C: ("BPCSelector",  None),     # uint32_t: dword element [7] in IDA = BPC/gamma mode (bits[7:0])
        0x3C: ("TilingEnum",   None),     # uint32_t: 0=X-tiled, 1=Y-tiled, else=linear
        # Other fields: stride, surface address, color encoding, etc. TBD from PCode
    },
    "AppleIntelPlaneRegCache": {
        0x100: ("PLANE_CTL",       None),
        0x104: ("PLANE_STRIDE",    None),
        0x154: ("PLANE_COLOR_CTL", None),
    },
    "AppleIntelScalerRegCache": {
        0x00: ("PS_CTRL",    None),
        0x04: ("PS_WIN_POS", None),
        0x08: ("PS_WIN_SZ",  None),
    },
    "AppleIntelMMIO": {
        0x00: ("fMMIOBase", "volatile uint8_t*"),
    },
    # IntelAccelerator: offsets from Ghidra decompile of populateResetRegisterList,
    # and from kern_gen11.cpp getGPUInfo / getGPUInfoICL hooks (confirmed by disasm).
    # Two layouts exist for TGL vs ICL binary:
    #   TGL (getGPUInfo):    0x115c/0x0dd8=NumSlices, 0x1158/0x0ddc=NumSubSlices
    #   ICL (getGPUInfoICL): 0x1190/0x12cc=NumSlices, 0x1188/0x12d0=NumSubSlices
    "IntelAccelerator": {
        # populateResetRegisterList Ghidra decompile
        0x1240: ("fMMIOBase",         "volatile uint8_t*"),  # MMIO BAR0 mapping base
        0x12C0: ("fResetRegCount",    "uint64_t"),           # IGVector.count
        0x12C8: ("fResetRegCapacity", "uint64_t"),           # IGVector.capacity
        0x12D0: ("fResetRegData",     "void*"),              # IGVector.data (0x24-byte entries)
        # TGL binary topology (getGPUInfo hook, confirmed by disasm)
        0x0DD8: ("fNumSlicesMirror",     None),              # NumSlices mirror
        0x0DDC: ("fNumSubSlicesMirror",  None),              # NumSubSlices mirror
        0x1124: ("fExecutionUnitCount",  None),              # MaxEUPerSubSlice × NumSubSlices
        0x1150: ("fFrequencyPair",       "uint64_t"),        # lo=fMaxMHz, hi=fMinMHz
        0x1158: ("fNumSubSlices",        None),
        0x115C: ("fNumSlices",           None),
        0x1164: ("fL3BankCount",         None),
        0x116C: ("fMaxEUPerSubSlice",    None),
        # V44 diagnostic
        0x128C: ("fbRegistered",         "uint8_t"),         # set by registerWithFramebufferController
    },
    # IGAccelTask: offset 0x298 confirmed from ensureTaskContext lambda in NootedGreen.
    "IGAccelTask": {
        0x298: ("fContext", "void*"),  # IGHardwareExtendedContext* for this task
    },
    # IGHardwareBlit3DContext: offset 0 is vtable; other fields TBD from blit3d_init_ctx probe.
    "IGHardwareBlit3DContext": {
        0x00: ("vtable", "void*"),
    },
}

# When True, emit ONLY whitelisted structs (skip Mach-O loader / CodeSign cruft).
# When False, emit every kext-defined struct in the DTM. Default is True so the
# generated AppleIntelParams.hpp stays focused on our targets; flip to False if
# you want a full dump for exploration.
WHITELIST_ONLY = True

# Struct names to ALWAYS skip (Mach-O loader, code-sign blobs, libkern types
# that come in via Ghidra's built-in headers and are noise for our purposes).
SKIP_STRUCT_NAMES = frozenset([
    "mach_header", "segment_command", "section",
    "symtab_command", "dysymtab_command", "linkedit_data_command",
    "source_version_command", "uuid_command", "nlist",
    "CS_BlobIndex", "CS_CodeDirectory", "CS_GenericBlob", "CS_SuperBlob",
])

# Skip enums that are really preprocessor-define noise (Ghidra's Parse C Source
# turns every -D from the parse profile into an enum like "define__POSIX_C_SOURCE").
# These are not real driver enums.
SKIP_ENUM_PREFIXES = ("define_", "_FORTIFY_", "_POSIX_", "_LARGEFILE", "_INTEGRAL_", "__x86_", "__APPLE_", "__GNUC_")

# Known offsets used to cross-check Ghidra and to synthesize stubs.
# (For init-probed structs see KNOWN_FIELD_NAMES above; EXPECTED_OFFSETS
#  is only consulted for the stub emitter when Ghidra has no definition.)
EXPECTED_OFFSETS = {
    "CRTCParams": {
        "TRANS_CLK_SEL":       0x00,
        "TRANS_DDI_FUNC_CTL":  0x04,
        "TRANS_DDI_FUNC_CTL2": 0x08,
        "TRANS_MSA_MISC":      0x0C,
        "TRANS_HTOTAL":        0x10,
        "TRANS_HBLANK":        0x14,
        "TRANS_HSYNC":         0x18,
        "TRANS_VTOTAL":        0x1C,
        "TRANS_VBLANK":        0x20,
        "TRANS_VSYNC":         0x24,
        "PIPE_SRCSZ":          0x28,
        "TRANS_CONF":          0x2C,
        "PPS_0":               0x88,
        "PPS_16":              0xB4,
        "DSC_ENGINE_SEL":      0xE8,
        "DSC_JOINER_CTL":      0xEC,
    },
    "SCALERPARAMS": {
        "PS_CTRL":    0x00,
        "PS_WIN_POS": 0x04,
        "PS_WIN_SZ":  0x08,
    },
    "PLANEPARAMS": {
        "PLANE_CTL":       0x00,
        "PLANE_STRIDE":    0x18,
        "PLANE_SURF":      0x20,
    },
    "AppleIntelPlaneRegCache": {
        "PLANE_CTL":       0x100,
        "PLANE_STRIDE":    0x104,
        "PLANE_COLOR_CTL": 0x154,
    },
}

HEADER_PREAMBLE = u"""\
//  Copyright © 2026 Stezza @ inc. Licensed under the Thou Shalt Not Profit License version 1.0.
//  See LICENSE for details.
//
//  AppleIntelParams.hpp — typed mirror of Apple's framebuffer-driver parameter
//  structures. AUTO-GENERATED by tools/extract_apple_params.py from a Ghidra-
//  analyzed AppleIntelTGLGraphicsFramebuffer.kext. Do not edit by hand — re-run
//  the script after updating struct definitions in Ghidra.
//
//  Source program: {prog_name}
//  Generated: {timestamp}
//
//  Populated structs: {n_populated}  |  Stubs: {n_stubs}  |  Enums: {n_enums}

#ifndef AppleIntelParams_hpp
#define AppleIntelParams_hpp

#include <stdint.h>

namespace AppleIntel {{

struct AppleIntelBaseController;
struct AppleIntelDisplayPath;
struct AppleIntelMMIO;
struct AppleIntelPlaneRegCache;
struct AppleIntelPowerWell;
struct AppleIntelScalerRegCache;

"""

HEADER_EPILOGUE = u"""\
} // namespace AppleIntel

#endif // AppleIntelParams_hpp
"""

# Skip Ghidra built-in categories that never contain kext types
_BUILTIN_PATHS = frozenset([
    "/", "/BuiltInTypes", "/DWARF",
    "/ghidra_builtins", "/ghidra",
])

# ----------------------------------------------------------------------------
# Type helpers
# ----------------------------------------------------------------------------

def _base_type(dt):
    """Unwrap TypeDef chains to the underlying DataType."""
    while isinstance(dt, TypeDef):
        dt = dt.getDataType()
    return dt


def to_ctype(dt, length):
    """Map a Ghidra DataType to a C++ type string.

    Returns scalar/struct/pointer types only. Arrays are handled by the emitter,
    which calls this on the element type and adds the [N] count itself — that
    avoids producing invalid C++ like `uint8_t[16] uuid` (correct: `uint8_t uuid[16]`).
    """
    dt = _base_type(dt)
    name = dt.getName()

    if isinstance(dt, Pointer):
        inner = _base_type(dt.getDataType())
        inner_name = inner.getName() if inner else "void"
        return "{}*".format(inner_name)

    # Ghidra's "string" / TerminatedCString / fixed-length string types — emit char.
    # The scalar-array path in the emitter wraps to `char name[length]` automatically.
    if name in ("string", "TerminatedCString", "TerminatedUnicode", "char"):
        return "char"

    if isinstance(dt, (Structure, Union, Enum)):
        return name

    # Integer/undefined normalization by size
    sz = length if length > 0 else dt.getLength()
    if sz == 8:
        return "uint64_t"
    if sz == 4:
        return "uint32_t"
    if sz == 2:
        return "uint16_t"
    if sz == 1:
        return "uint8_t"

    return name or "uint8_t"


def sanitize(raw, offset):
    if not raw:
        return "field_{:04X}".format(offset)
    cleaned = "".join(c if (c.isalnum() or c == "_") else "_" for c in raw)
    return ("_" + cleaned) if cleaned[:1].isdigit() else cleaned

# ----------------------------------------------------------------------------
# Emitters
# ----------------------------------------------------------------------------

def emit_enum(en):
    name = en.getName()
    sz = en.getLength()
    width_map = {1: "uint8_t", 2: "uint16_t", 4: "uint32_t", 8: "uint64_t"}
    base = width_map.get(sz, "uint32_t")
    lines = ["enum class {} : {} {{".format(name, base)]
    for val_name in en.getNames():
        lines.append("    {} = 0x{:X},".format(val_name, en.getValue(val_name)))
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def emit_struct_or_union(dt, emitted):
    """Emit a Structure or Union recursively, emitting dependencies first."""
    name = dt.getName()
    if name in emitted:
        return ""
    emitted.add(name)

    total = dt.getLength()
    n_comp = dt.getNumDefinedComponents()
    is_union = isinstance(dt, Union)
    keyword = "union" if is_union else "struct"

    out = ""

    # Emit nested types first
    for comp in dt.getDefinedComponents():
        base = _base_type(comp.getDataType())
        if isinstance(base, (Structure, Union)) and base.getName() not in emitted:
            out += emit_struct_or_union(base, emitted) + "\n"
        elif isinstance(base, Enum) and base.getName() not in emitted:
            emitted.add(base.getName())
            out += emit_enum(base) + "\n"

    lines = []
    lines.append("// {} {} -- 0x{:X} bytes, {} components".format(
        keyword, name, total, n_comp))
    lines.append("{} {} {{".format(keyword, name))

    written = 0
    asserts = []
    components = list(dt.getDefinedComponents())
    if not is_union:
        components.sort(key=lambda c: c.getOffset())

    for comp in components:
        offset = comp.getOffset()
        length = comp.getLength()
        cdt = _base_type(comp.getDataType())
        fname = sanitize(comp.getFieldName(), offset)

        if not is_union and offset > written:
            gap = offset - written
            lines.append("    uint8_t _pad_{:04X}[0x{:X}]; // +0x{:X}..+0x{:X}".format(
                written, gap, written, offset - 1))
            written = offset

        if isinstance(cdt, Array):
            # Emit as ELEM NAME[N] (correct C++), not `ELEM[N] NAME`.
            elem = _base_type(cdt.getDataType())
            elem_len = elem.getLength()
            elem_ctype = to_ctype(elem, elem_len)
            count = cdt.getNumElements()
            lines.append("    {:<14} {}[{}]; // +0x{:X}".format(
                elem_ctype, fname, count, offset))
        else:
            ctype = to_ctype(cdt, length)
            # Collapse plain scalar arrays — also handles fixed-length `char`
            # strings (length > 1) into `char name[length]`.
            if length > 0 and ctype in ("uint64_t", "uint32_t", "uint16_t", "uint8_t", "char"):
                tsz = {"uint64_t": 8, "uint32_t": 4, "uint16_t": 2,
                       "uint8_t": 1, "char": 1}[ctype]
                if length > tsz and length % tsz == 0:
                    lines.append("    {:<14} {}[{}]; // +0x{:X}".format(
                        ctype, fname, length // tsz, offset))
                else:
                    lines.append("    {:<14} {}; // +0x{:X}".format(ctype, fname, offset))
            else:
                lines.append("    {:<14} {}; // +0x{:X}, 0x{:X} bytes".format(
                    ctype, fname, offset, length))

        if not fname.startswith("field_") and not fname.startswith("_pad_"):
            asserts.append(
                "static_assert(__builtin_offsetof({}, {}) == 0x{:X}, \"{}.{}\");".format(
                    name, fname, offset, name, fname))

        if not is_union:
            written = offset + length

    if not is_union and written < total:
        gap = total - written
        lines.append("    uint8_t _pad_{:04X}[0x{:X}]; // +0x{:X}..+0x{:X} (trailing)".format(
            written, gap, written, total - 1))

    lines.append("};")
    lines.append("static_assert(sizeof({}) == 0x{:X}, \"{} size\");".format(name, total, name))
    lines.extend(asserts)

    # Cross-check expected offsets
    if name in EXPECTED_OFFSETS:
        actual = {}
        for comp in dt.getDefinedComponents():
            fn = comp.getFieldName()
            if fn:
                actual[sanitize(fn, comp.getOffset())] = comp.getOffset()
        for fn, exp in EXPECTED_OFFSETS[name].items():
            if fn in actual and actual[fn] != exp:
                print("WARNING: {}.{} offset 0x{:X} != expected 0x{:X}".format(
                    name, fn, actual[fn], exp))

    lines.append("")
    out += "\n".join(lines)
    return out


def emit_stub(name):
    """Emit a placeholder struct driven by EXPECTED_OFFSETS."""
    fields = EXPECTED_OFFSETS.get(name, {})
    if not fields:
        # No offset data — emit a minimal opaque placeholder
        lines = [
            "// {} -- STUB: not yet defined in Ghidra".format(name),
            "struct {} {{".format(name),
            "    // TODO: define fields in Ghidra's Data Type Manager",
            "    uint8_t _opaque[1];",
            "};",
            "",
        ]
        return "\n".join(lines)

    sorted_fields = sorted(fields.items(), key=lambda x: x[1])
    # Infer a minimum size: last known offset + 4 bytes, rounded up to 16
    last_off = sorted_fields[-1][1]
    min_size = ((last_off + 4 + 15) // 16) * 16

    lines = ["// {} -- STUB: not yet defined in Ghidra (expected offsets only)".format(name)]
    lines.append("struct {} {{".format(name))
    written = 0
    asserts = []
    for fname, off in sorted_fields:
        if off > written:
            lines.append("    uint8_t _pad_{:04X}[0x{:X}];".format(written, off - written))
        lines.append("    uint32_t {}; // +0x{:X} (expected)".format(fname, off))
        asserts.append(
            "static_assert(__builtin_offsetof({}, {}) == 0x{:X}, \"{}.{} expected\");".format(
                name, fname, off, name, fname))
        written = off + 4
    if written < min_size:
        lines.append("    uint8_t _pad_{:04X}[0x{:X}]; // trailing".format(written, min_size - written))
    lines.append("};")
    lines.extend(asserts)
    lines.append("")
    return "\n".join(lines)


def emit_pcode_struct(struct_name, fields):
    """Emit a struct whose layout was discovered by the PCode walker.

    ``fields`` is a dict {offset_int: size_int} collected from LOAD/STORE ops.
    Known offsets from KNOWN_FIELD_NAMES are used to supply human-readable
    names and optional C-type overrides; all others get field_NNNN names.
    If the dict is empty an opaque stub is returned instead.
    """
    if not fields:
        return (
            "// {} -- PCode analysis yielded no this-pointer accesses\n"
            "struct {} {{ uint8_t _opaque[1]; }};\n\n"
        ).format(struct_name, struct_name)

    known = KNOWN_FIELD_NAMES.get(struct_name, {})
    sorted_f = sorted(fields.items())   # [(offset, size), ...]
    last_off, last_sz = sorted_f[-1]
    # Round total size up to nearest 8-byte boundary (heap objects are aligned).
    total = ((last_off + last_sz + 7) // 8) * 8

    # Size-to-scalar-type map.
    sz_to_type = {8: "uint64_t", 4: "uint32_t", 2: "uint16_t", 1: "uint8_t"}

    lines = [
        "// struct {} -- PCode-discovered from {}::init, {} access sites, ~0x{:X} bytes".format(
            struct_name, struct_name, len(fields), total),
        "struct {} {{".format(struct_name),
    ]
    asserts = []
    written = 0
    for off, sz in sorted_f:
        if off < written:
            # Overlapping accesses (e.g. sub-fields of a union-like member):
            # skip to avoid invalid struct layout.
            continue
        if off > written:
            lines.append("    uint8_t        _pad_{:04X}[0x{:X}]; // +0x{:X}".format(
                written, off - written, written))
        if off in known:
            fname, ctype_override = known[off]
            ctype = ctype_override if ctype_override else sz_to_type.get(sz, "uint8_t")
        else:
            fname = "field_{:04X}".format(off)
            ctype = sz_to_type.get(sz, "uint8_t")
        lines.append("    {:<14} {}; // +0x{:X}".format(ctype, fname, off))
        # Only emit static_assert for named (non-auto) fields.
        # Only assert semantically named fields. Auto placeholders (unk_NNNN)
        # may be unaligned by design and are not stable ABI contracts.
        if off in known and not fname.startswith("unk_"):
            asserts.append(
                "static_assert(__builtin_offsetof({}, {}) == 0x{:X}, \"{}.{}\");".format(
                    struct_name, fname, off, struct_name, fname))
        written = off + sz
    if written < total:
        lines.append("    uint8_t        _pad_{:04X}[0x{:X}]; // trailing".format(
            written, total - written))
    lines.append("};")
    lines.append("// NOTE: total size is a lower bound; "
                 "extend once the real sizeof() is known from IDA/Ghidra.")
    lines.extend(asserts)
    lines.append("")
    return "\n".join(lines)


# ----------------------------------------------------------------------------
# PCode struct-discovery helpers
# ----------------------------------------------------------------------------

def _find_functions_by_fragment(program, fragment):
    """Return a list of Function objects whose demangled name contains *fragment*."""
    fm = program.getFunctionManager()
    matches = []
    for fn in fm.getFunctions(True):
        try:
            # getName(True) returns the demangled name when available.
            full = fn.getName(True)
        except Exception:
            full = fn.getName()
        if fragment in full:
            matches.append(fn)
    return matches


def analyze_param_offsets(program, func_name_fragment, param_index=0):
    """Decompile *func_name_fragment* and harvest member accesses from one parameter.

    Returns a dict {offset: max_access_size} representing every distinct
    constant offset reached via PTRSUB or PTRADD from the chosen parameter
    that feeds into a LOAD or STORE.

    The decompiler's PCode representation is used so that the analysis is
    architecture-independent and survives inlining / optimisation.
    """
    fns = _find_functions_by_fragment(program, func_name_fragment)
    if not fns:
        print("  [pcode-probe] No function matched: '{}'".format(func_name_fragment))
        return {}
    if len(fns) > 1:
        print("  [pcode-probe] {} matches for '{}'; using first: {}".format(
            len(fns), func_name_fragment, fns[0].getName(True)))

    fn = fns[0]
    print("  [pcode-probe] Decompiling {} at {}...".format(
        fn.getName(True), fn.getEntryPoint()))

    ifc = DecompInterface()
    try:
        opts = DecompileOptions()
        ifc.setOptions(opts)
        if not ifc.openProgram(program):
            print("  [pcode-probe] DecompInterface.openProgram() failed")
            return {}

        res = ifc.decompileFunction(fn, 120, None)
        if not res.decompileCompleted():
            print("  [pcode-probe] Decompile did not complete: {}".format(
                res.getErrorMessage()))
            return {}

        hfunc = res.getHighFunction()

        # Collect the varnode(s) for the selected parameter so we can trace
        # accesses through copies (CASTs, COPYs).
        param_varnodes = set()
        proto = hfunc.getFunctionPrototype()
        num_in = proto.getNumParams()
        if num_in <= param_index:
            print("  [pcode-probe] Function has {} parameter(s) — cannot analyze param {}".format(
                num_in, param_index))
            return {}
        target_param = proto.getParam(param_index)
        if target_param is not None:
            hvar = target_param.getHighVariable()
            if hvar is not None:
                for vn in hvar.getInstances():
                    param_varnodes.add(vn)

        fields = {}  # offset -> max_access_size

        for block in hfunc.getBasicBlocks():
            it = block.getIterator()
            while it.hasNext():
                op = it.next()
                opcode = op.getOpcode()

                # We only care about pointer-arithmetic ops that produce an
                # address used by LOAD or STORE.
                if opcode not in (PcodeOp.PTRSUB, PcodeOp.PTRADD,
                                  PcodeOp.INT_ADD):
                    continue

                # input(0) should be or derive from `this`;
                # input(1) is the offset constant.
                ptr_in  = op.getInput(0)
                off_in  = op.getInput(1)
                if ptr_in is None or off_in is None:
                    continue
                if not off_in.isConstant():
                    continue

                # Quick check: is the base related to the selected parameter?
                # We only follow one level of copy / cast here; a deeper
                # alias analysis would require a full DFG traversal.
                if ptr_in not in param_varnodes:
                    # Accept if it's a CAST/COPY whose input is in param_varnodes.
                    def_op = ptr_in.getDef()
                    if def_op is None:
                        continue
                    if def_op.getOpcode() not in (PcodeOp.CAST, PcodeOp.COPY,
                                                   PcodeOp.INT_ZEXT,
                                                   PcodeOp.INT_SEXT,
                                                   PcodeOp.MULTIEQUAL):
                        continue
                    found = False
                    for i in range(def_op.getNumInputs()):
                        if def_op.getInput(i) in param_varnodes:
                            found = True
                            break
                    if not found:
                        continue

                raw_offset = off_in.getOffset()
                # Ghidra encodes negative offsets as large unsigned integers on
                # 64-bit; cap to a sane range (structs won't be > 64 KiB).
                if raw_offset > 0xFFFF:
                    continue

                # Walk consumers of the computed pointer to find the access size.
                out_vn = op.getOutput()
                if out_vn is None:
                    continue
                desc_it = out_vn.getDescendants()
                while desc_it.hasNext():
                    use_op = desc_it.next()
                    uc = use_op.getOpcode()
                    if uc == PcodeOp.LOAD:
                        out = use_op.getOutput()
                        sz = out.getSize() if out is not None else 4
                    elif uc == PcodeOp.STORE:
                        val = use_op.getInput(2)
                        sz = val.getSize() if val is not None else 4
                    else:
                        continue
                    if sz < 1:
                        sz = 1
                    # Keep the largest access seen at this offset
                    # (smallest access could be a flag byte inside a dword).
                    if raw_offset not in fields or fields[raw_offset] < sz:
                        fields[raw_offset] = sz

        print("  [pcode-probe] {} discovered {} field offset(s) in {}".format(
            func_name_fragment, len(fields), fn.getName(True)))
        return fields

    finally:
        ifc.dispose()


def analyze_this_offsets(program, func_name_fragment):
    """Backward-compatible wrapper for `this`-pointer analysis."""
    return analyze_param_offsets(program, func_name_fragment, 0)


def analyze_nested_param_offsets(program, func_name_fragment, param_index, pointer_field_offset):
    """Harvest pointee offsets from a pointer field loaded from one parameter.

    Example: if `this + 0x78` is `AppleIntelBaseController::fMMIO`, this routine
    follows the load of that pointer and records offsets used through the pointee.
    Returns a dict {offset: max_access_size} for the nested struct.
    """
    fns = _find_functions_by_fragment(program, func_name_fragment)
    if not fns:
        print("  [nested-probe] No function matched: '{}'".format(func_name_fragment))
        return {}
    if len(fns) > 1:
        print("  [nested-probe] {} matches for '{}'; using first: {}".format(
            len(fns), func_name_fragment, fns[0].getName(True)))

    fn = fns[0]
    print("  [nested-probe] Decompiling {} at {}...".format(
        fn.getName(True), fn.getEntryPoint()))

    ifc = DecompInterface()
    try:
        opts = DecompileOptions()
        ifc.setOptions(opts)
        if not ifc.openProgram(program):
            print("  [nested-probe] DecompInterface.openProgram() failed")
            return {}

        res = ifc.decompileFunction(fn, 120, None)
        if not res.decompileCompleted():
            print("  [nested-probe] Decompile did not complete: {}".format(
                res.getErrorMessage()))
            return {}

        hfunc = res.getHighFunction()
        proto = hfunc.getFunctionPrototype()
        num_in = proto.getNumParams()
        if num_in <= param_index:
            print("  [nested-probe] Function has {} parameter(s) — cannot analyze param {}".format(
                num_in, param_index))
            return {}

        param_varnodes = set()
        target_param = proto.getParam(param_index)
        if target_param is not None:
            hvar = target_param.getHighVariable()
            if hvar is not None:
                for vn in hvar.getInstances():
                    param_varnodes.add(vn)

        nested_ptr_varnodes = set()

        for block in hfunc.getBasicBlocks():
            it = block.getIterator()
            while it.hasNext():
                op = it.next()
                opcode = op.getOpcode()
                if opcode not in (PcodeOp.PTRSUB, PcodeOp.PTRADD, PcodeOp.INT_ADD):
                    continue

                ptr_in = op.getInput(0)
                off_in = op.getInput(1)
                if ptr_in is None or off_in is None or not off_in.isConstant():
                    continue
                if off_in.getOffset() != pointer_field_offset:
                    continue

                if ptr_in not in param_varnodes:
                    def_op = ptr_in.getDef()
                    if def_op is None:
                        continue
                    if def_op.getOpcode() not in (PcodeOp.CAST, PcodeOp.COPY,
                                                  PcodeOp.INT_ZEXT, PcodeOp.INT_SEXT,
                                                  PcodeOp.MULTIEQUAL):
                        continue
                    found = False
                    for i in range(def_op.getNumInputs()):
                        if def_op.getInput(i) in param_varnodes:
                            found = True
                            break
                    if not found:
                        continue

                out_vn = op.getOutput()
                if out_vn is None:
                    continue

                desc_it = out_vn.getDescendants()
                while desc_it.hasNext():
                    use_op = desc_it.next()
                    if use_op.getOpcode() != PcodeOp.LOAD:
                        continue
                    loaded_ptr = use_op.getOutput()
                    if loaded_ptr is not None:
                        nested_ptr_varnodes.add(loaded_ptr)

        if not nested_ptr_varnodes:
            print("  [nested-probe] No pointer loads found from +0x{:X} in {}".format(
                pointer_field_offset, fn.getName(True)))
            return {}

        fields = {}
        for block in hfunc.getBasicBlocks():
            it = block.getIterator()
            while it.hasNext():
                op = it.next()
                opcode = op.getOpcode()
                if opcode not in (PcodeOp.PTRSUB, PcodeOp.PTRADD, PcodeOp.INT_ADD):
                    continue

                ptr_in = op.getInput(0)
                off_in = op.getInput(1)
                if ptr_in is None or off_in is None or not off_in.isConstant():
                    continue
                if ptr_in not in nested_ptr_varnodes:
                    def_op = ptr_in.getDef()
                    if def_op is None:
                        continue
                    if def_op.getOpcode() not in (PcodeOp.CAST, PcodeOp.COPY,
                                                  PcodeOp.INT_ZEXT, PcodeOp.INT_SEXT,
                                                  PcodeOp.MULTIEQUAL):
                        continue
                    found = False
                    for i in range(def_op.getNumInputs()):
                        if def_op.getInput(i) in nested_ptr_varnodes:
                            found = True
                            break
                    if not found:
                        continue

                raw_offset = off_in.getOffset()
                if raw_offset > 0xFFFF:
                    continue

                out_vn = op.getOutput()
                if out_vn is None:
                    continue
                desc_it = out_vn.getDescendants()
                while desc_it.hasNext():
                    use_op = desc_it.next()
                    uc = use_op.getOpcode()
                    if uc == PcodeOp.LOAD:
                        out = use_op.getOutput()
                        sz = out.getSize() if out is not None else 4
                    elif uc == PcodeOp.STORE:
                        val = use_op.getInput(2)
                        sz = val.getSize() if val is not None else 4
                    else:
                        continue
                    if sz < 1:
                        sz = 1
                    if raw_offset not in fields or fields[raw_offset] < sz:
                        fields[raw_offset] = sz

        print("  [nested-probe] {} discovered {} nested field offset(s) via +0x{:X} in {}".format(
            func_name_fragment, len(fields), pointer_field_offset, fn.getName(True)))
        return fields

    finally:
        ifc.dispose()


def _fmt_offset_list(offsets):
    return ", ".join("+0x{:X}".format(off) for off in sorted(offsets)) if offsets else "(none)"


def print_partial_report(with_fields, promoted_pcode, pcode_only):
    """Print a compact checklist of structs that still need field recovery."""
    partial_names = []
    for name in sorted(pcode_only):
        known = KNOWN_FIELD_NAMES.get(name, {})
        recovered = sorted(pcode_only[name].keys())
        named = [off for off in recovered if off in known]
        unnamed = [off for off in recovered if off not in known]
        partial_names.append((name, len(named), len(unnamed), recovered, known))

    if not partial_names:
        return

    print("  Remaining partials:")
    for name, named_count, unnamed_count, recovered, known in partial_names:
        known_offsets = set(known.keys())
        missing_named = sorted(known_offsets - set(recovered))
        print("    - {}: {} recovered, {} unnamed, missing {}".format(
            name, named_count + unnamed_count, unnamed_count, len(missing_named)))
        if missing_named:
            print("      missing named offsets: {}".format(_fmt_offset_list(missing_named)))
        if unnamed_count:
            print("      unnamed recovered offsets: {}".format(_fmt_offset_list(off for off in recovered if off not in known_offsets)))


def auto_name_recovered_offsets(pcode_structs):
    """Assign stable placeholder names for recovered-but-unnamed offsets.

    This converts `field_NNNN` placeholders into deterministic `unk_NNNN` names
    by extending KNOWN_FIELD_NAMES at runtime. It keeps offsets explicit and
    allows PCode-complete structs to move out of the partial bucket.
    """
    added = 0
    for struct_name, fields in pcode_structs.items():
        known = KNOWN_FIELD_NAMES.setdefault(struct_name, {})
        for off in sorted(fields.keys()):
            if off in known:
                continue
            known[off] = ("unk_{:04X}".format(off), None)
            added += 1
    if added:
        print("Auto placeholder naming: added {} recovered offset name(s)".format(added))


def apply_auto_field_hints(program):
    """Auto-assign names from accessor hints when a hint yields one clear offset."""
    if not AUTO_FIELD_HINTS:
        return

    print("Auto field-hint discovery ({} hints)...".format(len(AUTO_FIELD_HINTS)))
    # Collect candidate offset sets per (struct_name, field_name).
    # If individual accessors are ambiguous, we resolve by intersecting getter/setter
    # candidate sets for the same logical field.
    candidate_sets = {}   # (struct_name, field_name) -> [set(offsets), ...]
    ctype_by_field = {}   # (struct_name, field_name) -> ctype_override

    for struct_name, func_fragment, param_index, field_name, ctype_override in AUTO_FIELD_HINTS:
        fields = analyze_param_offsets(program, func_fragment, param_index)
        offsets = set(fields.keys())
        if not offsets:
            continue

        key = (struct_name, field_name)
        candidate_sets.setdefault(key, []).append(offsets)
        if ctype_override is not None and key not in ctype_by_field:
            ctype_by_field[key] = ctype_override

        if len(offsets) > 1:
            print("  [auto-hint] {} in {} has {} candidate offsets".format(
                func_fragment, struct_name, len(offsets)))

    for (struct_name, field_name), sets_list in candidate_sets.items():
        resolved = set(sets_list[0])
        for off_set in sets_list[1:]:
            resolved &= off_set

        # Fallback: if intersection is empty and we only had one accessor sample,
        # allow a direct single-offset resolution.
        if not resolved and len(sets_list) == 1 and len(sets_list[0]) == 1:
            resolved = set(sets_list[0])

        if len(resolved) != 1:
            if resolved:
                print("  [auto-hint] unresolved {}.{} => {} possible offsets ({})".format(
                    struct_name, field_name, len(resolved), _fmt_offset_list(resolved)))
            else:
                print("  [auto-hint] unresolved {}.{} => empty intersection".format(
                    struct_name, field_name))
            continue

        off = next(iter(resolved))
        known = KNOWN_FIELD_NAMES.setdefault(struct_name, {})
        if off in known:
            continue

        known[off] = (field_name, ctype_by_field.get((struct_name, field_name)))
        print("  [auto-hint] {}.{} => +0x{:X}".format(struct_name, field_name, off))

# ----------------------------------------------------------------------------
# Discovery
# ----------------------------------------------------------------------------

def _is_builtin(dt):
    """Return True for Ghidra built-in / synthetic types we don't want."""
    try:
        path = dt.getCategoryPath().toString()
        if any(path == b or path.startswith(b + "/") for b in _BUILTIN_PATHS):
            return True
    except Exception:
        pass
    name = dt.getName()
    # Ghidra auto-names like "undefined", "undefined4", etc.
    if name.startswith("undefined") or name in ("void", "bool", "char",
            "uchar", "short", "ushort", "int", "uint", "long", "ulong",
            "longlong", "ulonglong", "float", "double", "string"):
        return True
    return False


def _unwrap_pointer(dt):
    dt = _base_type(dt)
    if isinstance(dt, Pointer):
        return _base_type(dt.getDataType())
    return dt


def _better_struct(existing, candidate):
    """Return True if `candidate` is a more useful definition than `existing`.

    Multiple categories in the DTM can contain a struct with the same name —
    e.g. `/Demangler/CRTCParams` (1-byte placeholder Ghidra auto-creates when
    it sees a function signature) AND `/ghidra_seed_structs.h/CRTCParams` (the
    real 0xF0-byte version from our seed file). Prefer the one with the most
    defined components; on ties, prefer the larger size.
    """
    if existing is None:
        return True
    ec = existing.getNumDefinedComponents()
    cc = candidate.getNumDefinedComponents()
    if cc != ec:
        return cc > ec
    return candidate.getLength() > existing.getLength()


def _is_single_opaque_blob(dt):
    """Return True when a struct is just one full-size opaque field.

    Ghidra seed headers sometimes define placeholders like:
      struct Foo { uint8_t _opaque[N]; };
    These are not meaningful recovered layouts and should not be emitted in
    the generated header.
    """
    if not isinstance(dt, Structure):
        return False
    if dt.getNumDefinedComponents() != 1:
        return False
    total = dt.getLength()
    if total <= 0:
        return False

    comp = dt.getDefinedComponents()[0]
    if comp.getOffset() != 0 or comp.getLength() != total:
        return False

    fname = sanitize(comp.getFieldName(), comp.getOffset())
    return fname.startswith("_opaque")


def collect_all_types(program):
    """Return dicts: structs{name->dt}, unions{name->dt}, enums{name->dt}.

    Deduplicates by name across DTM categories: when multiple structs share a
    name (e.g. a Demangler 1-byte placeholder vs our parsed seed-file version),
    keep the one with the most defined components / largest size.
    """
    dtm = program.getDataTypeManager()
    structs, unions, enums = {}, {}, {}

    # 1. Everything in the DTM
    from java.util import ArrayList
    all_dt_list = ArrayList()
    dtm.getAllDataTypes(all_dt_list)
    for dt in all_dt_list:
        if _is_builtin(dt):
            continue
        if isinstance(dt, Structure):
            n = dt.getName()
            if _better_struct(structs.get(n), dt):
                structs[n] = dt
        elif isinstance(dt, Union):
            unions.setdefault(dt.getName(), dt)
        elif isinstance(dt, Enum):
            enums.setdefault(dt.getName(), dt)

    # 2. Function signatures and locals — but only register candidates we
    # haven't already collected via the DTM scan above. (setdefault used to
    # win over our DTM choice with a Demangler placeholder; now we just skip.)
    fm = program.getFunctionManager()
    for fn in fm.getFunctions(True):
        for candidate in ([fn.getReturnType()]
                          + [p.getDataType() for p in fn.getParameters()]):
            base = _unwrap_pointer(candidate)
            if isinstance(base, Structure) and not _is_builtin(base):
                n = base.getName()
                if n not in structs and _better_struct(None, base):
                    structs[n] = base
            elif isinstance(base, Union) and not _is_builtin(base):
                unions.setdefault(base.getName(), base)
            elif isinstance(base, Enum) and not _is_builtin(base):
                enums.setdefault(base.getName(), base)
        try:
            for var in fn.getAllVariables():
                base = _unwrap_pointer(var.getDataType())
                if isinstance(base, Structure) and not _is_builtin(base):
                    n = base.getName()
                    if n not in structs:
                        structs[n] = base
        except Exception:
            pass

    # 3. Defined data in all memory blocks
    listing = program.getListing()
    mem = program.getMemory()
    for block in mem.getBlocks():
        addr = block.getStart()
        end = block.getEnd()
        while addr is not None and addr <= end:
            data = listing.getDataAt(addr)
            if data is not None:
                base = _unwrap_pointer(data.getDataType())
                if isinstance(base, Structure) and not _is_builtin(base):
                    structs.setdefault(base.getName(), base)
                elif isinstance(base, Union) and not _is_builtin(base):
                    unions.setdefault(base.getName(), base)
                elif isinstance(base, Enum) and not _is_builtin(base):
                    enums.setdefault(base.getName(), base)
                addr = data.getMaxAddress().add(1)
            else:
                try:
                    addr = addr.add(1)
                except Exception:
                    break

    return structs, unions, enums

# ----------------------------------------------------------------------------
# Function signature validation
# ----------------------------------------------------------------------------

def discover_function_signatures(program):
    """Look up each SIG_TARGETS entry in the symbol table and compare the
    actual mangled name with the expected one we use in NootedGreen hooks.

    Returns a list of (hook_label, status, actual_mangled, expected_mangled).
    status is one of: "OK", "MISMATCH", "NOT_FOUND", "NO_SYMBOL".
    """
    sym_table = program.getSymbolTable()
    results = []

    for fragment, expected, hook_label in SIG_TARGETS:
        matches = _find_functions_by_fragment(program, fragment)
        if not matches:
            results.append((hook_label, "NOT_FOUND", None, expected))
            continue

        # Search all symbols at the entry point for a mangled (__Z) name.
        actual_mangled = None
        for fn in matches:
            entry = fn.getEntryPoint()
            for sym in sym_table.getSymbols(entry):
                sname = sym.getName()
                if sname.startswith("__Z"):
                    actual_mangled = sname
                    break
            if actual_mangled:
                break

        # Fallback: the function name itself may already be mangled.
        if actual_mangled is None:
            fn_name = matches[0].getName()
            if fn_name.startswith("__Z"):
                actual_mangled = fn_name

        if actual_mangled is None:
            results.append((hook_label, "NO_SYMBOL",
                            matches[0].getName(True), expected))
        elif actual_mangled == expected:
            results.append((hook_label, "OK", actual_mangled, expected))
        else:
            results.append((hook_label, "MISMATCH", actual_mangled, expected))

    return results


def print_sig_report(results):
    """Print a human-readable signature validation table."""
    ok = sum(1 for _, s, _, _ in results if s == "OK")
    print("Signature validation: {}/{} OK".format(ok, len(results)))
    for hook_label, status, actual, expected in results:
        if status == "OK":
            print("  [OK]       {}".format(hook_label))
        elif status == "NOT_FOUND":
            print("  [NOT FOUND] {} — symbol not in binary".format(hook_label))
        elif status == "NO_SYMBOL":
            print("  [NO MANGLED] {} — demangled: {}".format(hook_label, actual))
        else:
            print("  [MISMATCH] {}".format(hook_label))
            print("    expected: {}".format(expected))
            print("    actual  : {}".format(actual))


# ----------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------

def main():
    program = currentProgram
    if program is None:
        print("No program open in Ghidra")
        return

    print("Scanning {}...".format(program.getName()))

    # -----------------------------------------------------------------------
    # Phase -1 — Signature validation
    # Verify that each mangled symbol used in NootedGreen hooks matches what
    # is actually in the binary.  Run this first so any mismatch is visible
    # before the slow PCode analysis begins.
    # -----------------------------------------------------------------------
    print("Signature validation ({} targets)...".format(len(SIG_TARGETS)))
    sig_results = discover_function_signatures(program)
    print_sig_report(sig_results)


    # -----------------------------------------------------------------------
    # Phase 0 — PCode struct discovery
    # Walk every function listed in INIT_FUNC_TARGETS and harvest parameter-based
    # member accesses to reconstruct struct layouts. Results are stored in
    # pcode_structs: {struct_name -> {offset: size}}.
    # -----------------------------------------------------------------------
    pcode_structs = {}  # struct_name -> {offset: size}
    print("PCode struct discovery ({} targets)...".format(len(INIT_FUNC_TARGETS)))
    for func_fragment, struct_name, param_index in INIT_FUNC_TARGETS:
        fields = analyze_param_offsets(program, func_fragment, param_index)
        if fields:
            if struct_name in pcode_structs:
                # Merge: keep max access size per offset across multiple overloads.
                existing = pcode_structs[struct_name]
                for off, sz in fields.items():
                    if off not in existing or existing[off] < sz:
                        existing[off] = sz
            else:
                pcode_structs[struct_name] = fields
            print("  {}: {} offsets discovered".format(struct_name, len(pcode_structs[struct_name])))

    print("Nested-pointer struct discovery ({} targets)...".format(len(NESTED_POINTER_TARGETS)))
    for func_fragment, param_index, pointer_field_offset, struct_name in NESTED_POINTER_TARGETS:
        fields = analyze_nested_param_offsets(program, func_fragment, param_index, pointer_field_offset)
        if fields:
            if struct_name in pcode_structs:
                existing = pcode_structs[struct_name]
                for off, sz in fields.items():
                    if off not in existing or existing[off] < sz:
                        existing[off] = sz
            else:
                pcode_structs[struct_name] = fields
            print("  {}: {} offsets discovered after nested merge".format(struct_name, len(pcode_structs[struct_name])))

    # Accessor-level automatic naming for fields where we can infer a stable name
    # from get*/set*/is* style methods.
    apply_auto_field_hints(program)

    # Any still-unnamed recovered offsets get stable placeholder names so
    # generated structs are fully named (unk_NNNN) instead of field_NNNN.
    auto_name_recovered_offsets(pcode_structs)

    # Seed phase: for every struct in KNOWN_FIELD_NAMES, inject its entries into
    # pcode_structs so they are always emitted even when PCode analysis missed them
    # (e.g. very short init that only calls super, or inlined assignments).
    def _known_size(ctype_override):
        """Infer byte size from a KNOWN_FIELD_NAMES c_type string."""
        if ctype_override and "*" in ctype_override:
            return 8   # pointer on x86_64
        return 4       # default scalar

    for struct_name, known in KNOWN_FIELD_NAMES.items():
        entry = pcode_structs.setdefault(struct_name, {})
        for off, (fname, ctype_override) in known.items():
            sz = _known_size(ctype_override)
            if off not in entry or entry[off] < sz:
                entry[off] = sz
        print("  {}: seeded {} known offsets (total {} after merge)".format(
            struct_name, len(known), len(entry)))

    structs, unions, enums = collect_all_types(program)
    print("  Structures : {}".format(len(structs)))
    print("  Unions     : {}".format(len(unions)))
    print("  Enums      : {}".format(len(enums)))

    # Four tiers:
    #   with_fields  : size >= 4 AND has defined components  -> full emit
    #   pcode_only   : PCode-discovered, not in with_fields  -> PCode emit
    #   sized_opaque : size >= 4 AND 0 components, not pcode -> opaque sized stub
    #   tiny         : size < 4 AND 0 components             -> skip (true 1-byte placeholder)
    with_fields  = {}
    sized_opaque = {}
    tiny         = {}
    opaque_blob  = {}
    for n, s in structs.items():
        if n in SKIP_STRUCT_NAMES:
            continue
        if WHITELIST_ONLY and n not in STRUCT_WHITELIST:
            continue
        if _is_single_opaque_blob(s):
            opaque_blob[n] = s
            continue
        sz = s.getLength()
        nc = s.getNumDefinedComponents()
        if nc > 0 and sz >= 4:
            with_fields[n] = s
        elif nc == 0 and sz >= 4:
            sized_opaque[n] = s
        else:
            tiny[n] = s

    # PCode promotion buckets:
    #   promoted_pcode : fully named layouts recovered from PCode
    #   pcode_only     : still has placeholders / missing fields
    promoted_pcode = {}
    pcode_only = {}
    for n, f in pcode_structs.items():
        if n in with_fields:
            continue
        known = KNOWN_FIELD_NAMES.get(n, {})
        if f and all(off in known for off in f.keys()) and all(not fname.startswith("field_") for off, (fname, _) in known.items() if off in f):
            promoted_pcode[n] = f
        else:
            pcode_only[n] = f

    print("  With fields   : {}".format(len(with_fields)))
    print("  Promoted PCode: {}".format(len(promoted_pcode)))
    print("  PCode-only    : {}".format(len(pcode_only)))
    print("  Sized opaque  : {}".format(len(sized_opaque)))
    print("  Opaque blobs  : {} (skipped)".format(len(opaque_blob)))
    print("  Tiny/skip     : {}".format(len(tiny)))
    print_partial_report(with_fields, promoted_pcode, pcode_only)

    # Whitelist coverage
    for wanted in STRUCT_WHITELIST:
        in_full   = wanted in with_fields
        in_promoted = wanted in promoted_pcode
        in_pcode  = wanted in pcode_only
        in_opaque = wanted in sized_opaque
        in_blob   = wanted in opaque_blob
        in_tiny   = wanted in tiny
        if not (in_full or in_promoted or in_pcode or in_opaque or in_blob or in_tiny) and wanted not in structs:
            print("WARNING: '{}' not found anywhere - add it to Ghidra or INIT_FUNC_TARGETS".format(wanted))
        elif in_tiny and not (in_full or in_promoted or in_pcode):
            print("WARNING: '{}' is a 1-byte placeholder - define fields in Ghidra".format(wanted))
        elif in_blob and not (in_full or in_promoted or in_pcode):
            print("INFO: '{}' is a full-size opaque blob and was skipped".format(wanted))

    # Stubs: whitelist names absent from both with_fields and pcode buckets
    stub_names = [n for n in STRUCT_WHITELIST
                  if n not in with_fields and n not in pcode_only and n not in promoted_pcode and n not in opaque_blob]

    n_fields  = len(with_fields)
    n_promoted = len(promoted_pcode)
    n_pcode   = len(pcode_only)
    n_opaque  = len(sized_opaque)
    n_stubs   = len(stub_names)
    n_enums   = len(enums)

    if n_fields == 0 and n_promoted == 0 and n_pcode == 0 and n_opaque == 0 and n_stubs == 0 and n_enums == 0:
        print("Nothing to emit.")
        return

    out_path = askFile("Save AppleIntelParams.hpp to", "Save")
    out_file = out_path.getAbsolutePath()

    body = HEADER_PREAMBLE.format(
        prog_name=program.getName(),
        timestamp=datetime.datetime.now().isoformat(),
        n_populated=n_fields + n_promoted + n_pcode,
        n_stubs=n_stubs,
        n_enums=n_enums,
    )

    emitted = set()

    # Enums first (no dependencies). Skip preprocessor-define noise.
    if enums:
        kept = [n for n in sorted(enums)
                if not any(n.startswith(pfx) for pfx in SKIP_ENUM_PREFIXES)]
        if kept:
            body += "// ---- Enums ----\n\n"
            for name in kept:
                emitted.add(name)
                body += emit_enum(enums[name]) + "\n"

    # Unions
    if unions:
        body += "// ---- Unions ----\n\n"
        for name in sorted(unions):
            body += emit_struct_or_union(unions[name], emitted) + "\n"

    # Structs with named fields
    if with_fields:
        body += "// ---- Structs (fields identified) ----\n\n"
        for name in sorted(with_fields):
            body += emit_struct_or_union(with_fields[name], emitted) + "\n"

    # Sized structs with no fields yet - emit opaque with correct size
    if sized_opaque:
        body += "// ---- Structs (sized, fields not yet identified in Ghidra) ----\n\n"
        for name in sorted(sized_opaque):
            if name in emitted:
                continue
            emitted.add(name)
            sz = sized_opaque[name].getLength()
            body += (
                "// {} -- 0x{:X} bytes, no fields identified yet\n"
                "struct {} {{\n"
                "    uint8_t _opaque[0x{:X}]; // TODO: name fields in Ghidra\n"
                "}};\n"
                "static_assert(sizeof({}) == 0x{:X}, \"{} size\");\n\n"
            ).format(name, sz, name, sz, name, sz, name)

    # PCode-discovered structs (better than opaque stubs but not yet in Ghidra's DTM).
    if promoted_pcode:
        body += "// ---- Structs (PCode-discovered, fully named) ----\n"
        body += "// These layouts are fully named from KNOWN_FIELD_NAMES and can be promoted as-is.\n\n"
        for name in sorted(promoted_pcode):
            if name not in emitted:
                emitted.add(name)
                body += emit_pcode_struct(name, promoted_pcode[name]) + "\n"

    if pcode_only:
        body += "// ---- Structs (PCode-discovered from function analysis) ----\n"
        body += "// Fields were reconstructed by tracing direct parameter accesses and nested pointer dereferences.\n"
        body += "// Names from KNOWN_FIELD_NAMES are accurate; field_NNNN names are placeholders.\n\n"
        for name in sorted(pcode_only):
            if name not in emitted:
                emitted.add(name)
                body += emit_pcode_struct(name, pcode_only[name]) + "\n"

    # EXPECTED_OFFSETS stubs for whitelist structs not yet fully defined
    if stub_names:
        body += "// ---- Stubs (whitelist structs not yet defined in Ghidra) ----\n\n"
        for name in stub_names:
            if name not in emitted:
                body += emit_stub(name) + "\n"

    body += HEADER_EPILOGUE

    with open(out_file, "w") as f:
        f.write(body.encode("utf-8") if isinstance(body, unicode) else body)

    print("Wrote to {}".format(out_file))
    print("  With fields   : {}".format(n_fields))
    print("  Promoted PCode: {}".format(n_promoted))
    print("  PCode-only    : {}".format(n_pcode))
    print("  Sized opaque  : {}".format(n_opaque))
    print("  Stubs         : {}".format(n_stubs))
    print("  Enums         : {}".format(n_enums))
    print("  Unions        : {}".format(len(unions)))


main()
