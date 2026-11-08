"""Guard 01.11 optional omni/spot shadow bypasses without disabling light.

Both original SELF builds were verified off-repo; CI verifies the source patch
instructions and makes the synthetic G3PATCH2 writer exercise all guarded
sites. Reaching the same scene in real gameplay is still required before any
FPS or render-correctness claim.
"""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import tlou_patches as p

SHADOWS={
    "omni_light_shadows": (0xA63BBA,),
    "spot_light_shadows": (0xA6445F,0xA652A4),
}
NEIGHBORS={
    "omni_light_shadows": {0xA63BCA:"418a84244f010000"},
    "spot_light_shadows": {
        0xA6446F:"418a842450010000",
        0xA652B8:"418a842450010000",
    },
}

class OptionalLightShadows111(unittest.TestCase):
    def test_opt_in_and_111_only(self):
        for name in SHADOWS:
            self.assertNotIn("disable-"+name,p.selected_options({name:False},"01.11"))
            self.assertNotIn("disable-"+name,p.selected_options({name:True},"01.00"))
            expected=p.PERFORMANCE_EFFECTS_111[name]
            for digest in p.SELF_SHA256_111:
                self.assertEqual(p.selected_options({name:True},"01.11",digest)["disable-"+name],expected)
            self.assertTrue(any(new is not None for _,_,new in expected))

    def test_consumer_reads_forced_off_nonshadow_routes_left_untouched(self):
        for name,consumers in SHADOWS.items():
            instructions={at:(before,after) for at,before,after in p.PERFORMANCE_EFFECTS_111[name]}
            for at in consumers:
                old,new=instructions[at]
                self.assertTrue(old.startswith("418a8424"))
                self.assertEqual(new, "30c0" + "90"*6)
                self.assertEqual(len(bytes.fromhex(old)),len(bytes.fromhex(new)))
            for at,original in NEIGHBORS[name].items():
                self.assertEqual(instructions[at],(original,None))
        self.assertEqual(p.PERFORMANCE_EFFECTS_111["omni_light_shadows"][0],
            (0xA638C4,"4c8d25a56edb00",None))

    def test_no_overlap_with_1_11_existing_effects_or_stability(self):
        current=[]
        for name,group in p.PERFORMANCE_EFFECTS_111.items():
            for at,old,new in group:
                if new is None:continue
                span=(at,at+len(bytes.fromhex(old)))
                for prev in current:
                    self.assertTrue(span[1]<=prev[0] or prev[1]<=span[0],(name,span,prev))
                current.append(span)
        for group in p.BASE_PATCHES_111.values():
            for at,old,new in group:
                if new is None:continue
                span=(at,at+len(bytes.fromhex(old)))
                for prev in current:
                    self.assertTrue(span[1]<=prev[0] or prev[1]<=span[0],(span,prev))

if __name__=="__main__":
    unittest.main()
