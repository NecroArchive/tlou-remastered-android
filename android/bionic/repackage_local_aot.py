#!/usr/bin/env python3
"""Stage a signed TLOU APK's stable native runtime plus new local ARM64 AOT tooling.

The input APK comes from a pinned successful GitHub Actions run. No user's game
content enters CI. Do NOT change the native libraries in a launcher-only update.
"""
from __future__ import annotations

import io
import os
import hashlib
from pathlib import Path, PurePosixPath
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
STAGE = ROOT / "out/android-bionic"
AOT = STAGE / "aot-compiler"
SOURCE_OVERRIDES = (
    "android/tlou_bionic.py", "android/device_prepare.py",
    "android/tlou_patches.py", "tools/probe_tlou.py",
    "android/aot/aot_local.py",
)


def safe_path(name: str) -> None:
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name:
        raise ValueError(f"Unsafe APK member path: {name}")


def compiler_payload() -> dict[str, bytes]:
    compiler = AOT / "bin/libbbrecomp.so"
    if not compiler.is_file() or not (AOT / "bundle").is_dir():
        raise ValueError("Verified on-device compiler not staged")
    code = compiler.read_bytes()
    if code[:4] != b"\x7fELF" or struct.unpack_from("<H", code, 18)[0] != 183:
        raise ValueError("AOT compiler not AArch64 ELF")
    result = {
        "bin/libbbrecomp.so": code,
        "aot/upstream.sha256": (AOT / "upstream.sha256").read_bytes(),
    }
    for folder in ("bundle", "licenses"):
        for p in sorted((AOT / folder).rglob("*")):
            if p.is_file() and not p.is_symlink():
                result[f"aot/{folder}/{p.relative_to(AOT / folder).as_posix()}"] = p.read_bytes()
    if not any(x.startswith("aot/bundle/") for x in result):
        raise ValueError("Compiler missing LLVM bundle")
    # AOT TSO profiles are distinct compilers with distinct linker bundles:
    # `--tso` stays enabled, but codegen selection occurs before translation.
    # The experiment's packaging workflow supplies archived, pinned v3/v4
    # compilers, and the staged v5 compiler. Never silently mix bundles.
    profiles_root = os.environ.get("TLOU_TSO_MULTI_PROFILE_DIR", "").strip()
    if profiles_root:
        expected_versions = {
            "v3": b"3-tlou-string",
            "v4": b"4-tlou-tso-aligned-ar-v1",
            "v5": b"5-tlou-tso-outlined-unaligned-v1",
        }
        for profile, version in expected_versions.items():
            directory = AOT if profile == "v5" else (
                Path(profiles_root) / profile / "android")
            binary = directory / ("bin/libbbrecomp.so" if profile == "v5"
                                  else "libbbrecomp.so")
            if not binary.is_file():
                raise ValueError(f"Missing AOT TSO profile compiler: {profile}")
            data = binary.read_bytes()
            if (data[:4] != b"\\x7fELF" or
                    struct.unpack_from("<H", data, 18)[0] != 183 or
                    version not in data):
                raise ValueError(f"Wrong AOT TSO compiler ABI/version: {profile}")
            bundle = directory / "bundle"
            if not (bundle / "helpers-aarch64.o").is_file():
                raise ValueError(f"AOT TSO profile linker bundle missing: {profile}")
            key = f"aot/compilers/{profile}"
            result[f"{key}/libbbrecomp.so"] = data
            result[f"{key}/compiler.sha256"] = (
                hashlib.sha256(data).hexdigest()+"\\n").encode()
            for member in sorted(bundle.rglob("*")):
                if member.is_file() and not member.is_symlink():
                    result[f"{key}/bundle/{member.relative_to(bundle).as_posix()}"] = member.read_bytes()
    for name in SOURCE_OVERRIDES:
        result[f"source/{name}"] = (ROOT / name).read_bytes()
    for file in sorted((ROOT / "gow3_pc/scripts").glob("*.py")):
        result[f"source/gow3_pc/scripts/{file.name}"] = file.read_bytes()
    return result


def merge_tools(original: bytes, replacements: dict[str, bytes]) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(io.BytesIO(original)) as old, zipfile.ZipFile(
            output, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as new:
        names = set()
        for info in old.infolist():
            safe_path(info.filename)
            if info.filename in names:
                raise ValueError(f"Duplicate original tool: {info.filename}")
            names.add(info.filename)
            new.writestr(info, replacements.get(info.filename, old.read(info.filename)))
        for name, value in sorted(replacements.items()):
            safe_path(name)
            if name not in names:
                new.writestr(name, value, compress_type=zipfile.ZIP_DEFLATED)
    data = output.getvalue()
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        required = ("python/bin/python3.13", "bin/orbis-pkg-util",
                    "bin/libbbrecomp.so", "source/android/device_prepare.py",
                    "source/android/aot/aot_local.py", "aot/upstream.sha256")
        missing = [n for n in required if n not in archive.namelist()]
        if missing:
            raise ValueError(f"Tools ZIP missing {missing}")
        for name, value in replacements.items():
            if archive.read(name) != value:
                raise ValueError(f"Incorrect tools member: {name}")
    return data


def stage(apk: Path) -> None:
    replacements = compiler_payload()
    required = {
        "lib/arm64-v8a/libgow3cpu.so",
        "lib/arm64-v8a/libgow3gpu.so",
        "lib/arm64-v8a/libgow3present.so",
        "lib/arm64-v8a/libc++_shared.so",
        "lib/arm64-v8a/libpython3.13.so",
        "assets/runtime/gow3-probe",
        "assets/tools.zip",
    }
    names = set()
    with zipfile.ZipFile(apk) as original:
        names.update(original.namelist())
        if not required.issubset(names):
            raise ValueError(f"Base APK missing {sorted(required - names)}")
        for entry in original.infolist():
            name = entry.filename
            safe_path(name)
            if entry.is_dir():
                continue
            if name.startswith("lib/arm64-v8a/"):
                destination = STAGE / "apk-jni" / "arm64-v8a" / name.split("/", 2)[2]
            elif name.startswith(("assets/runtime/", "assets/drivers/")):
                destination = STAGE / "apk-assets" / name[len("assets/"):]
            else:
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(original.read(name))
        tools = merge_tools(original.read("assets/tools.zip"), replacements)
    path = STAGE / "apk-assets/tools.zip"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(tools)
    print(f"TLOU experimental AOT tools staged ({len(tools)} bytes); native CPU/GPU preserved from pinned signed base APK.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: repackage_local_aot.py <pinned-source-apk>")
    stage(Path(sys.argv[1]))
