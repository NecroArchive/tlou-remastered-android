#!/usr/bin/env python3
"""Experimental TLOU AOT compiler adapter. Requires a local bb-recomp binary.

Prepares TLOU's G3PROBE5 image and translates its x86-64 guest code to ARM64
on the player's own device. This is not yet a game runtime integration.
Never upload games, prepared images or generated .so files to CI/GitHub.
"""
from __future__ import annotations

# PS4 fiber callbacks can replace the entire x86 register file. The compiled
# caller must reload callee-saved GPRs from GuestState after a callback.
# The upstream bb-recomp --resume shard key includes the calleeSaved option.
NATIVE_FIBER_CPU_FLAGS = ("--tso", "--resume-entries", "--no-callee-saved")


def native_guest_flags(profile: str) -> tuple[str, ...]:
    """No-TSO is strictly opt-in; fiber and resume entry flags never change."""
    if profile not in ("v3", "v4", "v5", "off"):
        raise ValueError("Unrecognized TSO lowering profile")
    return tuple(f for f in NATIVE_FIBER_CPU_FLAGS
                 if profile != "off" or f != "--tso")


# AOT vs FEX gameplay (logs 28/29): six workers 3.00 vs 2.52 CPU cores.
# Keep --tso and fiber ABI safeguards. The verified PS4 1.11 native image has
# all static patches built in, --no-game-hooks, and no runtime x86 patch writes.
# The pinned compiler explicitly supports -O2 and --no-dirty-check.
# Native patches are stored as alternatives of affected functions. Their
# original entry checks are REQUIRED for bb-recomp variant redirection.
# Keep LLVM -O2; never use --no-dirty-check with --variants.
AOT_111_PERFORMANCE_FLAGS = ("-O2",)
AOT_111_PERFORMANCE_PROFILE = "tlou-111-llvmO2-native-variants-v1"

import argparse
import hashlib
import re
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


# CUSA00552 01.11: decoded switch at +0xb59ff7, table at +0x117d550.
# The 11 indexed cases (0..10, with two aliases) include +0xb5a041,
# which was reached on-device after fixing the worker's FS/GS TLS.
# Never apply these offsets to an unverified game executable.
TLOU_111_SWITCH_SHA256 = "3cfa475f88ca68485b6b9230e62b84e195cdbe19e6f7295c110b8dcf8f20884e"
TLOU_111_SWITCH_PC = 0xB59FF7
TLOU_111_SWITCH_TABLE = 0x117D550
TLOU_111_SWITCH_SLOTS = 11
# Hash of a short instruction sequence; no game code bytes are shipped.
TLOU_111_SWITCH_SIGNATURE_LEN = 27
TLOU_111_SWITCH_SIGNATURE_SHA256 = "0682edaeaeb0618c6124ac1af9927f051e1cbb04de0f47ce636fe889cb2ee711"


# A second verified 01.11 switch discovered at +0x9c7650: 4 cases.
# Its case +0x9c7667 caused the STOP after the game reached world-military-city.
TLOU_111_SECOND_SWITCH_PC = 0x9C7650
TLOU_111_SECOND_SWITCH_TABLE = 0x11562B0
TLOU_111_SECOND_SWITCH_SLOTS = 4
TLOU_111_SECOND_SWITCH_SIGNATURE_LEN = 23
TLOU_111_SECOND_SWITCH_SIGNATURE_SHA256 = "3e712ef08edbc99750546e5d47589b1145bf67d957605d1ffef7680c8a3d106e"

# Verified NdJobWorkerThre indirect jump misses in logs 24, 26 and 27.
# The newest +0x1c28ad was outside the 35 targeted granular hints and
# stopped a genuine TSO-OFF recompilation after ~3m32s. The exact seed
# must be present even without an exported misses log on a future install.
TLOU_111_WORKER_JUMP_TARGETS = (
    0x1BF55B, 0x1C28AD, 0x553C14, 0x553C82, 0xA2125E,
)

# Worker's unresolved indirect-branch target families. Granularizing only
# prior target and caller functions (35 hints -> 627 granular functions)
# missed a *different* target function just 13 KiB away in ZIP 27. Sweep
# bounded, verified 1.11 code neighborhoods so any function with an
# instruction in these ranges becomes granular before the next gameplay.
# Every image offset in each bounded region is passed to --granular-funcs;
# the compiler maps offsets to enclosing functions and deduplicates starts.
# Unlike sampling every 32 bytes this also catches very short functions.
# Regions are exclusive at end: 112 KiB/114688 selectors total, NOT the
# whole executable. This changes code generation only, not runtime logging.
TLOU_111_WORKER_JUMP_HOTSPOTS = (
    (0x1BC000, 0x1C6000),  # animation/job-worker jump family, logs 26/27
    (0x54F000, 0x559000),  # worker +0x553c14/+0x553c82 family
    (0xA1D000, 0xA25000),  # worker +0xa2125e family
)
TLOU_111_WORKER_JUMP_HOTSPOT_STEP = 1

# Real 1.11 AOT jump caller PCs from logs 24/26/27; a true miss log
# additionally contributes its own callers, regardless of source version.
TLOU_111_KNOWN_INDIRECT_CALLERS = (
    0x1BF38A, 0x1BF68D, 0x1C2EF6, 0xB4E33, 0xB484E,
    0xCCA09, 0xCDE4D, 0x2519, 0x5115B3, 0x511A58,
    0x487D38, 0x7E06A3, 0x7E123E, 0xA21A20,
    0xA1EA44, 0x9DB103, 0x9DBB8C,
)



def known_switch_entries(boot: Path, eboot_sha: str) -> list[int]:
    """Find every target of the verified 1.11 switch, not just the first miss.

    The compiler builds *one* 64-shard unit table. Adding a later case changes
    that table and invalidates the whole existing --resume cache. Preseeding
    all cases now avoids a separate full rebuild for every unvisited case.
    """
    if eboot_sha != TLOU_111_SWITCH_SHA256:
        return []
    info = check_boot(boot)
    image_start = boot.stat().st_size - info["size"]
    needed = TLOU_111_SWITCH_TABLE + TLOU_111_SWITCH_SLOTS * 4
    if needed > info["size"]:
        raise ValueError("1.11 jump table outside the linked image")
    with boot.open("rb") as stream:
        stream.seek(image_start + TLOU_111_SWITCH_PC)
        signature = stream.read(TLOU_111_SWITCH_SIGNATURE_LEN)
        if hashlib.sha256(signature).hexdigest() != TLOU_111_SWITCH_SIGNATURE_SHA256:
            raise ValueError("1.11 executable no longer matches the verified switch dispatch")
        stream.seek(image_start + TLOU_111_SWITCH_TABLE)
        data = stream.read(TLOU_111_SWITCH_SLOTS * 4)
    if len(data) != TLOU_111_SWITCH_SLOTS * 4:
        raise ValueError("1.11 jump table truncated")
    offsets = struct.unpack("<11i", data)
    targets = sorted(set(TLOU_111_SWITCH_TABLE + delta for delta in offsets))
    if (len(targets) != 10 or min(targets) < 0xB5A012 or
            max(targets) > 0xB5A33F or 0xB5A041 not in targets):
        raise ValueError("Unexpected 1.11 jump table targets; refuse unsafe AOT entry seeds")
    second_end = TLOU_111_SECOND_SWITCH_TABLE + TLOU_111_SECOND_SWITCH_SLOTS * 4
    if second_end > info["size"]:
        raise ValueError("1.11 second switch table outside the linked image")
    with boot.open("rb") as stream:
        stream.seek(image_start + TLOU_111_SECOND_SWITCH_PC)
        signature = stream.read(TLOU_111_SECOND_SWITCH_SIGNATURE_LEN)
        if hashlib.sha256(signature).hexdigest() != TLOU_111_SECOND_SWITCH_SIGNATURE_SHA256:
            raise ValueError("1.11 second verified switch signature mismatch")
        stream.seek(image_start + TLOU_111_SECOND_SWITCH_TABLE)
        second = stream.read(TLOU_111_SECOND_SWITCH_SLOTS * 4)
    if len(second) != 4 * TLOU_111_SECOND_SWITCH_SLOTS:
        raise ValueError("1.11 second verified switch table truncated")
    second_targets = sorted(set(TLOU_111_SECOND_SWITCH_TABLE + delta
                                for delta in struct.unpack("<4i", second)))
    if second_targets != [0x9C7667, 0x9C769E, 0x9C76D2, 0x9C7708]:
        raise ValueError("Unexpected 1.11 second switch targets")
    return sorted(set(targets + second_targets))


def verified_miss_entries(path: Path | None, image_size: int) -> list[int]:
    """Read only the compiler's genuine 'miss +hex' entries.

    Older logs contain 'outside 0xffffffffffffffff', which is not code and
    cannot be fixed by adding a unit. Leave those lines out of the build set.
    """
    import re
    if not path or not path.is_file():
        return []
    found = set()
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            match = re.match(r"^\s*miss\s+\+(?:0x)?([0-9a-fA-F]+)\b", line)
            if not match:
                continue
            entry = int(match.group(1), 16)
            if 0x1000 <= entry < image_size:
                found.add(entry)
    return sorted(found)


def granular_risk_entries(eboot_sha: str, misses: Path | None,
                          image_size: int) -> list[int]:
    """Cover entire functions implicated by saved AOT indirect jump misses.

    Exact extra entries fix only the missed target. --granular-funcs emits
    native entry points at every decoded instruction boundary inside the
    enclosing source/target functions, protecting against unseen sibling
    cases. The prepass is compile-time only, not runtime instrumentation.
    """
    import re
    risk = set()
    if eboot_sha == TLOU_111_SWITCH_SHA256:
        risk.update(TLOU_111_WORKER_JUMP_TARGETS)
        risk.update(TLOU_111_KNOWN_INDIRECT_CALLERS)
        # --granular-funcs resolves each hint to its enclosing function.
        # A bounded address sweep also reaches other unvisited functions
        # in each observed indirect-jump family. The compiler deduplicates
        # them in its prepass; do not enable expensive whole-image granularity.
        for first, last in TLOU_111_WORKER_JUMP_HOTSPOTS:
            risk.update(range(first, min(last, image_size),
                              TLOU_111_WORKER_JUMP_HOTSPOT_STEP))
    if misses and misses.is_file():
        with misses.open("r", encoding="utf-8", errors="replace") as stream:
            for line in stream:
                match = re.match(
                    r"^\s*miss\s+\+(?:0x)?([0-9a-fA-F]+)\b.*\bkind=jump\b", line)
                if not match:
                    continue
                risk.add(int(match.group(1), 16))
                context = re.search(r"\bbacktrace=([^\s]+)", line)
                if context:
                    for frame in context.group(1).split(",")[:12]:
                        match_frame = re.fullmatch(r"\+(?:0x)?([0-9a-fA-F]+)", frame)
                        if match_frame:
                            risk.add(int(match_frame.group(1), 16))
    return sorted(value for value in risk if 0x1000 <= value < image_size)


def prepare_granular_risk_file(boot: Path, eboot_sha: str, misses: Path | None,
                               destination: Path) -> list[int]:
    hints = granular_risk_entries(eboot_sha, misses, check_boot(boot)["size"])
    if hints:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text("".join(f"+0x{value:x}\n" for value in hints),
                               encoding="ascii")
    else:
        destination.unlink(missing_ok=True)
    return hints


def prepare_extra_entries(boot: Path, eboot_sha: str, misses: Path | None,
                          destination: Path) -> tuple[list[int], list[int]]:
    known = known_switch_entries(boot, eboot_sha)
    image_size = check_boot(boot)["size"]
    observed = (list(TLOU_111_WORKER_JUMP_TARGETS)
                if eboot_sha == TLOU_111_SWITCH_SHA256 else [])
    if any(target >= image_size for target in observed):
        raise ValueError("Validated 1.11 worker entry outside the linked image")
    missed = verified_miss_entries(misses, image_size)
    entries = sorted(set(known + observed + missed))
    if entries:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text("".join(f"+0x{value:x}\n" for value in entries),
                               encoding="ascii")
    else:
        destination.unlink(missing_ok=True)
    return known, entries





def prepare_native_animation_patch(boot: Path, self_digest: str,
                                   output: Path,
                                   options: dict | None = None,
                                   catalog_mode: bool = False) -> list[str]:
    """Compile fully byte-verified 1.11 patches into an isolated native image.

    Unspecified options retain the legacy animation-only mode for compatibility
    with the synthetic regression fixtures. The Android app ALWAYS supplies its
    current patch-options.json and checks the selected profile before launch.

    The normal FEX boot and the player's SELF are never modified. Every byte
    guard, including the read-only guards, must match the original linked
    image before the transactional output file is published.
    """
    if KNOWN_EBOOTS.get(self_digest) != "1.11":
        output.unlink(missing_ok=True)
        return []
    import importlib.util
    profile = Path(__file__).resolve().parents[1] / "tlou_patches.py"
    spec = importlib.util.spec_from_file_location("tlou_aot_guarded_patches", profile)
    if not spec or not spec.loader:
        raise ValueError("Missing verified 1.11 patch profile")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if self_digest not in module.SELF_SHA256_111:
        raise ValueError("AOT native patches require verified 1.11 executable")
    if options is None:
        groups = {"invalid-animation-output-pointer":
                  module.BASE_PATCHES_111["invalid-animation-output-pointer"]}
    else:
        # On the variant path compile only mandatory stability patches into
        # the baseline. Every resolution and visual effect (including OFF)
        # must remain selectable without retranslating other game functions.
        # Validating selected options here is useful, but NEVER bake those
        # optional options into the AOT image.
        module.selected_options(options, "01.11", self_digest)
        baseline = {"job_sleep": True} if catalog_mode else options
        groups = module.base_patches_for(baseline, "01.11")
        if not catalog_mode:
            groups.update(module.selected_options(options, "01.11", self_digest))
        # Generic 1.11 AOT job-worker idle-loop optimization. This is not
        # selected by GPU/SoC/device identity; keep the existing codegen and
        # launch compatibility unchanged while expanding jump coverage.
        if options.get("job_sleep", True):
            groups["native-job-idle-backoff"] = [
                (0xC87D71, "483de8030000", "483d80000000"),
            ]
        if "invalid-animation-output-pointer" not in groups:
            raise ValueError("Required native animation guard was disabled")
    cave = groups["invalid-animation-output-pointer"]
    if len(cave) != 2 or [addr for addr, _, _ in cave] != [0xA623D3, 0xC9FC61]:
        raise ValueError("Verified 1.11 animation patch changed")
    info = check_boot(boot)
    if info["magic"] != "G3PROBE5":
        raise ValueError("Expected original G3PROBE5 image")
    start = boot.stat().st_size - info["size"]
    changes: list[tuple[int, bytes]] = []
    original_checks: dict[tuple[int, int], bytes] = {}
    with boot.open("rb") as source:
        for group, entries in groups.items():
            for addr, expected_hex, replacement_hex in entries:
                expected = bytes.fromhex(expected_hex)
                if not expected or addr < 0 or addr + len(expected) > info["size"]:
                    raise ValueError("Native patch outside linked guest image: "
                                     + group + " " + hex(addr))
                source.seek(start + addr)
                actual = source.read(len(expected))
                if actual != expected:
                    raise ValueError("1.11 native patch guard mismatch: "
                                     + group + " at " + hex(addr))
                key = (addr, len(expected))
                if key in original_checks and original_checks[key] != expected:
                    raise ValueError("Conflicting original-byte patch guards")
                original_checks[key] = expected
                if replacement_hex is None:
                    continue  # validation only, never write this span
                replacement = bytes.fromhex(replacement_hex)
                if len(replacement) != len(expected):
                    raise ValueError("Native patch changes executable image layout: "
                                     + group + " at " + hex(addr))
                for previous_at, previous in changes:
                    if addr < previous_at + len(previous) and previous_at < addr + len(replacement):
                        raise ValueError("Overlapping native patches: "
                                         + group + " at " + hex(addr))
                changes.append((addr, replacement))
    output.parent.mkdir(parents=True, exist_ok=True)
    temp = output.with_suffix(output.suffix + ".tmp")
    try:
        shutil.copyfile(boot, temp)
        with temp.open("r+b") as modified:
            for addr, data in changes:
                modified.seek(start + addr)
                modified.write(data)
            modified.flush()
            os.fsync(modified.fileno())
        if check_boot(temp) != info:
            raise ValueError("Native boot structure changed")
        os.replace(temp, output)
    finally:
        temp.unlink(missing_ok=True)
    return list(groups)



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


def verify_aot_coverage(stats: Path, meta: dict) -> dict:
    """Refuse useless native images that mostly contain unsupported-op traps."""
    import re
    version = str(meta.get("compiler_version", ""))
    if version not in {"3-tlou-string", "4-tlou-tso-aligned-ar-v1", "5-tlou-tso-outlined-unaligned-v1"}:
        raise ValueError("Old bb-recomp compiler: TSO LLVM lowering is missing; rebuild with TLOU TSO tool")
    if not stats.is_file():
        raise ValueError("No native compiler coverage report")
    text = stats.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"units with bbr_trap \(unsupported ops\): (\d+) \(([\d.]+)%\)", text)
    if not match:
        raise ValueError("Native compiler omitted unsupported-op coverage summary")
    dirty = int(match.group(1))
    percent = float(match.group(2))
    count = int(meta.get("unit_count", 0))
    if count <= 0 or dirty > count:
        raise ValueError("AOT function coverage count invalid")
    # Default TLOU v1.11 upstream 1.3.0 --tso produced 60598 dirty
    # of 75413 units. With the new memory lowering, at most a tiny number
    # of genuinely unsupported string/vector instructions may remain.
    if "LoadMemTSO" in text or "StoreMemTSO" in text or percent > 0.25:
        raise ValueError(f"AOT has {dirty}/{count} units without native code ({percent}%); refusing unusable build")
    return {"unsupported_units": dirty, "unsupported_percent": percent,
            "estimated_usable_units": count - dirty, "compiler_version": version}


def verify_native_variant_coverage(stats: Path, catalog: dict) -> dict:
    """Never mark a library ready if ANY requested patch function failed lifting.

    The pinned compiler's summary is written to compile.txt even without
    --variant-report; count the full group and function coverage. A partial
    variant catalog must not silently launch with missing visual patches.
    """
    report = stats.read_text(encoding="utf-8", errors="replace")
    pattern = (r"variants:\s+(\d+)\s+groups in the catalogs\s+"
               r"\((\d+)\s+touch code\),\s+(\d+)\s+variants planned,\s+"
               r"(\d+)\s+built\s+\([^)]*\),\s+(\d+)\s+skipped,")
    found = re.search(pattern, report)
    if not found:
        raise ValueError("Compiler did not confirm complete native patch variants")
    declared, touches, planned, built, skipped = map(int, found.groups())
    required = len(catalog["groups"])
    if (declared != required or touches != required or planned < required
            or built != planned or skipped):
        raise ValueError(
            f"Incomplete AOT patch variants: {touches}/{required} groups, "
            f"{built}/{planned} functions, {skipped} skipped; refusing build")
    return {"groups": declared, "groups_with_code": touches,
            "variants_planned": planned, "variants_built": built,
            "variants_skipped": skipped}


def available_compile_memory_bytes() -> int:
    """Actual Linux MemAvailable, not PS4 sparse virtual pool or zram size."""
    try:
        for line in Path("/proc/meminfo").read_text(encoding="ascii").splitlines():
            if line.startswith("MemAvailable:"):
                parts = line.split()
                return int(parts[1]) * 1024 if len(parts) >= 3 and parts[2] == "kB" else 0
    except (OSError, ValueError):
        pass
    return 0


def select_compile_workers(requested: int, available_bytes: int) -> int:
    """Avoid AOT codegen's ~2 GiB peak colliding with 8 GB Android LMKD.

    This only changes translation throughput, not code, runtime scheduling,
    shard count, TSO profile, or FPS. Unknown availability keeps the request.
    """
    if available_bytes <= 0:
        return requested
    gib = 1024 ** 3
    ceiling = 1 if available_bytes < 4 * gib else 2 if available_bytes < 6 * gib else requested
    return max(1, min(requested, ceiling))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True,
                        help="On-device folder containing eboot.bin, sce_module/ and sce_sys/")
    parser.add_argument("--out", type=Path, required=True, help="Private writable work directory")
    parser.add_argument("--boot", type=Path, help="Prepared G3PROBE5 boot-linked.bin")
    parser.add_argument("--compiler", type=Path, help="On-device bb-recomp v1.3+ executable")
    parser.add_argument("--bundle-dir", type=Path, help="bb-recomp bundle directory")
    parser.add_argument("--misses", type=Path, help="Previous AOT misses; add missing guest entry points")
    parser.add_argument("--options", type=Path, help="Current FEX-validated native patch selections")
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--shards", type=int, default=64)
    # Shipping Android frontend supports only the one validated build policy.
    # Older developer TSO variants are not selectable through packaged tools.
    parser.add_argument("--tso-profile", choices=("off",), default="off",
                        help="Internal compatibility flag; the only Android AOT build mode")
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
        digest = sha256(game / "eboot.bin")
        native_boot = out / "native-boot-linked.bin"
        native_options = None
        if args.options is not None:
            native_options = json.loads(args.options.read_text(encoding="utf-8"))
            if not isinstance(native_options, dict):
                raise ValueError("Native patch options must be a JSON object")
        # Single library, all user-selectable patches compiled as variants.
        # Do not put any optional visual/resolution byte changes into the
        # baseline image; otherwise the 'off' version cannot be selected.
        catalog_mode = version == "1.11" and native_options is not None
        native_patches = prepare_native_animation_patch(
            boot, digest, native_boot, native_options, catalog_mode=catalog_mode)
        variant_info = None
        if catalog_mode:
            try:
                from .patch_variants import create_catalog
            except ImportError:
                from patch_variants import create_catalog
            variant_info = create_catalog(
                native_boot, digest, out / "aot" / "tlou-111-all-variants.txt")
        staged = stage_boot(native_boot if native_patches else boot, out / "bb-prepared")
        aot = out / "aot"
        aot.mkdir(exist_ok=True)
        so = aot / "libtlouaot.so"
        fast_111 = version == "1.11" and native_options is not None and (
            "invalid-animation-output-pointer" in native_patches and
            "frame-timeout" in native_patches)
        cpu_flags = AOT_111_PERFORMANCE_FLAGS if fast_111 else ("-O1",)
        available_compile_bytes = available_compile_memory_bytes()
        compile_jobs = select_compile_workers(args.jobs, available_compile_bytes)

        # --tso is a CODE GENERATION option, not a runtime switch.
        # Fibers and resume-entry safeguards remain enabled in both modes.
        guest_flags = native_guest_flags(args.tso_profile)
        command = [str(args.compiler or "/path/to/libbbrecomp.so"),
                   "--boot", str(staged), "--game-dir", str(game), "--so", str(so),
                   "--stats", str(aot / "compile.txt"), "--jobs", str(compile_jobs),
                   "--shards", str(args.shards), "--target", "aarch64",
                   "--host", "armv8.2", *guest_flags, *cpu_flags, "--no-nice",
                   "--progress", "--resume"]
        if args.bundle_dir:
            command += ["--bundle-dir", str(args.bundle_dir)]
        if variant_info:
            command += ["--variants", variant_info["catalog"]]
            command += ["--variant-report"]
            if "--no-dirty-check" in command:
                raise ValueError("Native patch variants require dirty-entry checks")
        # All 11 entries of the verified 1.11 scheduler switch are seeded at
        # once. This avoids repeated full recompilation as new cases execute.
        seed_file = aot / "verified-extra-entries.txt"
        seed_targets, added_entries = prepare_extra_entries(
            staged, digest, args.misses, seed_file)
        granular_file = aot / "granular-risk-functions.txt"
        granular_hints = prepare_granular_risk_file(
            staged, digest, args.misses, granular_file)
        if granular_hints:
            command += ["--granular-funcs", str(granular_file)]
        patch_entries = [0xC9FC61] if native_patches else []
        if patch_entries:
            added_entries = sorted(set(added_entries + patch_entries))
            seed_file.write_text(
                "".join(f"+0x{entry:x}\n" for entry in added_entries),
                encoding="ascii")
        if added_entries:
            command += ["--extra-entries", str(seed_file)]
        # Verify the emitted native jump table has ALL genuine runtime misses,
        # not just the switch cases. Percent coverage alone missed this crash.
        checked_entries = added_entries
        if checked_entries:
            command += ["--check-entries",
                        ",".join(f"{target:x}" for target in checked_entries)]
        report = {"version": version, "original_game": str(original_game),
                  "effective_game": str(game), "eboot_sha256": sha256(game / "eboot.bin"),
                  "input_boot_sha256": sha256(boot), "staged_boot_sha256": sha256(staged),
                  "native_patch_profile": (
                      variant_info["profile"] if variant_info
                      else "tlou-111-animation-guard-v1" if native_patches else ""),
                  "variant_catalog": variant_info,
                  "native_patches": native_patches,
                  "native_patch_options": native_options,
                  "native_codegen_profile": AOT_111_PERFORMANCE_PROFILE if fast_111 else "conservative-O1",
                  "native_codegen_flags": list(cpu_flags),
                  "runtime_code_immutable": not bool(variant_info),
                  "runtime_patch_contract": (
                      "native-variants;--no-game-hooks;--patches-at-launch"
                      if variant_info else "checks-enabled"),
                  "native_boot_sha256": sha256(native_boot) if native_patches else "",
                  "native_patch_entries": [f"+0x{x:x}" for x in patch_entries],
                  "verified_executable_bytes": verified_code_bytes,
                  "compile_memory_available_mib": available_compile_bytes // (1024 * 1024),
                  "compile_workers_requested": args.jobs,
                  "compile_workers_effective": compile_jobs,
                  "input_boot": check_boot(boot), "translation_command": command,
                  "runtime_integrated": False, "status": "staged",
                  "verified_switch_targets": [f"+0x{x:x}" for x in seed_targets],
                  "extra_entry_count": len(added_entries),
                  "granular_risk_hint_count": len(granular_hints),
                  "granular_policy": "verified-indirect-miss-owners-and-callers",
                  "tso_profile": args.tso_profile,
                  "guest_memory_model": ("relaxed_ARM64_no_TSO_UNSAFE"
                                         if args.tso_profile == "off"
                                         else "conservative_TSO"),
                  "fiber_resume_entries": True,
                  "callee_saved_policy": "reload_after_calls_for_fibers",
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
            report["coverage"] = verify_aot_coverage(aot / "compile.txt", meta)
            if variant_info:
                report["variant_build"] = verify_native_variant_coverage(
                    aot / "compile.txt", variant_info)
                if int(meta.get("abi_flags", 0)) & 6 != 6:
                    raise ValueError("Native variant ABI lacks dirty-entry and variant flags")
            report["status"] = "aarch64_generated_native_ready"
            report["runtime_integrated"] = True
            report["unit_count"] = meta.get("unit_count")
        (out / "aot-report.json").write_text(json.dumps(report, indent=2) + "\n")
        return code
    except (OSError, ValueError, subprocess.CalledProcessError) as e:
        # Persist the actual failure in the exported diagnostic ZIP. Never
        # accidentally reuse a previously generated .so after quality gates
        # rejected the newer compiler or an excessive number of trap units.
        if "report" in locals() and "out" in locals():
            report["status"] = "validation_failed"
            report["failure_reason"] = str(e)
            try:
                (out / "aot-report.json").write_text(json.dumps(report, indent=2) + "\n")
            except OSError:
                pass
        print(f"TLOU local AOT: {e}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
