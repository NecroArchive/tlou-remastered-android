#!/usr/bin/env python3
"""Offline asset assembly tests for TLOU's 3 pinned TSO compiler variants."""
import importlib.util
import os
from pathlib import Path
import struct
import tempfile
import unittest

root=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location("aot_pack",root/"repackage_local_aot.py")
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class MultiCompilerPackaging(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        self.original_stage=mod.AOT
        self.old=os.environ.get("TLOU_TSO_MULTI_PROFILE_DIR")
        self.addCleanup(self.restore)
        mod.AOT=self.root/"staged-v5"
        self.v3=self.root/"archives/v3/android"
        self.v4=self.root/"archives/v4/android"
        self.v5=mod.AOT
        for directory,version,location in (
            (self.v3,b"3-tlou-string","libbbrecomp.so"),
            (self.v4,b"4-tlou-tso-aligned-ar-v1","libbbrecomp.so"),
            (self.v5,b"5-tlou-tso-outlined-unaligned-v1","bin/libbbrecomp.so"),
        ):
            path=directory/location
            path.parent.mkdir(parents=True,exist_ok=True)
            blob=bytearray(128)
            blob[:4]=b"\x7fELF"
            struct.pack_into("<H",blob,18,183)
            path.write_bytes(bytes(blob)+version)
            bundle=directory/"bundle"
            bundle.mkdir(parents=True)
            (bundle/"helpers-aarch64.o").write_bytes(b"helper "+version)
            (bundle/"libbbcpu-stub.so").write_bytes(b"stub")
        (mod.AOT/"upstream.sha256").write_text("a"*64+"\n")
        os.environ["TLOU_TSO_MULTI_PROFILE_DIR"]=str(self.root/"archives")

    def restore(self):
        mod.AOT=self.original_stage
        if self.old is None:
            os.environ.pop("TLOU_TSO_MULTI_PROFILE_DIR",None)
        else:
            os.environ["TLOU_TSO_MULTI_PROFILE_DIR"]=self.old

    def test_three_versions_are_isolated_and_checksummed(self):
        payload=mod.compiler_payload()
        assert "bin/libbbrecomp.so" in payload
        for profile,version in (("v3",b"3-tlou-string"),
                                ("v4",b"4-tlou-tso-aligned-ar-v1"),
                                ("v5",b"5-tlou-tso-outlined-unaligned-v1")):
            key=f"aot/compilers/{profile}"
            self.assertIn(version,payload[key+"/libbbrecomp.so"])
            self.assertIn(b"helper "+version,
                          payload[key+"/bundle/helpers-aarch64.o"])
            self.assertEqual(len(payload[key+"/compiler.sha256"].strip()),64)
        self.assertNotEqual(payload["aot/compilers/v3/libbbrecomp.so"],
                            payload["aot/compilers/v4/libbbrecomp.so"])

    def test_reject_wrong_compiler_version(self):
        (self.v4/"libbbrecomp.so").write_bytes(
            (self.v3/"libbbrecomp.so").read_bytes())
        with self.assertRaisesRegex(ValueError,"Wrong AOT TSO compiler"):
            mod.compiler_payload()

    def test_reject_missing_per_profile_linker_bundle(self):
        (self.v4/"bundle/helpers-aarch64.o").unlink()
        with self.assertRaisesRegex(ValueError,"linker bundle missing"):
            mod.compiler_payload()

    def test_reject_missing_profile_compiler(self):
        (self.v3/"libbbrecomp.so").unlink()
        with self.assertRaisesRegex(ValueError,"Missing AOT TSO profile"):
            mod.compiler_payload()


if __name__=="__main__":
    unittest.main()
