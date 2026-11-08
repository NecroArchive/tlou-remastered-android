"""01.11 graphics toggles must survive saved-settings reloading.

Synthetic-only CI contract. Actual original-byte guards of the added consumer
instructions were independently checked on both user-owned 01.11 SELF files.
"""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tlou_patches as patch


EXPECTED_CONSUMERS = {
    "ssao": {0xF907},
    "motion_blur": {0xACB868, 0xAE09C0, 0xAE0D7F, 0xAE0DD6, 0xAE0F25, 0xB8781E, 0x2F2035},
    "depth_of_field": {0xA86FFE, 0xB87611, 0xB88604},
    "bloom": {0xA86F8B},
}
BRANCHES = {0xF907, 0xACB868, 0xAE09C0, 0xAE0D7F,
            0xAE0DD6, 0xAE0F25, 0xB87611, 0xB88604}


def branch_target(address, instruction):
    if instruction[0] in (0x74, 0xEB) and len(instruction) == 2:
        return address + 2 + struct.unpack("<b", instruction[1:2])[0]
    if instruction[:2] == bytes((0x0F, 0x84)) and len(instruction) == 6:
        return address + 6 + struct.unpack("<i", instruction[2:])[0]
    if instruction[0] == 0xE9 and len(instruction) == 6:
        assert instruction[-1] == 0x90
        return address + 5 + struct.unpack("<i", instruction[1:5])[0]
    raise AssertionError(f"Unrecognized conditional-to-unconditional patch at {address:#x}")


class PersistentUpdate111EffectTests(unittest.TestCase):
    def test_each_reloaded_flag_has_runtime_consumer_patch(self):
        all_sites = {}
        for effect, required in EXPECTED_CONSUMERS.items():
            patches = patch.selected_options({effect: True}, "01.11")
            group = patches["disable-" + effect]
            sites = {addr: (old, new) for addr, old, new in group}
            self.assertTrue(required.issubset(sites.keys()), effect)
            for addr, (old, new) in sites.items():
                self.assertNotIn(addr, all_sites)
                all_sites[addr] = effect
                if new is not None:
                    self.assertEqual(len(bytes.fromhex(old)), len(bytes.fromhex(new)))
            self.assertNotIn("disable-" + effect,
                             patch.selected_options({effect: False}, "01.11"))

    def test_off_branches_preserve_original_disabled_destinations(self):
        all_sites = {addr: (old, new)
                     for changes in patch.PERFORMANCE_EFFECTS_111.values()
                     for addr, old, new in changes}
        for addr in BRANCHES:
            old, new = all_sites[addr]
            self.assertEqual(branch_target(addr, bytes.fromhex(old)),
                             branch_target(addr, bytes.fromhex(new)))

    def test_getters_disable_one_option_without_removing_other_flags(self):
        rows = {addr: (old, new)
                for changes in patch.PERFORMANCE_EFFECTS_111.values()
                for addr, old, new in changes}
        for addr in (0xA86FFE, 0xA86F8B):
            self.assertEqual(rows[addr], ("84c0", "30c0"))
        # The bloom constructor still writes adjacent bytes 01, 01, 01;
        # never zero four bytes (which would also turn off tonemapping).
        self.assertEqual(rows[0xAEB202][1], "c783b401000001000101")
        # The motion-blur bitpack zeroes its disabled contribution only;
        # 0xB87801 already sets r15d to zero before this consumer.
        self.assertEqual(rows[0xB8781E], ("410f44c7", "4489f890"))
        # The proven re-enabler writes through r13 to the global
        # 0x181A770 render-state object; same-offset unrelated objects
        # must not be patched merely because they use a 0x16D field.
        self.assertEqual(rows[0x2F2035], ("41c6856d01000001", "41c6856d01000000"))
        self.assertNotIn(0x193B6A, rows)
        self.assertNotIn(0x193BB0, rows)

    def test_100_patches_unaffected(self):
        before = patch.PERFORMANCE_EFFECTS
        for effect in EXPECTED_CONSUMERS:
            selected = patch.selected_options({effect: True}, "01.00")
            self.assertEqual(selected["disable-" + effect], before[effect])
        self.assertEqual(len(patch.scene_aim_resolution_patches("360")), 32)

    def test_111_both_sha_profiles_activate_identical_consumer_patches(self):
        requested = {effect: True for effect in patch.PERFORMANCE_EFFECTS_111}
        requested["resolution"] = "360"
        variants = [patch.selected_options(requested, "01.11", digest)
                    for digest in patch.SELF_SHA256_111]
        self.assertEqual(variants[0], variants[1])
        self.assertEqual(len(variants[0]), 9)


if __name__ == "__main__":
    unittest.main()
