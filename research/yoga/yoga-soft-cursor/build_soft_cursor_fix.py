#!/usr/bin/env python3
"""Build SoftCursorFix.kext natively on macOS without Xcode."""
import argparse
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

SRC_DIR = Path(__file__).resolve().parent
PRODUCT = "SoftCursorFix"
VERSION = "0.1.0"
DEFAULT_BUNDLE_ID = "local.yoga.driver.SoftCursorFix"


def run(cmd):
    cmd = [str(c) for c in cmd]
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)


def resolve_lilu_resources(path: Path) -> Path:
    """Accept either a Lilu.kext bundle or its Contents/Resources directory."""
    if (path / "Contents/Resources/Library/plugin_start.cpp").is_file():
        return path / "Contents/Resources"
    if (path / "Library/plugin_start.cpp").is_file():
        return path
    sys.exit(
        f"ERROR: Could not find Library/plugin_start.cpp under {path}.\n"
        "Pass the path to a debug/SDK Lilu.kext bundle or its Contents/Resources directory."
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build SoftCursorFix.kext natively on macOS using clang/ld."
    )
    parser.add_argument(
        "--sdk",
        type=Path,
        default=Path(os.environ["MAC_KERNEL_SDK"]) if "MAC_KERNEL_SDK" in os.environ else None,
        help="Path to MacKernelSDK (or set MAC_KERNEL_SDK).",
    )
    parser.add_argument(
        "--lilu",
        type=Path,
        default=(
            Path(os.environ["LILU_KEXT"])
            if "LILU_KEXT" in os.environ
            else (Path(os.environ["LILU_RESOURCES"]) if "LILU_RESOURCES" in os.environ else None)
        ),
        help="Path to Lilu.kext (with SDK Headers/Library) or Lilu.kext/Contents/Resources (or set LILU_KEXT / LILU_RESOURCES).",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path(os.environ.get("OUT_DIR", SRC_DIR / "build")),
        help="Output directory for SoftCursorFix.kext (or set OUT_DIR; default: ./build).",
    )
    parser.add_argument(
        "--bundle-id",
        default=DEFAULT_BUNDLE_ID,
        help=f"CFBundleIdentifier for the built kext (default: {DEFAULT_BUNDLE_ID}).",
    )
    args = parser.parse_args()
    if args.sdk is None:
        parser.error("Missing MacKernelSDK path: pass --sdk or set MAC_KERNEL_SDK.")
    if args.lilu is None:
        parser.error("Missing Lilu path: pass --lilu or set LILU_KEXT / LILU_RESOURCES.")
    return args


def main():
    args = parse_args()
    sdk = args.sdk.expanduser().resolve()
    lilu = resolve_lilu_resources(args.lilu.expanduser().resolve())
    out_root = args.out.expanduser().resolve()
    bundle_id = args.bundle_id

    libkmod = sdk / "Library/x86_64/libkmod.a"
    if not (sdk / "Headers").is_dir() or not libkmod.is_file():
        sys.exit(f"ERROR: Invalid MacKernelSDK at {sdk} (expected Headers/ and Library/x86_64/libkmod.a).")

    stage = out_root / "stage-swcursor"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    kext = out_root / f"{PRODUCT}.kext"
    if kext.exists():
        shutil.rmtree(kext)
    contents = kext / "Contents"
    (contents / "MacOS").mkdir(parents=True)

    info = dict(
        CFBundleDevelopmentRegion="en",
        CFBundleExecutable=PRODUCT,
        CFBundleIdentifier=bundle_id,
        CFBundleInfoDictionaryVersion="6.0",
        CFBundleName=PRODUCT,
        CFBundlePackageType="KEXT",
        CFBundleShortVersionString=VERSION,
        CFBundleVersion=VERSION,
        OSBundleRequired="Root",
        OSBundleLibraries={
            "as.vit9696.Lilu": "1.7.0",
            "com.StezzaPilot.NootedGreen": "1.0.0",
            **{
                f"com.apple.kpi.{k}": "12.0.0"
                for k in ("bsd", "dsep", "iokit", "libkern", "mach", "unsupported")
            },
        },
        IOKitPersonalities={
            PRODUCT: dict(
                CFBundleIdentifier=bundle_id,
                IOClass=PRODUCT,
                IOMatchCategory=PRODUCT,
                IOProviderClass="IOResources",
                IOResourceMatch="IOKit",
                IOProbeScore=1000,
            )
        },
    )
    (contents / "Info.plist").write_bytes(plistlib.dumps(info))

    info_c = stage / "info.c"
    info_c.write_text(
        f"""#include <mach/mach_types.h>
extern kern_return_t {PRODUCT}_kern_start(kmod_info_t *, void *);
extern kern_return_t {PRODUCT}_kern_stop(kmod_info_t *, void *);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL({bundle_id}, "{VERSION}", {PRODUCT}_kern_start, {PRODUCT}_kern_stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
"""
    )

    flags = [
        "-target", "x86_64-apple-macos13",
        "-mkernel", "-nostdinc", "-fno-builtin", "-fno-common",
        "-fno-stack-protector", "-mmmx", "-msse", "-msse2",
        "-DKERNEL", "-DKERNEL_PRIVATE", "-DDRIVER_PRIVATE",
        "-DAPPLE", "-DNeXT", "-DDEBUG=1", "-DAPPLE_KEXT_ASSERTIONS=1",
        f"-DPRODUCT_NAME={PRODUCT}", f"-DMODULE_VERSION={VERSION}",
        "-D__null_terminated=", "-D__stateful_pure=",
        "-isystem", sdk / "Headers",
        f"-I{lilu}",
        "-O2",
    ]
    cppflags = [
        "-std=gnu++17", "-nostdinc++", "-fno-exceptions", "-fno-rtti",
        "-fapple-kext", "-mllvm", "-disable-atexit-based-global-dtor-lowering",
        "-Wno-deprecated-declarations", "-Wno-inconsistent-missing-override",
        "-Wno-unused-parameter",
    ]

    run(["clang", *flags, "-c", info_c, "-o", stage / "info.o"])
    run(["clang++", *flags, *cppflags, "-c", SRC_DIR / f"{PRODUCT}.cpp", "-o", stage / f"{PRODUCT}.o"])
    run(["clang++", *flags, *cppflags, "-c", lilu / "Library/plugin_start.cpp", "-o", stage / "plugin_start.o"])

    binary = contents / "MacOS" / PRODUCT
    run([
        "ld", "-arch", "x86_64", "-kext", "-export_dynamic",
        "-macosx_version_min", "13.0",
        stage / "info.o",
        stage / f"{PRODUCT}.o",
        stage / "plugin_start.o",
        libkmod,
        "-o", binary,
    ])

    undef = subprocess.check_output(["nm", "-u", str(binary)], text=True)
    assert "atexit" not in undef, f"Unexpected atexit in undefined symbols:\n{undef}"

    run(["codesign", "--force", "--sign", "-", str(kext)])
    run(["codesign", "--verify", "--verbose", str(kext)])
    run(["file", str(binary)])
    print(f"\nBUILT OK: {kext}")


if __name__ == "__main__":
    main()
