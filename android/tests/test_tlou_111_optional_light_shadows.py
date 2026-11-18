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

# The old implementation overwrote the category reads with AL=0.
# The following TEST AL / JE then skipped the ENTIRE light. Redirect the
# shadow classification register BEFORE that check, preserving light output.
REDIRECTS={
    "omni_light_shadows": {0xA63BB6:("84c9","30c9")},
    "spot_light_shadows": {
        0xA6445A:("4584ed","4530ed"),
        0xA652A0:("85d2","31d2"),
    },
}
ORIGINAL_SHADOW_READS={
    "omni_light_shadows": {0xA63BBA:"418a84244d010000"},
    "spot_light_shadows": {
        0xA6445F:"418a84244e010000",
        0xA652A4:"418a84244e010000",
    },
}
NONSHADOW_LIGHT_CONSUMERS={
    "omni_light_shadows": {0xA63BCA:"418a84244f010000"},
    "spot_light_shadows": {
        0xA6446F:"418a842450010000",
        0xA652B8:"418a842450010000",
    },
}

class OptionalLightShadows111(unittest.TestCase):
    def test_opt_in_and_111_only(self):
        for name in REDIRECTS:
            self.assertNotIn("disable-"+name,p.selected_options({name:False},"01.11"))
            self.assertNotIn("disable-"+name,p.selected_options({name:True},"01.00"))
            expected=p.PERFORMANCE_EFFECTS_111[name]
            for digest in p.SELF_SHA256_111:
                self.assertEqual(p.selected_options({name:True},"01.11",digest)["disable-"+name],expected)
            self.assertTrue(any(new is not None for _,_,new in expected))

    def test_shadow_classification_reaches_unshadowed_light_path(self):
        for name,redirects in REDIRECTS.items():
            instructions={at:(before,after) for at,before,after in p.PERFORMANCE_EFFECTS_111[name]}
            for at,(original,redirect) in redirects.items():
                old,new=instructions[at]
                self.assertEqual(old,original)
                self.assertEqual(new,redirect)
                self.assertEqual(len(bytes.fromhex(old)),len(bytes.fromhex(new)))
            # The previous AL=0 patch was followed by TEST AL; JE skip-light,
            # and the following unshadowed consumer was never reached.
            # Preserve shadow read sites completely; their input classifier
            # is now zero and bypasses them.
            for at in ORIGINAL_SHADOW_READS[name]:
                self.assertNotIn(at,instructions)
            for at,original in NONSHADOW_LIGHT_CONSUMERS[name].items():
                old,new=instructions[at]
                self.assertEqual(old,original)
                self.assertEqual(new,"b001"+"90"*6)
                self.assertEqual(len(bytes.fromhex(old)),len(bytes.fromhex(new)))
        self.assertEqual(p.PERFORMANCE_EFFECTS_111["omni_light_shadows"][0],
            (0xA638C4,"4c8d25a56edb00",None))

    def test_original_control_flow_explains_dark_scene_regression(self):
        # Omni: TEST CL; JE unshadowed; MOV AL,shadow; TEST AL; JE skip;
        #       MOV AL,unshadow; OR AL,CL; JE skip; CALL light allocator.
        # Spot #1: TEST R13B; JE unshadowed; MOV AL,shadow; TEST AL;
        #          JE skip; MOV AL,unshadow; OR AL,R13B; JE skip.
        # Spot #2: TEST EDX; JE unshadowed; MOV AL,shadow; TEST AL;
        #          JE skip; TEST EDX; JNE output; MOV AL,unshadow; TEST AL.
        self.assertEqual(REDIRECTS["omni_light_shadows"][0xA63BB6],("84c9","30c9"))
        self.assertEqual(REDIRECTS["spot_light_shadows"][0xA6445A],("4584ed","4530ed"))
        self.assertEqual(REDIRECTS["spot_light_shadows"][0xA652A0],("85d2","31d2"))

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
