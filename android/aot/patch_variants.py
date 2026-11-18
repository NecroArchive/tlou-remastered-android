#!/usr/bin/env python3
"""Build every verified TLOU 1.11 patch variant from a *baseline-only* boot.

Uses bb-recomp's native variant catalog (group / w / excl), not a full
library per combination. The Android game applies its selected BBPATCH2 at
each launch, and the AOT runtime hashes and selects the affected function
variant. Data writes need no variant: AOT guest code reads the guest bytes.

This file does not include game instructions or data. No user game assets
leave the device, and no driver, Vulkan option or TSO flag is changed.
"""
import hashlib
import importlib.util
from pathlib import Path
import struct

EFFECTS = ("ssao", "motion_blur", "depth_of_field", "bloom",
           "sunlight_shadows", "subsurface", "omni_light_shadows",
           "spot_light_shadows")
RESOLUTIONS = ("720", "540", "360")
PROFILE = "tlou-111-variants-v3"  # Omni/spot classification rerouted, no light discard.


def load_patches():
    path = Path(__file__).resolve().parents[1] / "tlou_patches.py"
    spec = importlib.util.spec_from_file_location("tlou_variant_patch_definitions", path)
    if spec is None or spec.loader is None:
        raise ValueError("Cannot load verified TLOU patch definitions")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def boot_layout(boot: Path):
    """G3PROBE5/BBPROBE5 segment table. Flag bit 0 means executable."""
    with boot.open("rb") as source:
        header = source.read(104)
        if len(header) != 104:
            raise ValueError("Truncated linked image")
        fields = struct.unpack("<8s12Q", header)
        magic, size, _, ns, nr, ni = fields[:6]
        mc = fields[-1]
        if magic not in (b"G3PROBE5", b"BBPROBE5") or not (
                0 < size <= 512 * 1024 * 1024 and 0 < ns <= 64
                and mc in (1, 2, 3, 4, 5, 6, 7, 8)):
            raise ValueError("Unrecognized linked-image layout")
        source.seek(104 + 56 * mc)
        nb_raw = source.read(8)
        if len(nb_raw) != 8:
            raise ValueError("Truncated binding count")
        nb = struct.unpack("<Q", nb_raw)[0]
        if nb > ni:
            raise ValueError("Invalid binding count")
        source.seek(104 + 56 * mc + 8 + 24 * nb)
        segments = []
        for _ in range(ns):
            raw = source.read(24)
            if len(raw) != 24:
                raise ValueError("Truncated linked-image segments")
            start, length, flags = struct.unpack("<3Q", raw)
            if start > size or length > size - start or flags > 7:
                raise ValueError("Bad linked-image segment")
            segments.append((start, start + length, bool(flags & 1)))
        expected_size = 104 + 56*mc + 8 + 24*nb + 24*ns + 128*ni + 32*nr + size
        if boot.stat().st_size != expected_size:
            raise ValueError("Linked-image size mismatch")
        return boot.stat().st_size - size, size, segments


def catalog_data(patches, boot_bytes, segments, image_size):
    """Emit only code variants; always validate *all* optional byte guards."""
    lines = [
        "# TLOU 1.11: all verified effects and resolution variants.",
        "# Runtime applies selected patches.bin; no AOT recompilation on switch.",
        "# Mutually exclusive resolution choices; independent visual effects.",
    ]
    group_names = []
    code_count = 0
    data_count = 0
    ranges = []  # for overlap detection across independent patch groups
    groups = [("resolution_" + res, "resolution",
               patches.scene_aim_resolution_patches_111(res))
              for res in RESOLUTIONS]
    groups += [("effect_" + effect, "",
                patches.PERFORMANCE_EFFECTS_111[effect]) for effect in EFFECTS]
    for group, exclusive, spans in groups:
        lines.append("group " + group + (" excl=" + exclusive if exclusive else ""))
        group_names.append(group)
        group_code = 0
        for addr, before_hex, after_hex in spans:
            old = bytes.fromhex(before_hex)
            if not old or addr < 0 or addr + len(old) > image_size:
                raise ValueError("Patch outside verified TLOU 1.11 image: " + group)
            original = boot_bytes(addr, len(old))
            if original != old:
                raise ValueError("Original patch guard mismatch in " + group +
                                 " at +" + hex(addr) + "; refusing AOT")
            if after_hex is None:
                continue
            replacement = bytes.fromhex(after_hex)
            if len(replacement) != len(old):
                raise ValueError("Patch changes original code layout: " + group)
            segment = [item for item in segments
                       if item[0] <= addr and addr + len(old) <= item[1]]
            if len(segment) != 1:
                raise ValueError("Patch straddles linked-image segment: " + group)
            if not segment[0][2]:
                # Data constants are written into guest memory at game launch.
                # No alternate native code is needed for a guest memory load.
                data_count += 1
                continue
            for other_start, other_end, other_group, other_excl in ranges:
                if addr < other_end and other_start < addr + len(replacement):
                    if not (exclusive and exclusive == other_excl):
                        raise ValueError("Overlapping independent patch groups: " +
                                         group + " / " + other_group)
            ranges.append((addr, addr + len(replacement), group, exclusive))
            lines.append(f"w {addr:x} {replacement.hex()}")
            group_code += 1
            code_count += 1
        if not group_code:
            raise ValueError("Patch option has no compiled code variant: " + group)
    if len(group_names) != len(EFFECTS) + len(RESOLUTIONS):
        raise ValueError("Incomplete TLOU patch variant catalog")
    return "\n".join(lines) + "\n", group_names, code_count, data_count


def create_catalog(baseline_boot: Path, eboot_digest: str, destination: Path):
    patches = load_patches()
    if eboot_digest not in patches.RESOLUTION_111_SELF_SHA256:
        raise ValueError("Dynamic 1.11 variants require a SHA-verified executable")
    start, image_size, segments = boot_layout(baseline_boot)
    with baseline_boot.open("rb") as f:
        def read(offset, length):
            f.seek(start + offset)
            return f.read(length)
        catalog, groups, codes, data = catalog_data(patches, read, segments, image_size)
    destination.parent.mkdir(parents=True, exist_ok=True)
    tmp = destination.with_suffix(".tmp")
    tmp.write_text(catalog, encoding="ascii")
    tmp.replace(destination)
    return {
        "profile": PROFILE,
        "sha256": hashlib.sha256(catalog.encode("ascii")).hexdigest(),
        "groups": groups,
        "code_writes": codes,
        "guest_data_writes": data,
        "catalog": str(destination),
    }
