#!/usr/bin/env python3
"""
batch_map_kexts.py

Run the existing mapping pipeline across multiple AppleIntel*.kext binaries and
produce separate output artifacts per binary.

For each discovered binary, this script runs:
  1) cross_reference_binary.py
  2) validate_mappings.py
  3) generate_header_from_approved.py

Discovery rule:
  <root>/**/AppleIntel*.kext/Contents/MacOS/AppleIntel*
with optional requirement that ghidra_mmio_export.json exists in the same
directory as the binary.
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional


def slugify(text: str) -> str:
    return re.sub(r"[^a-zA-Z0-9._-]+", "_", text).strip("_")


def find_binaries(search_root: Path, require_ghidra: bool) -> List[Dict[str, Optional[Path]]]:
    results = []
    for kext in sorted(search_root.rglob("AppleIntel*.kext")):
        macos_dir = kext / "Contents" / "MacOS"
        if not macos_dir.exists() or not macos_dir.is_dir():
            continue

        for bin_path in sorted(macos_dir.iterdir()):
            if not bin_path.is_file():
                continue
            if not bin_path.name.startswith("AppleIntel"):
                continue
            if bin_path.suffix:
                continue

            ghidra = macos_dir / "ghidra_mmio_export.json"
            if require_ghidra and not ghidra.exists():
                continue

            results.append(
                {
                    "binary": bin_path,
                    "ghidra": ghidra if ghidra.exists() else None,
                    "kext": kext,
                }
            )

    return results


def run_cmd(cmd: List[str]) -> None:
    proc = subprocess.run(cmd)
    if proc.returncode != 0:
        raise RuntimeError("command failed: " + " ".join(cmd))


def stats_from_raw(raw_json: Path) -> Dict[str, int]:
    doc = json.loads(raw_json.read_text(encoding="utf-8"))
    res = doc["results"]
    unknown = sum(1 for r in res if str(r.get("canonical_linux_symbol", "")).startswith("UNKNOWN_"))
    return {
        "total": len(res),
        "known": len(res) - unknown,
        "unknown": unknown,
        "auto": sum(1 for r in res if r.get("status") == "AUTO_RENAME"),
        "review": sum(1 for r in res if r.get("status") == "REVIEW_REQUIRED"),
        "rejected": sum(1 for r in res if r.get("status") == "REJECTED"),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Batch run MMIO mapper pipeline for many kext binaries")
    ap.add_argument("--search-root", required=True, help="Root to scan for AppleIntel*.kext")
    ap.add_argument("--headers", required=True, help="Linux headers root for cross-reference")
    ap.add_argument("--platform", default="TGL", help="Platform prefix (default: TGL)")
    ap.add_argument("--out-dir", required=True, help="Directory to write per-binary outputs")
    ap.add_argument("--require-ghidra", action="store_true", help="Skip binaries without ghidra_mmio_export.json")
    ap.add_argument("--name-filter", default="", help="Only process binaries whose name contains this text")
    ap.add_argument("--exact-binary-name", default="", help="Only process binaries with this exact filename")
    args = ap.parse_args()

    search_root = Path(args.search_root)
    headers = Path(args.headers)
    out_dir = Path(args.out_dir)

    if not search_root.exists():
        print(f"ERROR: search root not found: {search_root}", file=sys.stderr)
        return 2
    if not headers.exists():
        print(f"ERROR: headers root not found: {headers}", file=sys.stderr)
        return 2

    out_dir.mkdir(parents=True, exist_ok=True)

    this_dir = Path(__file__).resolve().parent
    cross_ref = this_dir / "cross_reference_binary.py"
    validate = this_dir / "validate_mappings.py"
    gen_header = this_dir / "generate_header_from_approved.py"

    for req in (cross_ref, validate, gen_header):
        if not req.exists():
            print(f"ERROR: required script not found: {req}", file=sys.stderr)
            return 2

    bins = find_binaries(search_root, args.require_ghidra)
    if args.name_filter:
        needle = args.name_filter.lower()
        bins = [b for b in bins if needle in str(b["binary"].name).lower()]
    if args.exact_binary_name:
        bins = [b for b in bins if b["binary"].name == args.exact_binary_name]

    if not bins:
        print("No matching binaries found.")
        return 0

    summary = []
    print(f"Discovered {len(bins)} binaries")

    for item in bins:
        binary = item["binary"]
        ghidra = item["ghidra"]

        rel_parent = binary.parent.parent.parent.parent.relative_to(search_root)
        run_tag = slugify(str(rel_parent) + "__" + binary.name)

        raw = out_dir / f"{run_tag}_raw_mapping.json"
        approved = out_dir / f"{run_tag}_approved_renames.json"
        header = out_dir / f"{run_tag}_linux_regs.h"

        print(f"\n=== {binary} ===")
        cmd = [
            "python3",
            str(cross_ref),
            "--binary",
            str(binary),
            "--headers",
            str(headers),
            "--platform",
            args.platform,
            "--out",
            str(raw),
        ]
        if ghidra:
            cmd.extend(["--ghidra", str(ghidra)])
        run_cmd(cmd)

        run_cmd([
            "python3",
            str(validate),
            str(raw),
            "--approved-out",
            str(approved),
        ])

        run_cmd([
            "python3",
            str(gen_header),
            str(approved),
            "--out",
            str(header),
        ])

        st = stats_from_raw(raw)
        summary.append(
            {
                "binary": str(binary),
                "ghidra": str(ghidra) if ghidra else "",
                "raw": str(raw),
                "approved": str(approved),
                "header": str(header),
                **st,
            }
        )
        print(
            "stats: "
            f"total={st['total']} known={st['known']} unknown={st['unknown']} "
            f"auto={st['auto']} review={st['review']} rejected={st['rejected']}"
        )

    summary_path = out_dir / "batch_summary.json"
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"\nWrote summary: {summary_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
