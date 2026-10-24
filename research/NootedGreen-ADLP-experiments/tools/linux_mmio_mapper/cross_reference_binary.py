#!/usr/bin/env python3
"""
cross_reference_binary.py

1. Parse all _MMIO / _MMIO_PORT / _MMIO_PIPE defines from Linux i915 headers.
2. Disassemble the kext binary and collect immediate constants in known MMIO ranges.
3. Cross-reference: for each binary address find matching Linux symbols.
4. Score, assign confidence, and write a mapping JSON that validate_mappings.py accepts.

Usage:
    python3 cross_reference_binary.py \
        --binary  /path/to/AppleIntelTGLGraphicsFramebuffer \
        --headers /path/to/drm/others/drm_extracted/drivers/gpu/drm/i915 \
        --platform TGL \
        --out     /path/to/output_mapping.json
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Known MMIO address ranges to scan for in the binary (ICL/TGL relevant)
# ---------------------------------------------------------------------------
MMIO_RANGES = [
    (0x40000, 0x4ffff),   # power / misc
    (0x60000, 0x6ffff),   # DDI / AUX / PHY / combo-phy
    (0x160000, 0x16ffff), # transcoder
    (0x6b000, 0x6bfff),   # combo PHY C
]

# _MMIO(0x...) or _MMIO_PORT(..., 0x..., 0x...) etc.
# We capture the *first* hex literal after the macro name as the base address.
MMIO_DEF_RE = re.compile(
    r"^#\s*define\s+([\w]+)\s+.*?_MMIO(?:_PORT|_PIPE|_PORTC|_IDX)?\s*\("
    r"[^)]*?0x([0-9A-Fa-f]+)",
    re.MULTILINE,
)

# Also catch plain arithmetic base defines used in macros, e.g.:
#   #define _DPA_AUX_CH_CTL  0x64010
PLAIN_HEX_RE = re.compile(
    r"^#\s*define\s+([\w]+)\s+0x([0-9A-Fa-f]+)\s*(?:/\*|$)",
    re.MULTILINE,
)

# Canonical symbol rule used by validator output
VALID_SYMBOL_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")


def normalize_symbol(sym: str) -> Optional[str]:
    """
    Normalize Linux define names into validator-compatible canonical symbols.

    Examples:
      _DPA_AUX_CH_CTL -> DPA_AUX_CH_CTL
      __FOO_BAR       -> FOO_BAR
      ICL_PWR_WELL... -> unchanged
    """
    if not isinstance(sym, str):
        return None
    s = sym.strip()
    if not s:
        return None

    # Many i915 private register macros are underscore-prefixed.
    if s.startswith("_"):
        s = s.lstrip("_")

    if VALID_SYMBOL_RE.match(s):
        return s
    return None

# Immediate operand from otool -v disassembly
IMM_RE = re.compile(r"\$0x([0-9a-fA-F]{4,6})\b")


# ---------------------------------------------------------------------------
# Step 1: build address → [symbols] database from Linux headers
# ---------------------------------------------------------------------------

def parse_linux_headers(root: Path) -> dict[int, list[dict]]:
    """Return {address_int: [{symbol, path, line, macro_text}, ...]}"""
    db: dict[int, list[dict]] = {}

    headers = list(root.rglob("*.h"))
    for hpath in headers:
        try:
            text = hpath.read_text(encoding="utf-8", errors="replace")
        except Exception:
            continue

        rel = str(hpath.relative_to(root.parent.parent.parent.parent.parent)
                  if root.parent.parent.parent.parent.parent.exists()
                  else hpath)

        for pat in (MMIO_DEF_RE, PLAIN_HEX_RE):
            for m in pat.finditer(text):
                sym = m.group(1)
                norm_sym = normalize_symbol(sym)
                # Skip lowercase/non-canonical defines
                if not norm_sym:
                    continue
                try:
                    addr = int(m.group(2), 16)
                except ValueError:
                    continue

                line_no = text[: m.start()].count("\n") + 1
                entry = {
                    "symbol": norm_sym,
                    "path": str(hpath),
                    "line": line_no,
                    "macro_text": m.group(0).strip(),
                }
                db.setdefault(addr, []).append(entry)

    return db


# ---------------------------------------------------------------------------
# Step 2: extract candidate MMIO constants from binary
# ---------------------------------------------------------------------------

def in_ranges(v: int) -> bool:
    return any(lo <= v <= hi for lo, hi in MMIO_RANGES)


def extract_binary_addresses(binary: Path) -> set[int]:
    result = subprocess.run(
        ["otool", "-arch", "x86_64", "-t", "-v", str(binary)],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"ERROR: otool failed: {result.stderr.strip()}", file=sys.stderr)
        sys.exit(1)

    found: set[int] = set()
    for line in result.stdout.splitlines():
        for m in IMM_RE.finditer(line):
            v = int(m.group(1), 16)
            if in_ranges(v):
                found.add(v)
    return found


# ---------------------------------------------------------------------------
# Step 3: scoring
# ---------------------------------------------------------------------------

def score_entry(addr: int, sym_entry: dict, platform: str) -> tuple[int, list[str]]:
    """
    Returns (score, evidence_notes).
    Scoring:
      +5 exact address match (always true here)
      +4 symbol name contains platform prefix (e.g. ICL_, TGL_)
      +2 symbol name contains functional keyword (AUX, DDI, PWR, PHY, TRANS, BUF)
      +1 path contains 'display' (display subsystem header)
      -3 symbol looks like a plain offset/index (all digits suffix)
    """
    score = 5  # exact address match by definition
    notes = ["exact_address_match"]

    sym = sym_entry["symbol"]
    path = sym_entry["path"].lower()

    plat_upper = platform.upper()
    platform_matched = False
    if sym.startswith(plat_upper + "_"):
        score += 4
        notes.append(f"platform_prefix_{plat_upper}")
        platform_matched = True
    elif any(sym.startswith(p) for p in ("ICL_", "TGL_", "ADL_", "GEN11_", "GEN12_")):
        score += 3
        notes.append("related_gen_prefix")
        platform_matched = True
    notes.append(f"platform_match:{platform_matched}")

    func_keywords = ("AUX", "DDI", "PWR", "PHY", "TRANS", "BUF", "COMBO", "DPLL",
                     "PORT", "POWER", "WELL", "CTL", "STAT")
    matched_kw = [kw for kw in func_keywords if kw in sym]
    if matched_kw:
        score += 2
        notes.append("functional_keywords:" + ",".join(matched_kw))

    if "display" in path:
        score += 1
        notes.append("display_header")

    # penalise ambiguous plain-offset defines like _DPA_AUX_CH_CTL vs DDI_AUX_CH_CTL
    if re.search(r"_\d+$", sym):
        score -= 1
        notes.append("numeric_suffix_penalty")

    return score, notes


def confidence_from_score(score: int) -> str:
    if score >= 10:
        return "HIGH"
    if score >= 7:
        return "MEDIUM"
    return "LOW"


def status_from_confidence(
    conf: str,
    ambiguous: bool,
    exact_address_match: bool,
    platform_match: bool,
) -> str:
    if conf == "HIGH" and not ambiguous and exact_address_match and platform_match:
        return "AUTO_RENAME"
    if conf == "LOW":
        return "REJECTED"
    return "REVIEW_REQUIRED"


# ---------------------------------------------------------------------------
# Step 4: build mapping JSON
# ---------------------------------------------------------------------------

def build_mapping(
    binary_addrs: set[int],
    linux_db: dict[int, list[dict]],
    platform: str,
    ghidra_db: dict = {},
) -> dict:
    results = []

    FUNC_KW = ("aux", "ddi", "pwr", "power", "phy", "trans", "buf",
               "well", "ctl", "stat", "hotplug", "dpll", "combo")

    for addr in sorted(binary_addrs):
        addr_str = f"0x{addr:x}"
        candidates = linux_db.get(addr, [])
        ghidra_rec = ghidra_db.get(addr)

        ghidra_usage_match = False
        ghidra_bitfield_match = False

        if ghidra_rec:
            ghidra_functions = ghidra_rec.get("functions", [])
            ghidra_masks = ghidra_rec.get("masks", [])
            ghidra_snippets = ghidra_rec.get("snippets", [])
            snippet_text = " ".join(ghidra_snippets + ghidra_functions).lower()
            ghidra_usage_match = any(kw in snippet_text for kw in FUNC_KW)
            ghidra_bitfield_match = len(ghidra_masks) > 0

        if not candidates:
            # Heuristic recovery for unresolved addresses (never exact-match):
            # 1) byte/word access aliases around dword regs (addr -> addr&~0x3)
            # 2) common DDI/AUX port stride (+0x100)
            recovery_pool = []

            # (1) unaligned alias to dword base
            base4 = addr & ~0x3
            if base4 != addr and base4 in linux_db:
                for c in linux_db[base4]:
                    s, notes = score_entry(base4, c, platform)
                    s -= 3
                    notes.append(f"heuristic_unaligned_alias:+0x{addr-base4:x}")
                    recovery_pool.append((s, notes, c, base4))

            # (2) stride alias in display port/AUX range
            if 0x64000 <= addr <= 0x66FFF:
                for step in (0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700):
                    base = addr - step
                    if base in linux_db:
                        for c in linux_db[base]:
                            s, notes = score_entry(base, c, platform)
                            s -= 2
                            notes.append(f"heuristic_port_stride:+0x{step:x}")
                            recovery_pool.append((s, notes, c, base))

            if recovery_pool:
                recovery_pool.sort(key=lambda x: -x[0])
                best_score, best_notes, best, base_addr = recovery_pool[0]
                conf = confidence_from_score(best_score)
                platform_match_flag = any("platform_match:True" in n for n in best_notes)

                # Heuristic mappings are always non-exact and stay in review/reject lanes.
                status = status_from_confidence(
                    conf,
                    False,
                    False,
                    platform_match_flag,
                )

                path_str = best["path"]
                try:
                    rel_path = str(Path(path_str).relative_to(
                        Path.home() / "Downloads" / "drm" / "others" / "drm_extracted"
                    ))
                except ValueError:
                    rel_path = path_str

                heuristic_note = "; ".join(n for n in best_notes if n.startswith("heuristic_"))

                results.append(
                    {
                        "address": addr_str,
                        "platform": platform,
                        "canonical_linux_symbol": best["symbol"],
                        "canonical_source": {
                            "repo": "torvalds/linux",
                            "path": rel_path,
                            "line": best["line"],
                            "macro_text": best["macro_text"],
                        },
                        "apple_aliases": [],
                        "confidence": conf,
                        "evidence": {
                            "exact_address_match": False,
                            "platform_match": platform_match_flag,
                            "usage_match": ghidra_usage_match or any("functional_keywords" in n for n in best_notes),
                            "bitfield_match": ghidra_bitfield_match,
                            "ambiguity_notes": f"heuristic alias via 0x{base_addr:x}; {heuristic_note}",
                            "score": best_score,
                        },
                        "status": status,
                    }
                )
                continue

            results.append(
                {
                    "address": addr_str,
                    "platform": platform,
                    "canonical_linux_symbol": f"UNKNOWN_{addr_str}",
                    "canonical_source": {
                        "repo": "torvalds/linux",
                        "path": "",
                        "line": 1,
                        "macro_text": "",
                    },
                    "apple_aliases": [],
                    "confidence": "LOW",
                    "evidence": {
                        "exact_address_match": False,
                        "platform_match": False,
                        "usage_match": ghidra_usage_match,
                        "bitfield_match": ghidra_bitfield_match,
                        "ambiguity_notes": "no linux symbol found",
                        "score": 0,
                    },
                    "status": "REJECTED",
                }
            )
            continue

        scored = []
        for c in candidates:
            s, notes = score_entry(addr, c, platform)
            if ghidra_usage_match:
                s += 3
                notes.append("ghidra_usage_match")
            if ghidra_bitfield_match:
                s += 2
                notes.append("ghidra_bitfield_match")
            scored.append((s, notes, c))

        scored.sort(key=lambda x: -x[0])
        best_score, best_notes, best = scored[0]

        ambiguous = sum(1 for s, _, _ in scored if s >= best_score - 2) > 1
        ambiguity_notes = ""
        if ambiguous:
            alts = [c["symbol"] for _, _, c in scored[1:4]]
            ambiguity_notes = "alternatives: " + ", ".join(alts)

        conf = confidence_from_score(best_score)
        platform_match_flag = any("platform_match:True" in n for n in best_notes)
        exact_match_flag = True
        status = status_from_confidence(
            conf,
            ambiguous,
            exact_match_flag,
            platform_match_flag,
        )

        path_str = best["path"]
        try:
            rel_path = str(Path(path_str).relative_to(
                Path.home() / "Downloads" / "drm" / "others" / "drm_extracted"
            ))
        except ValueError:
            rel_path = path_str

        results.append(
            {
                "address": addr_str,
                "platform": platform,
                "canonical_linux_symbol": best["symbol"],
                "canonical_source": {
                    "repo": "torvalds/linux",
                    "path": rel_path,
                    "line": best["line"],
                    "macro_text": best["macro_text"],
                },
                "apple_aliases": [],
                "confidence": conf,
                "evidence": {
                    "exact_address_match": True,
                    "platform_match": platform_match_flag,
                    "usage_match": ghidra_usage_match or any("functional_keywords" in n for n in best_notes),
                    "bitfield_match": ghidra_bitfield_match,
                    "ambiguity_notes": ambiguity_notes,
                    "score": best_score,
                },
                "status": status,
            }
        )

    return {"platform": platform, "results": results}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description="Cross-reference kext binary MMIO addresses with Linux i915 symbols")
    parser.add_argument("--binary", required=True, help="Path to kext Mach-O binary")
    parser.add_argument("--headers", required=True, help="Root of i915 headers tree")
    parser.add_argument("--platform", default="TGL", help="Platform prefix (default: TGL)")
    parser.add_argument("--ghidra", default=None, help="Path to ghidra_mmio_export.json (optional)")
    parser.add_argument("--out", default="raw_mapping.json", help="Output JSON path")
    args = parser.parse_args()

    binary = Path(args.binary)
    headers = Path(args.headers)

    if not binary.exists():
        print(f"ERROR: binary not found: {binary}", file=sys.stderr)
        return 1
    if not headers.exists():
        print(f"ERROR: headers root not found: {headers}", file=sys.stderr)
        return 1

    # Load optional Ghidra export
    ghidra_db: dict[int, dict] = {}
    if args.ghidra:
        ghidra_path = Path(args.ghidra)
        if not ghidra_path.exists():
            print(f"ERROR: ghidra export not found: {ghidra_path}", file=sys.stderr)
            return 1
        raw = json.loads(ghidra_path.read_text(encoding="utf-8"))
        for rec in raw.get("records", []):
            try:
                addr = int(rec["address"], 16)
                ghidra_db[addr] = rec
            except (ValueError, KeyError):
                pass
        print(f"      Ghidra export loaded: {len(ghidra_db)} addresses")

    print(f"[1/3] Parsing Linux i915 headers from {headers} ...")
    linux_db = parse_linux_headers(headers)
    print(f"      {len(linux_db)} unique addresses indexed")

    print(f"[2/3] Extracting MMIO candidates from binary {binary.name} ...")
    binary_addrs = extract_binary_addresses(binary)
    print(f"      {len(binary_addrs)} candidate addresses found")

    print(f"[3/3] Cross-referencing against platform={args.platform} ...")
    mapping = build_mapping(binary_addrs, linux_db, args.platform, ghidra_db)

    matched = sum(1 for r in mapping["results"] if not r["canonical_linux_symbol"].startswith("UNKNOWN"))
    auto = sum(1 for r in mapping["results"] if r["status"] == "AUTO_RENAME")
    review = sum(1 for r in mapping["results"] if r["status"] == "REVIEW_REQUIRED")
    rejected = sum(1 for r in mapping["results"] if r["status"] == "REJECTED")

    Path(args.out).write_text(json.dumps(mapping, indent=2) + "\n", encoding="utf-8")

    print(f"\nResults: {len(mapping['results'])} total")
    print(f"  Linux symbol found : {matched}")
    print(f"  AUTO_RENAME        : {auto}")
    print(f"  REVIEW_REQUIRED    : {review}")
    print(f"  REJECTED/UNKNOWN   : {rejected}")
    print(f"\nWrote: {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
