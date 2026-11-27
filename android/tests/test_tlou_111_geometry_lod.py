"""Guarded 1.11 LOD selection; CI uses no copyrighted SELF bytes."""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import tlou_patches as patch

class GeometryLod(unittest.TestCase):
    def test_only_geometry_threshold_load_changes(self):
        spans = patch.PERFORMANCE_EFFECTS_111["distant_geometry_lod"]
        writes = [(a,old,new) for a,old,new in spans if new is not None]
        self.assertEqual(len(writes),1)
        a,old,new = writes[0]
        self.assertEqual(a,0xACF111)
        self.assertEqual(a+7+struct.unpack("<i",bytes.fromhex(old)[3:])[0],0x1526070)
        self.assertEqual(a+7+struct.unpack("<i",bytes.fromhex(new)[3:])[0],0x10D0D10)
        constants={addr:bytes.fromhex(guard) for addr,guard,replacement in spans}
        self.assertEqual(struct.unpack("<f",constants[0x10D0D10])[0],0.75)
        for digest in patch.SELF_SHA256_111:
            self.assertEqual(patch.selected_options({"distant_geometry_lod":True},"01.11",digest)["disable-distant_geometry_lod"],spans)
            self.assertNotIn("disable-distant_geometry_lod",patch.selected_options({},"01.11",digest))
        self.assertNotIn("disable-distant_geometry_lod",patch.selected_options({"distant_geometry_lod":True},"01.00"))
