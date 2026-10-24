# GhidraMMIOExport.py
# Ghidra script — run from Script Manager (Window > Script Manager > Run Script)
# Tested with Ghidra 10.x / 11.x Jython scripting environment.
#
# What it does:
#   1. Iterates every function in the binary.
#   2. Uses the Decompiler API to get pseudo-C for each function.
#   3. Scans instructions for immediate constants in known MMIO ranges.
#   4. Records: address, function name, read/write type, nearby AND mask (bitfield),
#      and a snippet of decompiled lines around each access.
#   5. Writes JSON to <binary_dir>/ghidra_mmio_export.json
#
# Run instructions:
#   1. Open AppleIntelTGLGraphicsFramebuffer in Ghidra and analyse it.
#   2. Window > Script Manager > click the "Script Directories" folder icon
#      and add the folder containing this file.
#   3. Find GhidraMMIOExport.py in the list and double-click to run.
#   4. JSON is written next to the binary (check the console for exact path).
#
# @category   Analysis
# @menupath   Analysis.MMIO Export for Linux Mapper

import json
import os

from ghidra.app.decompiler import DecompileOptions, DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.program.model.listing import CodeUnit
from ghidra.program.model.scalar import Scalar

# ---------------------------------------------------------------------------
# MMIO ranges relevant for ICL / TGL
# ---------------------------------------------------------------------------
MMIO_RANGES = [
    (0x40000, 0x4FFFF),
    (0x60000, 0x6FFFF),
    (0x160000, 0x16FFFF),
]

def in_range(v):
    return any(lo <= v <= hi for lo, hi in MMIO_RANGES)

# ---------------------------------------------------------------------------
# Decompiler setup
# ---------------------------------------------------------------------------
def make_decompiler(program):
    ifc = DecompInterface()
    opts = DecompileOptions()
    ifc.setOptions(opts)
    ifc.openProgram(program)
    return ifc

def decompile_function(ifc, func, monitor):
    result = ifc.decompileFunction(func, 30, monitor)
    if result and result.decompileCompleted():
        return result.getDecompiledFunction().getC()
    return ""

# ---------------------------------------------------------------------------
# Instruction-level scan for MMIO immediates + nearby AND mask
# ---------------------------------------------------------------------------
def get_mask_near(listing, addr, window=6):
    """
    Look at up to 'window' instructions after addr for an AND with a small mask.
    Returns the mask as int if found, else None.
    """
    cu = listing.getCodeUnitAt(addr)
    if cu is None:
        return None
    for _ in range(window):
        cu = listing.getCodeUnitAfter(cu.getAddress())
        if cu is None:
            break
        mnem = cu.getMnemonicString().upper() if hasattr(cu, "getMnemonicString") else ""
        if mnem in ("AND", "TEST"):
            for i in range(cu.getNumOperands()):
                op = cu.getDefaultOperandRepresentation(i)
                try:
                    v = int(op, 16) if op.startswith("0x") else int(op)
                    if 0 < v < 0xFFFFFFFF:
                        return v
                except (ValueError, TypeError):
                    pass
    return None

# ---------------------------------------------------------------------------
# Decompiled-source line snippet around an MMIO address
# ---------------------------------------------------------------------------
def snippet_for_address(decomp_c, addr_hex, lines_around=3):
    """
    Return up to lines_around lines before/after any line containing the
    address constant (as hex) in the decompiled C.
    """
    if not decomp_c:
        return []
    needle = addr_hex.lower()
    source_lines = decomp_c.splitlines()
    out = []
    for i, line in enumerate(source_lines):
        if needle in line.lower():
            lo = max(0, i - lines_around)
            hi = min(len(source_lines), i + lines_around + 1)
            out.extend(source_lines[lo:hi])
    # deduplicate while preserving order
    seen = set()
    unique = []
    for l in out:
        if l not in seen:
            seen.add(l)
            unique.append(l)
    return unique

# ---------------------------------------------------------------------------
# Classify read vs write from instruction mnemonic
# ---------------------------------------------------------------------------
def classify_access(mnemonic):
    m = mnemonic.upper()
    if m.startswith("MOV"):
        return "write"   # simplified; could inspect operand order
    if m in ("CMP", "TEST", "AND", "OR"):
        return "read"
    return "unknown"


def iter_operand_scalars(instr):
    """
    Ghidra 12 compatibility: InstructionDB does not expose getScalars().
    Extract Scalar operands through getOpObjects(op_index).
    """
    num_ops = instr.getNumOperands()
    for i in range(num_ops):
        try:
            objs = instr.getOpObjects(i)
        except Exception:
            objs = []
        for obj in objs:
            if isinstance(obj, Scalar):
                yield obj

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def run():
    program = currentProgram  # noqa: F821  (Ghidra global)
    monitor = ConsoleTaskMonitor()
    listing = program.getListing()
    func_manager = program.getFunctionManager()

    ifc = make_decompiler(program)

    records = {}  # addr_int -> record dict

    funcs = list(func_manager.getFunctions(True))
    total = len(funcs)
    print("[GhidraMMIOExport] Functions to scan: {}".format(total))

    for fi, func in enumerate(funcs):
        if fi % 100 == 0:
            print("  [{}/{}] {}".format(fi, total, func.getName()))

        func_name = func.getName()
        decomp_c = decompile_function(ifc, func, monitor)

        # Scan instructions in this function body
        addr_set = func.getBody()
        inst_iter = listing.getInstructions(addr_set, True)
        while inst_iter.hasNext():
            instr = inst_iter.next()
            mnem = instr.getMnemonicString()
            for scalar in iter_operand_scalars(instr):
                v = scalar.getUnsignedValue()
                if in_range(v):
                    addr_int = int(v)
                    addr_hex = "0x{:x}".format(addr_int)
                    mask = get_mask_near(listing, instr.getAddress())
                    access = classify_access(mnem)
                    snippet = snippet_for_address(decomp_c, addr_hex)

                    if addr_int not in records:
                        records[addr_int] = {
                            "address": addr_hex,
                            "accesses": [],
                        }

                    records[addr_int]["accesses"].append(
                        {
                            "function": func_name,
                            "access_type": access,
                            "mask": "0x{:x}".format(mask) if mask else None,
                            "snippet": snippet,
                        }
                    )

    # Deduplicate accesses per function+type
    output = []
    for addr_int in sorted(records.keys()):
        rec = records[addr_int]
        # Summarise: unique functions, access types, masks
        funcs_list = sorted(set(a["function"] for a in rec["accesses"]))
        types = sorted(set(a["access_type"] for a in rec["accesses"]))
        masks = sorted(set(a["mask"] for a in rec["accesses"] if a["mask"]))
        # Collect unique snippets
        seen_snips = set()
        snippets = []
        for a in rec["accesses"]:
            for s in a["snippet"]:
                if s.strip() and s not in seen_snips:
                    seen_snips.add(s)
                    snippets.append(s)

        output.append(
            {
                "address": rec["address"],
                "functions": funcs_list,
                "access_types": types,
                "masks": masks,
                "snippets": snippets[:20],  # cap to keep file size sane
            }
        )

    ifc.closeProgram()

    # Write output next to binary
    binary_path = program.getExecutablePath()
    out_path = os.path.join(os.path.dirname(binary_path), "ghidra_mmio_export.json")
    with open(out_path, "w") as f:
        json.dump({"binary": binary_path, "records": output}, f, indent=2)

    print("[GhidraMMIOExport] Done. {} addresses recorded.".format(len(output)))
    print("[GhidraMMIOExport] Output: {}".format(out_path))

run()
