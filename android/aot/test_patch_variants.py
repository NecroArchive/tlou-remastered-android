#!/usr/bin/env python3
"""Verify all 1.11 native patch selectors before an installable APK is built."""
from pathlib import Path
import importlib.util
import unittest

ROOT=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location("tlou_patch_variants",ROOT/"patch_variants.py")
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
patches=mod.load_patches()

def fixture():
    expected={}
    groups=[patches.scene_aim_resolution_patches_111(r) for r in mod.RESOLUTIONS]
    groups += list(patches.PERFORMANCE_EFFECTS_111.values())
    for group in groups:
        for at, old, new in group:
            key=(at,len(bytes.fromhex(old)))
            if key in expected and expected[key]!=bytes.fromhex(old):
                raise AssertionError("Conflicting known source guards")
            expected[key]=bytes.fromhex(old)
    code=[(0,0x1100000,True),(0x1100000,0x1200000,False)]
    return expected, code

class AllPatchVariants(unittest.TestCase):
    def test_every_verified_effect_and_resolution_has_a_native_variant(self):
        old,segments=fixture()
        catalog,groups,code_count,data_count=mod.catalog_data(
            patches,lambda a,n:old.get((a,n),b""),segments,0x1200000)
        self.assertEqual(len(groups),11)
        self.assertEqual(len(set(groups)),11)
        self.assertEqual(set(groups),{
            "resolution_720","resolution_540","resolution_360",
            *(f"effect_{x}" for x in mod.EFFECTS)})
        self.assertEqual(catalog.count("excl=resolution"),3)
        self.assertEqual(catalog.count("\ngroup effect_"),8)
        self.assertGreater(code_count,30)
        self.assertGreater(data_count,0)
        self.assertIn("w a63bb6 30c9",catalog)  # omni classifier
        self.assertIn("w a6445a 4530ed",catalog)  # spot classifier
        self.assertIn("w ab0600 ",catalog)      # resolution selector
        self.assertNotIn("w 116bf10 ",catalog)  # guest data, not native function
        self.assertNotIn("w 115fb9c ",catalog)  # guest data
        self.assertNotIn("--no-dirty-check",catalog)

    def test_refuses_corrupt_or_unsupported_patch_bytes(self):
        originals,segments=fixture()
        corrupted=dict(originals)
        corrupted[(0xA63BB6,2)]=b"\x00"*2
        with self.assertRaisesRegex(ValueError,"guard mismatch"):
            mod.catalog_data(patches,lambda a,n:corrupted.get((a,n),b""),
                             segments,0x1200000)

    def test_missing_verification_segment_rejected(self):
        originals,segments=fixture()
        with self.assertRaisesRegex(ValueError,"straddles linked-image segment"):
            mod.catalog_data(patches,lambda a,n:originals.get((a,n),b""),
                             [(0,0xA60000,True)],0x1200000)

    def test_all_compile_options_are_independent_of_initial_user_preferences(self):
        self.assertEqual(mod.PROFILE,"tlou-111-variants-v3")
        # The frontend must bake only stability fixes and hand optional groups
        # to --variants; neither +720 nor +omni may be hardcoded at build time.
        code=(ROOT/"aot_local.py").read_text()
        self.assertIn("catalog_mode = version == \"1.11\" and native_options is not None",code)
        self.assertIn("baseline = {\"job_sleep\": True} if catalog_mode else options",code)
        self.assertIn('command += ["--variants", variant_info["catalog"]]',code)
        self.assertNotIn('AOT_111_PERFORMANCE_FLAGS = ("-O2", "--no-dirty-check")',code)
        self.assertIn('AOT_111_PERFORMANCE_FLAGS = ("-O2",)',code)

if __name__=="__main__":
    unittest.main()
