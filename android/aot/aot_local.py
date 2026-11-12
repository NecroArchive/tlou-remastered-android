#!/usr/bin/env python3
"""Experimental TLOU AOT compiler adapter. Requires a local bb-recomp binary.

Prepares TLOU's G3PROBE5 image and translates its x86-64 guest code to ARM64
on the player's own device. This is not yet a game runtime integration.
Never upload games, prepared images or generated .so files to CI/GitHub.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

KNOWN_EBOOTS = {
    "e5f0aa322f60bd90637cc10d83de08c77885b1921084021c630e04ed3d6febf7": "1.00",
    "3cfa475f88ca68485b6b9230e62b84e195cdbe19e6f7295c110b8dcf8f20884e": "1.11",
    "31f3b0d120803878d10a7184d139c545de334394dd7dd6ca6058a7c995e484a7": "1.11",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def check_self(path: Path) -> None:
    with path.open("rb") as stream:
        first = stream.read(32)
        if len(first) != 32 or first[:4] != b"O\x15=\x1d":
            raise ValueError(f"Not a PS4 SELF: {path}")
        seg_count = struct.unpack_from("<H", first, 24)[0]
        if not 0 < seg_count < 256:
            raise ValueError(f"Invalid SELF section count: {path}")
        stream.seek(32 + seg_count * 32)
        elf = stream.read(64)
        if elf[:7] != b"\x7fELF\x02\x01\x01" or len(elf) != 64:
            raise ValueError(f"No plaintext ELF64 in {path}")
        if struct.unpack_from("<H", elf, 18)[0] != 62:
            raise ValueError(f"Not x86-64: {path}")


def check_boot(path: Path) -> dict:
    with path.open("rb") as stream:
        header = stream.read(104)
        if len(header) != 104:
            raise ValueError("Truncated boot-linked header")
        magic, size, entry, ns, nr, ni, _, _, *tail = struct.unpack("<8s12Q", header)
        mc = tail[-1]
        if magic not in (b"G3PROBE5", b"BBPROBE5") or not 0 < size <= 512*1024*1024:
            raise ValueError("Invalid boot image or signature")
        if not 0 < ns <= 64 or nr > 4_000_000 or ni > 100_000 or not 0 < mc <= 16:
            raise ValueError("Invalid boot tables")
        stream.seek(104 + 56 * mc)
        b = stream.read(8)
        if len(b) != 8:
            raise ValueError("Truncated bindings header")
        nb = struct.unpack("<Q", b)[0]
        if nb > ni or 104 + 56*mc + 8 + 24*nb + 24*ns + 128*ni + 32*nr + size != path.stat().st_size:
            raise ValueError("Boot image size mismatch")
        return {"magic": magic.decode(), "size": size, "entry": entry,
                "segments": ns, "relocations": nr, "imports": ni,
                "modules": mc, "bindings": nb}


def verify_eboot_image(eboot: Path, boot: Path) -> int:
    """Check EVERY executable guest byte against the linked image.

    TLOU's linker copies the original x86-64 text as-is, except for the
    documented FS->GS 9-byte rewrite. This detects an update/base mismatch
    before LLVM translates an incompatible executable, without checking
    unrelated gigabytes of assets.
    """
    info = check_boot(boot)
    image_start = boot.stat().st_size - info["size"]
    header = eboot.read_bytes()
    if len(header) < 64 or header[:4] != b"O\x15=\x1d":
        raise ValueError("Expected plaintext PS4 SELF")
    count = struct.unpack_from("<H", header, 24)[0]
    base = 32 + count * 32
    phoff = struct.unpack_from("<Q", header, base + 32)[0]
    phsize, phnum = struct.unpack_from("<HH", header, base + 54)
    if phsize != 56 or not 0 < phnum < 256:
        raise ValueError("Invalid ELF program headers")
    patches = (b"\x64\x48\x8b\x04\x25\x00\x00\x00\x00",
               b"\x65\x48\x8b\x04\x25\x00\x00\x00\x00")
    checked = 0
    with boot.open("rb") as linked:
        for index in range(phnum):
            p = struct.unpack_from("<II6Q", header, base + phoff + 56 * index)
            kind, flags, _, vaddr, _, filesz, _, _ = p
            if kind != 1 or not flags & 1 or filesz == 0:
                continue
            if vaddr > info["size"] or filesz > info["size"] - vaddr:
                raise ValueError("Executable segment outside linked image")
            source_segment = None
            for i in range(count):
                f, pos, n, mem = struct.unpack_from("<4Q", header, 32 + i * 32)
                if f & 0x800 and (f >> 20) & 4095 == index:
                    if f & 10 or n != filesz or mem != filesz:
                        raise ValueError("Unsupported compressed/executable SELF segment")
                    if pos > len(header) or n > len(header) - pos:
                        raise ValueError("Truncated SELF executable segment")
                    source_segment = header[pos:pos+n]
                    break
            if source_segment is None:
                raise ValueError("Executable SELF segment missing")
            linked.seek(image_start + vaddr)
            actual = linked.read(filesz)
            expected = source_segment.replace(*patches)
            if actual != expected:
                raise ValueError("boot-linked image contains a different eboot (1.00/1.11 mismatch)")
            checked += filesz
    if checked == 0:
        raise ValueError("No executable bytes verified")
    return checked


def effective_game_folder(base_game: Path, boot: Path | None) -> Path:
    """Use the selected update's merged view, never the original base eboot.

    TLOU boots boot-linked.bin made from tlou-view (base + 1.11 update).
    Using --game-dir against the base 1.00 ELF while the image has 1.11
    would produce wrong function offsets and an unusable native library.
    """
    if boot is None:
        return base_game
    view = boot.parent / "tlou-view"
    if view.is_dir():
        if not (view / "eboot.bin").is_file():
            raise ValueError("Prepared tlou-view is missing the effective eboot.bin")
        return view.resolve(strict=True)
    if (boot.parent / "device-ready.json").is_file():
        raise ValueError("Prepared boot has no tlou-view: refusing to mix game versions")
    return base_game


def prepare(repo: Path, game: Path, out: Path) -> Path:
    scripts = repo / "gow3_pc/scripts"
    for script in ("prepare.py", "link_modules.py"):
        subprocess.run([sys.executable, str(scripts / script), str(game),
                        "--out", str(out)], check=True)
    return out / "boot-linked.bin"


def stage_boot(source: Path, out: Path) -> Path:
    src = check_boot(source)
    if src["magic"] != "G3PROBE5" or src["modules"] != 2:
        raise ValueError("Expected TLOU G3PROBE5 with both linked PRX modules")
    out.mkdir(parents=True, exist_ok=True)
    dest = out / "boot-linked.bin"
    tmp = out / "boot-linked.bin.tmp"
    try:
        with source.open("rb") as read, tmp.open("wb") as write:
            read.seek(8)
            write.write(b"BBPROBE5")
            shutil.copyfileobj(read, write, length=1024*1024)
        os.replace(tmp, dest)
    finally:
        tmp.unlink(missing_ok=True)
    if check_boot(dest)["magic"] != "BBPROBE5" or dest.stat().st_size != source.stat().st_size:
        raise ValueError("Boot staging failed")
    # The compiler's ELF reader expects the module order from link.json.
    link = source.parent / "link.json"
    if link.is_file():
        doc = json.loads(link.read_text())
        names = [v["file"] for v in doc.get("modules", [])]
        if names != ["libc.prx", "libSceFios2.prx"]:
            raise ValueError(f"Unexpected linked module order: {names}")
        shutil.copyfile(link, out / "link.json")
    else:
        (out / "link.json").write_text(json.dumps({"modules": [
            {"file": "libc.prx"}, {"file": "libSceFios2.prx"}]}))
    return dest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True,
                        help="On-device folder containing eboot.bin, sce_module/ and sce_sys/")
    parser.add_argument("--out", type=Path, required=True, help="Private writable work directory")
    parser.add_argument("--boot", type=Path, help="Prepared G3PROBE5 boot-linked.bin")
    parser.add_argument("--compiler", type=Path, help="On-device bb-recomp v1.3+ executable")
    parser.add_argument("--bundle-dir", type=Path, help="bb-recomp bundle directory")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--shards", type=int, default=64)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    try:
        game = args.game.resolve(strict=True)
        # Prefer the merged eboot of the prepared image when an update exists.
        boot_input = args.boot.resolve(strict=True) if args.boot else None
        original_game = game
        game = effective_game_folder(game, boot_input)
        out = args.out.resolve()
        if not 1 <= args.jobs <= 16 or not 1 <= args.shards <= 4096:
            raise ValueError("Invalid --jobs or --shards")
        for rel in ("eboot.bin", "sce_module/libc.prx", "sce_module/libSceFios2.prx"):
            check_self(game / rel)
        version = KNOWN_EBOOTS.get(sha256(game / "eboot.bin"))
        if not version:
            raise ValueError("Unverified TLOU executable, refusing AOT translation")
        if not (game / "sce_sys/param.sfo").is_file() and not args.dry_run:
            raise ValueError("Missing sce_sys/param.sfo, needed for AOT manifest")
        out.mkdir(parents=True, exist_ok=True)
        boot = (boot_input if boot_input else
                prepare(Path(__file__).resolve().parents[2], game, out / "tlou-prepared"))
        verified_code_bytes = verify_eboot_image(game / "eboot.bin", boot)
        staged = stage_boot(boot, out / "bb-prepared")
        aot = out / "aot"
        aot.mkdir(exist_ok=True)
        so = aot / "libtlouaot.so"
        command = [str(args.compiler or "/path/to/libbbrecomp.so"),
                   "--boot", str(staged), "--game-dir", str(game), "--so", str(so),
                   "--stats", str(aot / "compile.txt"), "--jobs", str(args.jobs),
                   "--shards", str(args.shards), "--target", "aarch64",
                   "--host", "armv8.2", "--tso", "-O1", "--no-nice",
                   "--progress", "--resume"]
        if args.bundle_dir:
            command += ["--bundle-dir", str(args.bundle_dir)]
        report = {"version": version, "original_game": str(original_game),
                  "effective_game": str(game), "eboot_sha256": sha256(game / "eboot.bin"),
                  "input_boot_sha256": sha256(boot), "staged_boot_sha256": sha256(staged),
                  "verified_executable_bytes": verified_code_bytes,
                  "input_boot": check_boot(boot), "translation_command": command,
                  "runtime_integrated": False, "status": "staged",
                  "guest_memory_model": "conservative_TSO",
                  "x87_mode_warning": "bb-recomp uses reduced precision x87, while TLOU FEX defaults to full; do not execute without compatibility validation"}
        (out / "aot-report.json").write_text(json.dumps(report, indent=2) + "\n")
        if args.dry_run:
            print("Input verified; BBPROBE5 adapter staged. AOT NOT YET COMPILED.")
            print(" ".join(command))
            return 0
        if not args.compiler or not args.compiler.is_file():
            raise ValueError("Local bb-recomp binary missing; supply --compiler")
        code = subprocess.run(command, check=False).returncode
        report["status"] = "compiler_failed"
        report["exit_code"] = code
        if code == 0:
            metadata_file = aot / "libtlouaot.json"
            if not so.is_file() or not metadata_file.is_file():
                raise ValueError("Compiler did not write .so and .json")
            meta = json.loads(metadata_file.read_text())
            with so.open("rb") as f:
                elf = f.read(20)
            if elf[:4] != b"\x7fELF" or struct.unpack_from("<H", elf, 18)[0] != 183:
                raise ValueError("Output is not AArch64")
            if meta.get("abi_version") != 2 or meta.get("so_sha256") != sha256(so):
                raise ValueError("Unexpected AOT ABI or .so hash")
            report["status"] = "aarch64_generated_runtime_not_integrated"
            report["unit_count"] = meta.get("unit_count")
        (out / "aot-report.json").write_text(json.dumps(report, indent=2) + "\n")
        return code
    except (OSError, ValueError, subprocess.CalledProcessError) as e:
        print(f"TLOU local AOT: {e}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
