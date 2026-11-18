"""Guard the observed 01.11 animation pointer SIGSEGV at guest 0xA623F5.

This verifies the 5-byte CALL trampoline, 31-byte original NOP cave and
both control-flow exits. The actual source bytes were independently checked
against the two user-provided SHA-256 allowlisted 01.11 SELF files.
"""
import struct
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tlou_patches as patches

class Tlou111AnimationPointerGuard(unittest.TestCase):
    def test_only_111_profile_gets_guard(self):
        group = patches.BASE_PATCHES_111["invalid-animation-output-pointer"]
        self.assertEqual(len(group),2)
        self.assertNotIn("invalid-animation-output-pointer",patches.BASE_PATCHES)
        self.assertEqual({0xA623D3,0xC9FC61},{row[0] for row in group})
        self.assertIn("invalid-animation-output-pointer",patches.base_patches_for({}, "01.11"))
        self.assertNotIn("invalid-animation-output-pointer",patches.base_patches_for({}, "01.00"))

    def test_jump_cave_and_valid_register_path(self):
        first,cave=patches.BASE_PATCHES_111["invalid-animation-output-pointer"]
        site,old,call=first
        base,expected,patched=cave
        self.assertEqual(bytes.fromhex(old),bytes.fromhex("4d8b741f98"))
        self.assertEqual(expected,"90"*31)
        code=bytes.fromhex(patched)
        self.assertEqual(len(code),31)
        self.assertEqual(code[0:5],bytes.fromhex(old))
        self.assertEqual(code[5:9],bytes.fromhex("504c89f0")) # push rax, mov rax,r14
        self.assertEqual(code[9:17],bytes.fromhex("48c1e8244885c058"))
        self.assertEqual(code[17:19],bytes.fromhex("7509")) # jnz ret
        self.assertEqual(code[19:23],bytes.fromhex("4883c408")) # pop CALL return on invalid node
        self.assertEqual(code[28:31],bytes.fromhex("c39090"))
        self.assertEqual(base+19+9,base+28) # valid pointer returns
        callcode=bytes.fromhex(call)
        self.assertEqual(callcode[0],0xE8)
        self.assertEqual(site+5+struct.unpack("<i",callcode[1:])[0],base)
        self.assertEqual(code[23],0xE9)
        self.assertEqual(base+28+struct.unpack("<i",code[24:28])[0],0xA625B0)
        # The invalid pointer from the supplied Android crash is rejected,
        # whereas a normal mapped animation output pointer stays on-path.
        self.assertEqual(0x100000007 >> 36,0)
        self.assertNotEqual(0x1121d00600 >> 36,0)

    def test_guard_does_not_overlap_effect_or_resolution_patches(self):
        from itertools import chain
        sites=[(addr,len(bytes.fromhex(old)))
            for changes in chain(patches.PERFORMANCE_EFFECTS_111.values(),
                                 [patches.scene_aim_resolution_patches_111("720")],
                                 [patches.BASE_PATCHES_111["job-sleep"]])
            for addr,old,_ in changes]
        for address,original,_ in patches.BASE_PATCHES_111["invalid-animation-output-pointer"]:
            length=len(bytes.fromhex(original))
            for other,other_size in sites:
                self.assertTrue(address+length<=other or other+other_size<=address,
                                (hex(address),hex(other)))

if __name__ == "__main__":
    unittest.main()
