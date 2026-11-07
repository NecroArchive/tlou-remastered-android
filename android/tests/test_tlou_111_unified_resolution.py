"""01.11 SELF-specific guarded unified resolution patch contract."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tlou_patches as patches

KNOWN="3cfa475f88ca68485b6b9230e62b84e195cdbe19e6f7295c110b8dcf8f20884e"


class Update111UnifiedResolution(unittest.TestCase):
    def test_only_uploaded_original_self_is_opted_in(self):
        self.assertIn(KNOWN,patches.RESOLUTION_111_SELF_SHA256)
        self.assertEqual(len(patches.RESOLUTION_111_SELF_SHA256),2)
        self.assertIn("31f3b0d120803878d10a7184d139c545de334394dd7dd6ca6058a7c995e484a7",
                      patches.RESOLUTION_111_SELF_SHA256)
        with self.assertRaisesRegex(ValueError,"01.11"):
            patches.selected_options({"resolution":"720"},"01.11")
        # Different SHA, same verified 24 patch locations. Both original
        # and pre-patched pointer-guard variant support unified resolutions.
        other="31f3b0d120803878d10a7184d139c545de334394dd7dd6ca6058a7c995e484a7"
        for mode in patches.LOW_RES_SCENE:
            expected=patches.selected_options({"resolution":mode},"01.11",KNOWN)
            actual=patches.selected_options({"resolution":mode},"01.11",other)
            self.assertEqual(actual,expected)
        with self.assertRaisesRegex(ValueError,"01.11"):
            patches.selected_options({"resolution":"720"},"01.11","unknown-self-hash")

    def test_verified_full_hud_post_and_aim_sites_are_all_replaced(self):
        self.assertEqual(len(patches.RESOLUTION_111_GUARDS),8)
        for name,(width,height) in patches.LOW_RES_SCENE.items():
            changes=patches.selected_options({"resolution":name},"01.11",KNOWN)[
                "resolution-"+name]
            self.assertEqual(len(changes),8)
            self.assertEqual({a for a,_,_ in changes},set(patches.RESOLUTION_111_GUARDS))
            for addr,before,after in changes:
                self.assertEqual(before,patches.RESOLUTION_111_GUARDS[addr])
                self.assertEqual(len(before),len(after))
                self.assertNotEqual(before,after)
            by_addr={addr:bytes.fromhex(after) for addr,before,after in changes}
            # Both C++ instances and their default vector constants use the
            # exact same dimensions. Quarter and half dimensions follow.
            self.assertEqual(struct.unpack("<4I",by_addr[0x116BF10]),
                             (width,height,width//8,height//4))
            self.assertEqual(by_addr[0x116BF30],by_addr[0x116BF10])
            self.assertEqual(by_addr[0xAB05D6][3:7],struct.pack("<I",width))
            self.assertEqual(by_addr[0xAB05D6][10:14],struct.pack("<I",height))
            self.assertEqual(by_addr[0xAB095D][3:7],struct.pack("<I",width))
            self.assertEqual(by_addr[0xAB095D][10:14],struct.pack("<I",height))
            self.assertEqual(by_addr[0xAD478C],
                b"\xb9"+struct.pack("<I",width)+b"\x41\xb8"+struct.pack("<I",height))
            self.assertEqual(by_addr[0xAD47E7],
                b"\xb9"+struct.pack("<I",width//2)+b"\x41\xb8"+struct.pack("<I",height//2))
            self.assertAlmostEqual(struct.unpack_from("<f",by_addr[0x115FB9C])[0],1/height,places=9)
            self.assertEqual(struct.unpack_from("<f",by_addr[0x115FB9C],4)[0],height)
            # Byte-exact C++ control flow: jmp from 0xAB060F to the original
            # stores and aspect ratio calculation at 0xAB062A.
            code=by_addr[0xAB0600]
            self.assertEqual(code[:1],b"\xb8")
            self.assertEqual(code[5:6],b"\xb9")
            self.assertEqual(code[10:11],b"\xe9")
            self.assertEqual(0xAB0600+15+struct.unpack("<i",code[-4:])[0],0xAB062A)

    def test_old_100_profile_is_unchanged_and_effects_remain_111_specific(self):
        for mode in patches.LOW_RES_SCENE:
            old=patches.selected_options({"resolution":mode},"01.00")
            self.assertEqual(old["resolution-"+mode],patches.scene_aim_resolution_patches(mode))
        self.assertIn("frame-timeout",patches.base_patches_for({},"01.11"))
        effects=patches.selected_options({"ssao":True,"resolution":"720"},"01.11",KNOWN)
        self.assertEqual(effects["disable-ssao"],patches.PERFORMANCE_EFFECTS_111["ssao"])


if __name__=="__main__":
    unittest.main()
