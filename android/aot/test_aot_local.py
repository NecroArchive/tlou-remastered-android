#!/usr/bin/env python3
"""No game files: synthetic G3PROBE5 image tests for the local AOT adapter."""
import importlib.util
import json
import struct
import tempfile
import unittest
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


class TestBridge(unittest.TestCase):
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
