#!/usr/bin/env python3
"""Production Android AOT ships one pinned compiler without multiple profile bundles."""
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

class SingleCompilerPackaging(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.original_aot=mod.AOT
        self.old=os.environ.pop("TLOU_TSO_MULTI_PROFILE_DIR",None)
        self.addCleanup(self.restore)
        mod.AOT=Path(self.tmp.name)/"aot-compiler"
        binary=mod.AOT/"bin/libbbrecomp.so"
        binary.parent.mkdir(parents=True)
        self.binary=binary
        data=bytearray(128)
        data[:4]=b"\x7fELF"
        struct.pack_into("<H",data,18,183)
        binary.write_bytes(bytes(data)+b"3-tlou-string")
        bundle=mod.AOT/"bundle"
        bundle.mkdir(parents=True)
        (bundle/"helpers-aarch64.o").write_bytes(b"verified helper")
        (mod.AOT/"upstream.sha256").write_text("a"*64+"\n")

    def restore(self):
        mod.AOT=self.original_aot
        if self.old is not None:
            os.environ["TLOU_TSO_MULTI_PROFILE_DIR"]=self.old
        else:
            os.environ.pop("TLOU_TSO_MULTI_PROFILE_DIR",None)

    def test_production_contains_one_compiler_no_profiles(self):
        payload=mod.compiler_payload()
        self.assertIn(b"3-tlou-string",payload["bin/libbbrecomp.so"])
        self.assertIn("aot/bundle/helpers-aarch64.o",payload)
        self.assertFalse(any(k.startswith("aot/compilers/") for k in payload))

    def test_wrong_v5_compiler_is_rejected(self):
        self.binary.write_bytes(self.binary.read_bytes().replace(
            b"3-tlou-string",b"5-tlou-tso-outlined-unaligned-v1"))
        with self.assertRaisesRegex(ValueError,"validated v3"):
            mod.compiler_payload()

    def test_reject_accidental_multi_profile_packaging(self):
        os.environ["TLOU_TSO_MULTI_PROFILE_DIR"]=str(Path(self.tmp.name)/"archives")
        with self.assertRaisesRegex(ValueError,"multiple AOT compiler"):
            mod.compiler_payload()

    def test_missing_bundle_is_rejected(self):
        (mod.AOT/"bundle/helpers-aarch64.o").unlink()
        (mod.AOT/"bundle").rmdir()
        with self.assertRaisesRegex(ValueError,"not staged"):
            mod.compiler_payload()

if __name__=="__main__":
    unittest.main()
