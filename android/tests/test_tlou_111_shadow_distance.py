"""Guarded normal-mode solar cascade distance patch."""
import struct
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import tlou_patches as patch

class ShadowDistance(unittest.TestCase):
    def test_normal_cascade_distance(self):
        spans=patch.PERFORMANCE_EFFECTS_111["sun_shadow_quality"]
        writes=[(a,bytes.fromhex(o),bytes.fromhex(n)) for a,o,n in spans if n is not None]
        self.assertEqual(len(writes),1)
        addr,old,new=writes[0]
        self.assertEqual(addr,0xB9B2A2)
        self.assertEqual(len(old),64)
        self.assertEqual(len(new),64)
        i=0xB9B2BE-addr
        self.assertEqual(new[i:i+4],bytes.fromhex("c5ea5915"))
        self.assertEqual(addr+i+8+struct.unpack("<i",new[i+4:i+8])[0],0x10D0D10)
        guards={a:bytes.fromhex(o) for a,o,n in spans}
        self.assertEqual(struct.unpack("<f",guards[0x10D0D10])[0],0.75)
        self.assertEqual(guards[0xB9B2E2],bytes.fromhex("85c00f8436030000c5f258c0"))
        for digest in patch.SELF_SHA256_111:
            self.assertEqual(patch.selected_options({"sun_shadow_quality":True},"01.11",digest)["disable-sun_shadow_quality"],spans)
            self.assertNotIn("disable-sun_shadow_quality",patch.selected_options({},"01.11",digest))
        self.assertNotIn("disable-sun_shadow_quality",patch.selected_options({"sun_shadow_quality":True},"01.00"))
