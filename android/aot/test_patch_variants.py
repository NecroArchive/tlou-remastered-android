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
    def test_100_native_catalog_covers_base_patches_and_all_user_options(self):
        # The original 1.00 AOT compiled all x86 code but had zero variants;
        # the first guest patch entered +0x946180 and stopped with code 22.
        originals={}
        groups=list(patches.BASE_PATCHES.values())
        groups += [patches.scene_aim_resolution_patches(r) for r in mod.RESOLUTIONS]
        groups += [patches.PERFORMANCE_EFFECTS[r] for r in mod.EFFECTS_100]
        for spans in groups:
            for address,old,new in spans:
                key=(address,len(bytes.fromhex(old)))
                before=bytes.fromhex(old)
                if key in originals:
                    self.assertEqual(originals[key],before)
                originals[key]=before
        catalog,names,writes,data=mod.catalog_data_100(
            patches,lambda a,n:originals.get((a,n),b""),
            [(0,0x1da8050,True)],0x1da8050)
        self.assertEqual(mod.PROFILE_100,"tlou-100-variants-v1")
        self.assertEqual(len(names),13)
        self.assertEqual(len(set(names)),13)
        self.assertIn("group base_null_component_transforms",catalog)
        self.assertIn("group base_static_ray_hit_submesh_bounds",catalog)
        self.assertIn("group base_job_sleep",catalog)
        self.assertIn("group resolution_720 excl=resolution",catalog)
        self.assertIn("group effect_ssao",catalog)
        self.assertIn("w 946363 00",catalog)
        self.assertGreater(writes,30)
        self.assertEqual(data,0)
        compiler=(ROOT/"aot_local.py").read_text()
        self.assertIn("catalog_mode_100 = version == \"1.00\"",compiler)
        self.assertIn("create_catalog_100(",compiler)
        # 1.00 must reuse verified intermediate shards when repairing
        # missing native entry points, not force a full recompilation.
        self.assertIn('"--progress", "--resume"',compiler)
        # --resume is removed ONLY if the legacy linked-code signature
        # differs and the existing compiler output has never passed an
        # explicit --check-entries proof. No perpetual precompile block.
        self.assertIn('if full_rebuild_100:\\n            command.remove("--resume")'.replace("\\\\n","\\n"),compiler)
        self.assertIn('"repair_previous_100_aot": repair_100',compiler)
        self.assertIn('"resume_enabled": "--resume" in command',compiler)
        self.assertIn('"full_rebuild_100": full_rebuild_100',compiler)

    def test_100_refuses_any_unverified_patch_bytes(self):
        originals={}
        for spans in [*patches.BASE_PATCHES.values(),
                      *[patches.scene_aim_resolution_patches(r)
                        for r in mod.RESOLUTIONS],
                      *[patches.PERFORMANCE_EFFECTS[k]
                        for k in mod.EFFECTS_100]]:
            for addr,old,new in spans:
                originals[(addr,len(bytes.fromhex(old)))]=bytes.fromhex(old)
        originals[(0x946363,1)]=b"\\xff"
        with self.assertRaisesRegex(ValueError,"byte guard mismatch"):
            mod.catalog_data_100(
                patches,lambda a,n:originals.get((a,n),b""),
                [(0,0x1da8050,True)],0x1da8050)

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
