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
