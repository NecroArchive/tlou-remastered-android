"""SSR patch must follow the native disabled path, not remove resources."""
import struct
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import tlou_patches as patch

class SsrPatch(unittest.TestCase):
    def test_original_off_target_and_calls(self):
        spans = patch.PERFORMANCE_EFFECTS_111["ssr"]
        writes = [(a,old,new) for a,old,new in spans if new is not None]
        self.assertEqual(writes, [(0xA93A3E,"7449","eb49")])
        self.assertEqual(0xA93A3E+2+0x49, 0xA93A89)
        guards = {at:bytes.fromhex(old) for at,old,new in spans}
        for at, target in [(0xA93A79,0xB11780),(0xA93A84,0xB11E90)]:
            self.assertEqual(guards[at][0],0xe8)
            self.assertEqual(at+5+struct.unpack("<i",guards[at][1:])[0],target)
        for digest in patch.SELF_SHA256_111:
            self.assertEqual(patch.selected_options({"ssr":True},"01.11",digest)["disable-ssr"], spans)
            self.assertNotIn("disable-ssr",patch.selected_options({},"01.11",digest))
        self.assertNotIn("disable-ssr",patch.selected_options({"ssr":True},"01.00"))
