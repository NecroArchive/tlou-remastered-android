"""Synthetic ELF tests for guarded base-executable patches; no game bytes shipped."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tlou_patches as profiles
from test_tlou_bionic import m


class BaseExecutablePatches(unittest.TestCase):
    def elf(self, path):
        end = max(at + len(original) // 2 for changes in profiles.BASE_PATCHES.values()
                  for at, original, _ in changes)
        raw = bytearray(end + 0x1000)
        struct.pack_into("<Q", raw, 0x20, 0x40)
        struct.pack_into("<HH", raw, 0x36, 56, 1)
        struct.pack_into("<IIQQQQQQ", raw, 0x40, 1, 5, 0x1000, 0, 0, end, end, 0x1000)
        for changes in profiles.BASE_PATCHES.values():
            for at, original, _ in changes:
                raw[at+0x1000:at+0x1000+len(original)//2] = bytes.fromhex(original)
        path.write_bytes(raw)

    def test_base_changes_and_rip_constants(self):
        with tempfile.TemporaryDirectory() as temp:
            elf, out = Path(temp)/"eboot.elf", Path(temp)/"patches.bin"
            self.elf(elf)
            patcher = m.load_patch_module(Path(__file__).resolve().parents[2])
            patcher.image_patches(elf, out, profiles.BASE_PATCHES)
            self.assertEqual(struct.unpack_from("<Q", out.read_bytes(), 16)[0], 9)
            for at, _, replacement in profiles.BASE_PATCHES["frame-timeout"][:2]:
                value = bytes.fromhex(replacement)
                target = at + 8 + struct.unpack_from("<i", value, 4)[0]
                raw = elf.read_bytes()
                fmt = "f" if at == 0x99285A else "d"
                self.assertEqual(struct.unpack_from("<"+fmt, raw, target+0x1000)[0], 20.0)

    def test_crash_trampoline_targets_and_padding_guard(self):
        changes=profiles.BASE_PATCHES['null-component-transforms']
        entries={at:(bytes.fromhex(old),bytes.fromhex(new) if new else None) for at,old,new in changes}
        old,stub=entries[0xf6be63]
        self.assertEqual(old,b'\x90'*13)
        self.assertEqual(0xf6be63+5+struct.unpack('b',stub[4:5])[0],0xf6be62)
        self.assertEqual(0xf6be63+13+struct.unpack('b',stub[-1:])[0],0xf6be76)
        jump=entries[0xf6be70][1]
        self.assertEqual(0xf6be70+5+struct.unpack('<i',jump[1:5])[0],0xf6be63)

    def test_mismatching_worker_guard_rejects_entire_sleep_patch(self):
        with tempfile.TemporaryDirectory() as temp:
            elf, out = Path(temp)/"eboot.elf", Path(temp)/"patches.bin"
            self.elf(elf)
            with elf.open("r+b") as f:
                f.seek(0xAACA40+0x1000)
                f.write(b"\x90")
            patcher = m.load_patch_module(Path(__file__).resolve().parents[2])
            patcher.image_patches(elf, out, profiles.BASE_PATCHES)
            self.assertEqual(struct.unpack_from("<Q", out.read_bytes(), 16)[0], 6)

    def test_low_resolution_game_hud_aim_and_post_share_one_coordinate_space(self):
        # Full-size/half-size postprocess images were incorrectly pinned to
        # 1920x1080 / 960x540, causing 1080p color alongside 720p depth.
        for profile, (width, height) in profiles.LOW_RES_SCENE.items():
            baseline = dict((at, (original, replacement))
                            for at, original, replacement in
                            profiles.PERFORMANCE_RESOLUTIONS[profile])
            patches = profiles.scene_aim_resolution_patches(profile)
            self.assertEqual(len(patches), len(baseline))
            changed = {at for at, old, new in patches if new != baseline[at][1]}
            required = {*profiles.GAME_UI_NATIVE_OVERRIDES,
                        *profiles.POSTPROCESS_FULL_SITES,
                        *profiles.POSTPROCESS_HALF_SITES,
                        profiles.AIM_NORMALIZATION_ADDRESS}
            self.assertEqual(changed, required)
            updated = {at: new for at, old, new in patches}
            for at, original, replacement in patches:
                self.assertEqual(original, baseline[at][0])
                self.assertEqual(len(replacement), len(original))
                if at in profiles.GAME_UI_NATIVE_OVERRIDES:
                    self.assertIn(struct.pack("<I", width).hex(), replacement)
                    self.assertIn(struct.pack("<I", height).hex(), replacement)
                    self.assertNotIn("80070000", replacement)
                    self.assertNotIn("38040000", replacement)
            for at in profiles.POSTPROCESS_FULL_SITES:
                self.assertIn("b9"+struct.pack("<I",width).hex()+
                              "41b8"+struct.pack("<I",height).hex(), updated[at])
                self.assertNotIn("b98007000041b838040000", updated[at])
            for at in profiles.POSTPROCESS_HALF_SITES:
                self.assertIn("b9"+struct.pack("<I",width//2).hex()+
                              "41b8"+struct.pack("<I",height//2).hex(), updated[at])
                self.assertNotIn("b9c003000041b81c020000", updated[at])
            b = bytes.fromhex(updated[profiles.AIM_NORMALIZATION_ADDRESS])
            self.assertAlmostEqual(struct.unpack_from("<f", b, 1)[0], 1/height, places=8)
            self.assertAlmostEqual(struct.unpack_from("<f", b, 10)[0], 1/width, places=8)
            self.assertEqual(profiles.selected_options({"resolution":profile})
                             ["resolution-"+profile], patches)
        self.assertNotIn("resolution-original", profiles.selected_options({"resolution":"original"}))


    def test_low_resolution_rejects_unrecognized_hud_patch(self):
        address = next(iter(profiles.GAME_UI_NATIVE_OVERRIDES))
        original = profiles.GAME_UI_NATIVE_OVERRIDES[address]
        try:
            profiles.GAME_UI_NATIVE_OVERRIDES[address] = "9090909090"
            with self.assertRaises(ValueError):
                profiles.scene_aim_resolution_patches("720")
        finally:
            profiles.GAME_UI_NATIVE_OVERRIDES[address] = original


    def test_job_worker_wait_regression_is_device_scoped(self):
        default=profiles.base_patches_for({})
        conservative=profiles.base_patches_for({"job_sleep":False})
        self.assertIn("job-sleep",default)
        self.assertNotIn("job-sleep",conservative)
        self.assertEqual(set(default)-set(conservative),{"job-sleep"})
        self.assertEqual(profiles.base_patches_for({"job_sleep":True}),default)
        with self.assertRaises(ValueError):
            profiles.base_patches_for({"job_sleep":"false"})

    def test_011_verified_effects_and_resolution_safety(self):
        self.assertEqual(len(profiles.SELF_SHA256_111), 2)
        for digest in profiles.SELF_SHA256_111:
            self.assertEqual(profiles.known_profile(digest), "01.11")
        self.assertEqual(profiles.known_profile(profiles.BASE_SELF_SHA256), "01.00")
        self.assertNotIn("job-sleep", profiles.base_patches_for({"job_sleep":False}, "01.11"))
        self.assertIn("job-sleep", profiles.base_patches_for({"job_sleep":True}, "01.11"))
        for name, changes in profiles.PERFORMANCE_EFFECTS_111.items():
            self.assertEqual(profiles.selected_options({name:True}, "01.11")["disable-"+name], changes)
            for address, before, after in changes:
                self.assertTrue(address > 0 and len(before)%2 == 0)
                if after: self.assertEqual(len(before),len(after))
        for r in ("720","540","360"):
            with self.assertRaisesRegex(ValueError,"01.11 ainda não validado"):
                profiles.selected_options({"resolution":r},"01.11")

    def test_011_all_patch_groups_emit_only_verified_replacements(self):
        """Exercise the actual patch writer with an ELF carrying 01.11 guards.

        The supplied copyrighted SELF files are intentionally NOT committed.
        Their exact instruction guards were checked separately in both files.
        """
        groups = profiles.base_patches_for({"job_sleep": True}, "01.11")
        groups.update(profiles.selected_options({name: True
                      for name in profiles.PERFORMANCE_EFFECTS_111}, "01.11"))
        sites = [item for changes in groups.values() for item in changes]
        end = max(at + len(original) // 2 for at, original, _ in sites)
        with tempfile.TemporaryDirectory() as temp:
            elf, out = Path(temp) / "eboot.elf", Path(temp) / "patches.bin"
            raw = bytearray(end + 0x1000)
            struct.pack_into("<Q", raw, 0x20, 0x40)
            struct.pack_into("<HH", raw, 0x36, 56, 1)
            struct.pack_into("<IIQQQQQQ", raw, 0x40, 1, 5, 0x1000, 0, 0,
                             end, end, 0x1000)
            for at, original, _ in sites:
                original = bytes.fromhex(original)
                # Longer validation guards intentionally overlap an instruction
                # whose shorter form also becomes a replacement.
                raw[at+0x1000:at+0x1000+len(original)] = original
            elf.write_bytes(raw)
            patcher = m.load_patch_module(Path(__file__).resolve().parents[2])
            patcher.image_patches(elf, out, groups)
            expected = sum(replacement is not None
                           for _, _, replacement in sites)
            self.assertEqual(struct.unpack_from("<Q", out.read_bytes(), 16)[0],
                             expected)
            # A mismatched 01.11 graphics group must be rejected in its entirety.
            invalid = groups["disable-motion_blur"][0][0] + 0x1000
            raw[invalid] ^= 1
            elf.write_bytes(raw)
            out.unlink()
            patcher.image_patches(elf, out, groups)
            reduced = expected - sum(replacement is not None for _, _, replacement
                                     in groups["disable-motion_blur"])
            self.assertEqual(struct.unpack_from("<Q", out.read_bytes(), 16)[0],
                             reduced)

    def test_unknown_executable_keeps_existing_version_guards(self):
        with tempfile.TemporaryDirectory() as temp:
            eboot = Path(temp)/"eboot.bin"
            eboot.write_bytes(b"another executable")
            fallback = {"test": []}
            self.assertIs(profiles.select(eboot, fallback), fallback)


if __name__ == "__main__":
    unittest.main()
