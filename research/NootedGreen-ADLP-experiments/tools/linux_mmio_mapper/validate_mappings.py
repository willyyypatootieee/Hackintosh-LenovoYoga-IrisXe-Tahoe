#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path

ADDRESS_RE = re.compile(r"^0x[0-9a-fA-F]+$")
LINUX_SYMBOL_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
UNKNOWN_RE = re.compile(r"^UNKNOWN_0x[0-9a-fA-F]+$")

REQUIRED_TOP = {"platform", "results"}
REQUIRED_RESULT = {
    "address",
    "platform",
    "canonical_linux_symbol",
    "canonical_source",
    "apple_aliases",
    "confidence",
    "evidence",
    "status",
}
REQUIRED_SOURCE = {"repo", "path", "line", "macro_text"}
REQUIRED_EVIDENCE = {
    "exact_address_match",
    "platform_match",
    "usage_match",
    "bitfield_match",
    "ambiguity_notes",
    "score",
}


def expected_confidence(score: int) -> str:
    if score >= 10:
        return "HIGH"
    if score >= 7:
        return "MEDIUM"
    return "LOW"


def is_auto_rename_eligible(r: dict) -> bool:
    ev = r["evidence"]
    no_ambiguity = str(ev["ambiguity_notes"]).strip() == ""
    return (
        ev["exact_address_match"] is True
        and ev["platform_match"] is True
        and r["confidence"] == "HIGH"
        and r["status"] == "AUTO_RENAME"
        and no_ambiguity
    )


def check_required_keys(obj: dict, required: set, ctx: str, errors: list) -> None:
    missing = sorted(required - set(obj.keys()))
    extra = sorted(set(obj.keys()) - required)
    if missing:
        errors.append(f"{ctx}: missing keys: {', '.join(missing)}")
    if extra:
        errors.append(f"{ctx}: unexpected keys: {', '.join(extra)}")


def validate_result(r: dict, idx: int, top_platform: str, errors: list, warnings: list) -> None:
    ctx = f"results[{idx}]"
    check_required_keys(r, REQUIRED_RESULT, ctx, errors)
    if any(k not in r for k in REQUIRED_RESULT):
        return

    addr = r["address"]
    if not isinstance(addr, str) or not ADDRESS_RE.match(addr):
        errors.append(f"{ctx}.address: invalid hex format")

    if r["platform"] != top_platform:
        warnings.append(f"{ctx}.platform differs from top-level platform")

    sym = r["canonical_linux_symbol"]
    aliases = r["apple_aliases"]
    if not isinstance(aliases, list) or any(not isinstance(a, str) for a in aliases):
        errors.append(f"{ctx}.apple_aliases: must be array of strings")

    if not isinstance(sym, str) or not sym:
        errors.append(f"{ctx}.canonical_linux_symbol: must be non-empty string")
    else:
        is_unknown = bool(UNKNOWN_RE.match(sym))
        is_linux_style = bool(LINUX_SYMBOL_RE.match(sym))
        if not (is_unknown or is_linux_style):
            errors.append(f"{ctx}.canonical_linux_symbol: must be Linux style or UNKNOWN_0x...")
        if isinstance(aliases, list) and sym in aliases:
            errors.append(f"{ctx}.canonical_linux_symbol duplicates Apple alias")

    src = r["canonical_source"]
    if not isinstance(src, dict):
        errors.append(f"{ctx}.canonical_source: must be object")
    else:
        check_required_keys(src, REQUIRED_SOURCE, f"{ctx}.canonical_source", errors)
        if all(k in src for k in REQUIRED_SOURCE):
            if not isinstance(src["line"], int) or src["line"] < 1:
                errors.append(f"{ctx}.canonical_source.line: must be >= 1")

    ev = r["evidence"]
    if not isinstance(ev, dict):
        errors.append(f"{ctx}.evidence: must be object")
        return
    check_required_keys(ev, REQUIRED_EVIDENCE, f"{ctx}.evidence", errors)
    if any(k not in ev for k in REQUIRED_EVIDENCE):
        return

    for flag in ["exact_address_match", "platform_match", "usage_match", "bitfield_match"]:
        if not isinstance(ev[flag], bool):
            errors.append(f"{ctx}.evidence.{flag}: must be boolean")

    if not isinstance(ev["ambiguity_notes"], str):
        errors.append(f"{ctx}.evidence.ambiguity_notes: must be string")

    score = ev["score"]
    if not isinstance(score, int) or score < -20 or score > 20:
        errors.append(f"{ctx}.evidence.score: must be integer in [-20, 20]")
        return

    conf = r["confidence"]
    if conf not in {"HIGH", "MEDIUM", "LOW"}:
        errors.append(f"{ctx}.confidence: invalid value")
    else:
        exp = expected_confidence(score)
        if conf != exp:
            errors.append(f"{ctx}.confidence={conf} does not match score={score} (expected {exp})")

    status = r["status"]
    if status not in {"AUTO_RENAME", "REVIEW_REQUIRED", "REJECTED"}:
        errors.append(f"{ctx}.status: invalid value")
    else:
        is_unknown = bool(UNKNOWN_RE.match(sym)) if isinstance(sym, str) else False
        ambiguous = str(ev["ambiguity_notes"]).strip() != ""

        if status == "AUTO_RENAME":
            if is_unknown:
                errors.append(f"{ctx}: UNKNOWN symbol cannot be AUTO_RENAME")
            if not is_auto_rename_eligible(r):
                errors.append(f"{ctx}: AUTO_RENAME requires exact+platform match, HIGH, and no ambiguity")

        if status == "REJECTED" and conf != "LOW":
            warnings.append(f"{ctx}: REJECTED with non-LOW confidence")

        if status == "REVIEW_REQUIRED" and conf == "HIGH" and not ambiguous:
            warnings.append(f"{ctx}: HIGH without ambiguity is usually AUTO_RENAME")


def validate_document(doc: dict) -> tuple:
    errors = []
    warnings = []

    if not isinstance(doc, dict):
        return ["Top-level document must be object"], warnings

    check_required_keys(doc, REQUIRED_TOP, "top", errors)
    if any(k not in doc for k in REQUIRED_TOP):
        return errors, warnings

    platform = doc["platform"]
    results = doc["results"]

    if not isinstance(platform, str) or not platform.strip():
        errors.append("top.platform must be non-empty string")

    if not isinstance(results, list):
        errors.append("top.results must be array")
        return errors, warnings

    for i, r in enumerate(results):
        if not isinstance(r, dict):
            errors.append(f"results[{i}] must be object")
            continue
        validate_result(r, i, platform, errors, warnings)

    return errors, warnings


def extract_auto_renames(doc: dict) -> list:
    output = []
    for r in doc["results"]:
        if is_auto_rename_eligible(r):
            output.append(
                {
                    "address": r["address"],
                    "platform": r["platform"],
                    "canonical_linux_symbol": r["canonical_linux_symbol"],
                    "apple_aliases": r["apple_aliases"],
                    "source": r["canonical_source"],
                }
            )
    return output


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate MMIO mapping JSON and emit safe AUTO_RENAME subset."
    )
    parser.add_argument("input", help="Path to mapping JSON")
    parser.add_argument(
        "--approved-out",
        default="approved_auto_renames.json",
        help="Output file for safe auto-rename records",
    )
    args = parser.parse_args()

    in_path = Path(args.input)
    if not in_path.exists():
        print(f"ERROR: input file does not exist: {in_path}")
        return 2

    try:
        doc = json.loads(in_path.read_text(encoding="utf-8"))
    except Exception as exc:
        print(f"ERROR: failed to parse JSON: {exc}")
        return 2

    errors, warnings = validate_document(doc)

    if warnings:
        for w in warnings:
            print(f"WARN: {w}")

    if errors:
        for e in errors:
            print(f"ERROR: {e}")
        print(f"\nFAILED: {len(errors)} error(s), {len(warnings)} warning(s)")
        return 1

    approved = extract_auto_renames(doc)
    out = {
        "platform": doc["platform"],
        "approved_count": len(approved),
        "approved": approved,
    }
    Path(args.approved_out).write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")

    print(f"PASS: {len(doc['results'])} records validated, {len(warnings)} warning(s)")
    print(f"Wrote AUTO_RENAME subset to: {args.approved_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
