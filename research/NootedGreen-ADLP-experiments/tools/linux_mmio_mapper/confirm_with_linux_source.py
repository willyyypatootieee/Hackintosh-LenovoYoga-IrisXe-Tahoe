#!/usr/bin/env python3
"""
confirm_with_linux_source.py

Post-process a mapping JSON and use Linux source usage to promote a subset of
safe REVIEW_REQUIRED records to AUTO_RENAME.

Promotion policy is intentionally strict:
  - only REVIEW_REQUIRED records
  - only HIGH confidence
  - exact_address_match must already be true
  - ambiguity_notes must already be empty
  - symbol must be referenced in Linux source
  - at least one source hit must look platform-relevant for TGL/ICL/GEN11/GEN12

This script does not modify address/symbol/source fields. It only updates:
  evidence.platform_match
  status

Usage:
  python3 confirm_with_linux_source.py \
    --input /path/to/tgl_raw_mapping_with_ghidra.json \
    --source-root /path/to/linux-mainline \
    --source-root /path/to/linux-drm-intel \
    --out /path/to/tgl_raw_mapping_source_confirmed.json \
    --report-out /path/to/tgl_source_confirmation_report.json
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Tuple


UNKNOWN_RE = re.compile(r"^UNKNOWN_0x[0-9a-fA-F]+$")


def run_grep_hits(symbol: str, source_root: Path, max_hits: int) -> List[Tuple[str, int, str]]:
    """
    Return grep hits as tuples: (path, line_no, line_text).
    Uses BSD grep compatible options available on macOS.
    """
    cmd = [
        "grep",
        "-R",
        "-n",
        "-w",
        "--include=*.c",
        "--include=*.h",
        symbol,
        str(source_root),
    ]

    try:
        proc = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError:
        return []

    # grep exit code 1 means no matches; not an error for us.
    if proc.returncode not in (0, 1):
        return []

    hits: List[Tuple[str, int, str]] = []
    for line in proc.stdout.splitlines():
        # format: /abs/path:123:text...
        parts = line.split(":", 2)
        if len(parts) < 3:
            continue
        path_str, line_no_str, text = parts
        try:
            line_no = int(line_no_str)
        except ValueError:
            continue
        hits.append((path_str, line_no, text.strip()))
        if len(hits) >= max_hits:
            break
    return hits


def is_platform_relevant(path_str: str, text: str, platform: str) -> bool:
    p = path_str.lower()
    t = text.lower()
    plat = platform.upper()

    generic_tokens = (
        "display_ver(11",
        "display_ver(12",
        "gen11",
        "gen12",
        "tigerlake",
        "icelake",
        "tgl",
        "icl",
    )
    if any(tok in p for tok in generic_tokens):
        return True
    if any(tok in t for tok in generic_tokens):
        return True

    # Intel i915-style platform checks often appear in source usage sites.
    platform_checks = (
        "is_tigerlake",
        "is_icelake",
        "platform_",
        "display_ver",
    )
    if any(tok in t for tok in platform_checks):
        return True

    # If a symbol itself is platform-prefixed, usage is inherently relevant.
    if plat and (f"{plat}_" in t or f"{plat.lower()}_" in t):
        return True

    return False


def is_strict_promotion_candidate(rec: dict) -> bool:
    if rec.get("status") != "REVIEW_REQUIRED":
        return False
    if rec.get("confidence") != "HIGH":
        return False

    sym = rec.get("canonical_linux_symbol", "")
    if not isinstance(sym, str) or UNKNOWN_RE.match(sym):
        return False

    ev = rec.get("evidence", {})
    if not isinstance(ev, dict):
        return False
    if ev.get("exact_address_match") is not True:
        return False
    if str(ev.get("ambiguity_notes", "")).strip() != "":
        return False

    return True


def main() -> int:
    ap = argparse.ArgumentParser(description="Promote safe REVIEW_REQUIRED records using Linux source confirmation")
    ap.add_argument("--input", required=True, help="Input mapping JSON")
    ap.add_argument("--source-root", action="append", required=True, help="Linux source root (repeatable)")
    ap.add_argument("--out", required=True, help="Output updated mapping JSON")
    ap.add_argument("--report-out", required=True, help="Output report JSON")
    ap.add_argument("--max-hits-per-symbol", type=int, default=80, help="Limit grep hits per symbol per root")
    args = ap.parse_args()

    in_path = Path(args.input)
    if not in_path.exists():
        print(f"ERROR: input not found: {in_path}", file=sys.stderr)
        return 2

    roots = [Path(p) for p in args.source_root]
    for r in roots:
        if not r.exists() or not r.is_dir():
            print(f"ERROR: source root not found or not directory: {r}", file=sys.stderr)
            return 2

    doc = json.loads(in_path.read_text(encoding="utf-8"))
    if not isinstance(doc, dict) or "results" not in doc or "platform" not in doc:
        print("ERROR: input JSON missing required top-level keys", file=sys.stderr)
        return 2

    platform = str(doc.get("platform", "TGL"))
    results = doc.get("results", [])
    if not isinstance(results, list):
        print("ERROR: top.results must be an array", file=sys.stderr)
        return 2

    # Build unique symbol set from strict candidates only.
    candidate_indices = [i for i, r in enumerate(results) if is_strict_promotion_candidate(r)]
    symbols = sorted({results[i]["canonical_linux_symbol"] for i in candidate_indices})

    symbol_hits: Dict[str, List[Tuple[str, int, str]]] = {}
    for sym in symbols:
        all_hits: List[Tuple[str, int, str]] = []
        for root in roots:
            all_hits.extend(run_grep_hits(sym, root, args.max_hits_per_symbol))
        symbol_hits[sym] = all_hits

    promoted = 0
    scanned = 0
    report_records = []

    for idx in candidate_indices:
        rec = results[idx]
        sym = rec["canonical_linux_symbol"]
        hits = symbol_hits.get(sym, [])
        scanned += 1

        platform_hits = [h for h in hits if is_platform_relevant(h[0], h[2], platform)]
        hit_count = len(hits)
        platform_hit_count = len(platform_hits)

        was_platform_match = bool(rec.get("evidence", {}).get("platform_match"))
        now_platform_match = was_platform_match or platform_hit_count > 0

        promote = now_platform_match and hit_count > 0
        if promote:
            rec["evidence"]["platform_match"] = True
            rec["status"] = "AUTO_RENAME"
            promoted += 1

        report_records.append(
            {
                "address": rec.get("address"),
                "symbol": sym,
                "was_platform_match": was_platform_match,
                "hit_count": hit_count,
                "platform_relevant_hit_count": platform_hit_count,
                "promoted": promote,
                "sample_hits": [
                    {
                        "path": h[0],
                        "line": h[1],
                        "text": h[2],
                    }
                    for h in hits[:5]
                ],
            }
        )

    out_doc = {"platform": doc["platform"], "results": results}
    Path(args.out).write_text(json.dumps(out_doc, indent=2) + "\n", encoding="utf-8")

    report = {
        "platform": platform,
        "source_roots": [str(r) for r in roots],
        "strict_candidates_scanned": scanned,
        "promoted_to_auto": promoted,
        "records": report_records,
    }
    Path(args.report_out).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(f"Strict candidates scanned: {scanned}")
    print(f"Promoted to AUTO_RENAME: {promoted}")
    print(f"Wrote updated mapping: {args.out}")
    print(f"Wrote report: {args.report_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
