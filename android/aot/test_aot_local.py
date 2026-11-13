#!/usr/bin/env python3
"""No game files: synthetic G3PROBE5 image tests for the local AOT adapter."""
import importlib.util
import json
import struct
import tempfile
import unittest
import hashlib
from unittest.mock import patch
from pathlib import Path

spec = importlib.util.spec_from_file_location("tlou_aot_local", Path(__file__).with_name("aot_local.py"))
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def fake_boot(path):
    image = bytes(range(256)) * 64
    with path.open("wb") as f:
        f.write(struct.pack("<8s6Q", b"G3PROBE5", len(image), 100, 3, 0, 0, 0))
        f.write(struct.pack("<Q", 24))  # procparam
        f.write(struct.pack("<4Q", 0, 0, 0, 0))
        f.write(struct.pack("<Q", 2))
        f.write(struct.pack("<7Q", 4096, 512, 4096, 0, 0, 0, 0))
        f.write(struct.pack("<7Q", 8192, 512, 8192, 0, 0, 0, 0))
        f.write(struct.pack("<Q", 0))  # bindings
        for seg in ((0, 1024, 5), (4096, 512, 5), (8192, 512, 5)):
            f.write(struct.pack("<3Q", *seg))
        f.write(image)


def self_and_boot(root: Path):
    source = root / "eboot.bin"
    boot = root / "boot-linked.bin"
    code = b"\x90" * 16 + b"\x64\x48\x8b\x04\x25\x00\x00\x00\x00" + b"\xc3" * 23
    data = bytearray(256 + len(code))
    data[:4] = b"O\x15=\x1d"
    struct.pack_into("<H", data, 24, 1)
    struct.pack_into("<4Q", data, 32, 0x800, 256, len(code), len(code))
    base = 64
    data[base:base+7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<H", data, base+18, 62)
    struct.pack_into("<Q", data, base+32, 64)
    struct.pack_into("<HH", data, base+54, 56, 1)
    struct.pack_into("<II6Q", data, base+64, 1, 5, 0, 0x400, 0,
                     len(code), len(code), 4096)
    data[256:] = code
    source.write_bytes(data)
    image = bytearray(16384)
    image[0x400:0x400+len(code)] = code.replace(
        b"\x64\x48\x8b\x04\x25\x00\x00\x00\x00",
        b"\x65\x48\x8b\x04\x25\x00\x00\x00\x00")
    with boot.open("wb") as b:
        b.write(struct.pack("<8s6Q", b"G3PROBE5", len(image), 0x400, 3, 0, 0, 0))
        b.write(struct.pack("<Q", 24))
        b.write(struct.pack("<4Q", 0, 0, 0, 0))
        b.write(struct.pack("<Q", 2))
        for row in ((4096,512,4096,0,0,0,0),(8192,512,8192,0,0,0,0)):
            b.write(struct.pack("<7Q",*row))
        b.write(struct.pack("<Q",0))
        for row in ((0,2048,5),(4096,512,5),(8192,512,5)):
            b.write(struct.pack("<3Q",*row))
        b.write(image)
    return source, boot, len(code)



def synthetic_switch_boot(path: Path) -> bytes:
    """Sparse BBPROBE5 with invented data, matching the known switch layout.

    Replaces the signature digest in tests to avoid including any game's code.
    """
    fake_boot(path)
    initial_size = path.stat().st_size
    image_start = initial_size - 16384
    size = mod.TLOU_111_SWITCH_TABLE + 1024
    signature = bytes(range(mod.TLOU_111_SWITCH_SIGNATURE_LEN))
    targets = (0xB5A041, 0xB5A0BD, 0xB5A127, 0xB5A1E8,
               0xB5A252, 0xB5A280, 0xB5A2B6, 0xB5A012,
               0xB5A012, 0xB5A2E4, 0xB5A33F)
    table = struct.pack("<11i", *(target-mod.TLOU_111_SWITCH_TABLE for target in targets))
    with path.open("r+b") as f:
        f.seek(8)
        f.write(struct.pack("<Q", size))
        f.truncate(image_start + size)
        f.seek(image_start + mod.TLOU_111_SWITCH_PC)
        f.write(signature)
        f.seek(image_start + mod.TLOU_111_SWITCH_TABLE)
        f.write(table)
    return signature


class TestBridge(unittest.TestCase):

    def test_native_111_animation_patch_isolated_and_guarded(self):
        # Synthetic header and invented filler, NO original PS4 data.
        import importlib.util
        profile_path = Path(__file__).resolve().parents[1] / "tlou_patches.py"
        spec = importlib.util.spec_from_file_location("tlou_test_patch_profile", profile_path)
        profile = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(profile)
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "original.bin"
            fake_boot(boot)
            initial = boot.stat().st_size
            start = initial - 16384
            size = 0xC9FC61 + 128
            sites = profile.BASE_PATCHES_111["invalid-animation-output-pointer"]
            with boot.open("r+b") as stream:
                stream.seek(8)
                stream.write(struct.pack("<Q", size))
                stream.truncate(start + size)
                for addr, original, replacement in sites:
                    self.assertEqual(len(original), len(replacement))
                    stream.seek(start + addr)
                    stream.write(bytes.fromhex(original))
            before = hashlib.sha256(boot.read_bytes()).hexdigest()
            result = root / "native-boot-linked.bin"
            patched = mod.prepare_native_animation_patch(
                boot, mod.TLOU_111_SWITCH_SHA256, result)
            self.assertEqual(patched, ["invalid-animation-output-pointer"])
            self.assertEqual(before, hashlib.sha256(boot.read_bytes()).hexdigest(),
                             "native compilation must not modify original FEX boot")
            self.assertEqual(mod.check_boot(result), mod.check_boot(boot))
            with boot.open("rb") as old, result.open("rb") as native:
                for addr, original, replacement in sites:
                    old.seek(start + addr)
                    native.seek(start + addr)
                    self.assertEqual(old.read(len(bytes.fromhex(original))),
                                     bytes.fromhex(original))
                    self.assertEqual(native.read(len(bytes.fromhex(replacement))),
                                     bytes.fromhex(replacement))
            # Wrong version must NOT produce the known 1.11 patch.
            self.assertEqual(mod.prepare_native_animation_patch(
                boot, "e5f0aa322f60bd90637cc10d83de08c77885b1921084021c630e04ed3d6febf7",
                result), [])
            self.assertFalse(result.exists())
            # Any mismatching opcode must fail closed without creating image.
            with boot.open("r+b") as stream:
                stream.seek(start + sites[0][0])
                stream.write(b"\xcc")
            with self.assertRaisesRegex(ValueError, "guard mismatch"):
                mod.prepare_native_animation_patch(
                    boot, mod.TLOU_111_SWITCH_SHA256, result)
            self.assertFalse(result.exists())

    def test_batch_known_1_11_jump_cases_before_codegen(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            boot = root / "bb-boot"
            signature = synthetic_switch_boot(boot)
            with patch.object(mod, "TLOU_111_SWITCH_SIGNATURE_SHA256",
                              hashlib.sha256(signature).hexdigest()):
                targets = mod.known_switch_entries(boot, mod.TLOU_111_SWITCH_SHA256)
                self.assertEqual(len(targets), 10)
                self.assertIn(0xB5A041, targets)
                self.assertIn(0xB5A33F, targets)
                self.assertEqual(mod.known_switch_entries(boot, "other-eboot"), [])
                misses = root / "misses.log"
                misses.write_text(
                    "# old crashes are not entry addresses\n"
                    "outside 0xffffffffffffffff kind=outside\n"
                    "miss +0xb5a041 kind=jump\n"
                    "miss +0xb5a041 kind=jump\n"
                    "miss +0xb5a111 kind=jump\n"
                    "miss +0xffffffffffffffff kind=outside\n")
                out = root / "extra-entries.txt"
                seed, extra = mod.prepare_extra_entries(
                    boot, mod.TLOU_111_SWITCH_SHA256, misses, out)
                self.assertEqual(seed, targets)
                self.assertEqual(extra, sorted(set(targets + [0xB5A111])))
                self.assertEqual(
                    out.read_text().splitlines(),
                    [f"+0x{x:x}" for x in extra])

    def test_switch_signature_mismatch_fails_early(self):
        with tempfile.TemporaryDirectory() as td:
            boot = Path(td) / "bb-boot"
            synthetic_switch_boot(boot)
            with self.assertRaisesRegex(ValueError, "verified switch dispatch"):
                mod.known_switch_entries(boot, mod.TLOU_111_SWITCH_SHA256)

    def test_old_outside_misses_do_not_trigger_recompilation(self):
        with tempfile.TemporaryDirectory() as td:
            boot = Path(td) / "boot"
            fake_boot(boot)
            misses = Path(td) / "misses.log"
            misses.write_text(
                "outside 0xffffffffffffffff kind=outside\n"
                "miss +0xffffffffffffffff kind=outside\n")
            out = Path(td) / "verified.txt"
            seeds, all_entries = mod.prepare_extra_entries(boot, "not-1.11", misses, out)
            self.assertEqual((seeds, all_entries), ([], []))
            self.assertFalse(out.exists())

    def test_native_fiber_profile_uses_exact_register_reload(self):
        self.assertEqual(
            mod.NATIVE_FIBER_CPU_FLAGS,
            ("--tso", "--resume-entries", "--no-callee-saved"))
        # Existing no-flag shards must not be mistaken for a fiber-safe build.
        self.assertNotIn("--flags-at-calls", mod.NATIVE_FIBER_CPU_FLAGS)

    def test_header_only_and_never_modify_input(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            original = base / "boot-linked.bin"
            fake_boot(original)
            (base / "link.json").write_text(json.dumps({"modules": [
                {"file": "libc.prx"}, {"file": "libSceFios2.prx"}]}))
            before = original.read_bytes()
            new = mod.stage_boot(original, base / "work")
            after = new.read_bytes()
            self.assertEqual(original.read_bytes(), before)
            self.assertEqual(after[:8], b"BBPROBE5")
            self.assertEqual(after[8:], before[8:])
            self.assertEqual(mod.check_boot(new)["modules"], 2)
            self.assertEqual(json.loads((new.parent / "link.json").read_text())["modules"][0]["file"], "libc.prx")

    def test_boot_executable_bytes_match(self):
        with tempfile.TemporaryDirectory() as td:
            source, boot, n = self_and_boot(Path(td))
            self.assertEqual(mod.verify_eboot_image(source,boot), n)

    def test_reject_boot_from_other_eboot(self):
        with tempfile.TemporaryDirectory() as td:
            source, boot, _ = self_and_boot(Path(td))
            raw = source.read_bytes()
            source.write_bytes(raw[:-1]+bytes([raw[-1]^1]))
            with self.assertRaisesRegex(ValueError, "different eboot"):
                mod.verify_eboot_image(source,boot)

    def test_choose_prepared_update_eboot(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td) / "base"
            base.mkdir()
            (base / "eboot.bin").write_bytes(b"1.00")
            prepared = Path(td) / "tlou-prepared"
            (prepared / "tlou-view").mkdir(parents=True)
            (prepared / "tlou-view/eboot.bin").write_bytes(b"1.11")
            boot = prepared / "boot-linked.bin"
            boot.write_bytes(b"placeholder")
            selected = mod.effective_game_folder(base, boot)
            self.assertEqual((selected / "eboot.bin").read_bytes(), b"1.11")
            self.assertEqual((base / "eboot.bin").read_bytes(), b"1.00")

    def test_fail_closed_without_prepared_view(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td) / "base"
            base.mkdir()
            prepared = Path(td) / "tlou-prepared"
            prepared.mkdir()
            (prepared / "device-ready.json").write_text("{}")
            with self.assertRaisesRegex(ValueError, "no tlou-view"):
                mod.effective_game_folder(base, prepared / "boot-linked.bin")

    def test_reject_bad_magic(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "bad.bin"
            fake_boot(path)
            old = path.read_bytes()
            path.write_bytes(b"INVALID!" + old[8:])
            with self.assertRaisesRegex(ValueError, "Invalid boot"):
                mod.check_boot(path)

    def test_reject_truncated_image(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "bad.bin"
            fake_boot(path)
            path.write_bytes(path.read_bytes()[:-5])
            with self.assertRaisesRegex(ValueError, "mismatch"):
                mod.check_boot(path)

    def test_reject_wrong_module_order(self):
        with tempfile.TemporaryDirectory() as td:
            base = Path(td)
            path = base / "boot-linked.bin"
            fake_boot(path)
            (base / "link.json").write_text(json.dumps({"modules": [
                {"file": "libSceFios2.prx"}, {"file": "libc.prx"}]}))
            with self.assertRaisesRegex(ValueError, "module order"):
                mod.stage_boot(path, base / "work")


if __name__ == "__main__":
    unittest.main()
