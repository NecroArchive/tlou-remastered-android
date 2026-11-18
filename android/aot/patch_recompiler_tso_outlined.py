#!/usr/bin/env python3
"""Opt-in AOT TSO v5: put rare unaligned full-fence scalar accesses in shared helpers.

The v4 runtime-guarded LDAR/STLR emits inlined DMB/LDR/DMB and
DMB/STR/DMB fallback for every dynamic scalar memory op; TLOU 1.11's
native .text grew from 75.58 to 94.53 MB with no measurable FPS gain.

v5 leaves all aligned LDAR/STLR logic and SIMD conservative lowering
unchanged, but replaces the unaligned fallback body with two shared
non-LTO helper calls, preserving the two DMB barriers and exact byte
size. ONLY active after the complete v4 patch and an explicit build flag.

Not a proof of TSO semantics or of full-game FPS.
"""
import argparse
from pathlib import Path
from patch_recompiler_tso_aligned_ar import NEW_LOAD as V4_LOAD, NEW_STORE as V4_STORE, NEW_VERSION as V4_VERSION

V5_VERSION = '#define BBR_COMPILER_VERSION "5-tlou-tso-outlined-unaligned-v1"'
SRC_LOAD = """    B.CreateFence(AtomicOrdering::SequentiallyConsistent);
    Value* fallback_value = loadVal(addr, size, false);
    B.CreateFence(AtomicOrdering::SequentiallyConsistent);"""
DST_LOAD = """    Value* fallback_value = B.CreateCall(
        m.helper("bbr_tso_unaligned_load", I64, {I64, I32}),
        {addr, c32(size)});"""
SRC_STORE = """    B.CreateFence(AtomicOrdering::SequentiallyConsistent);
    storeVal(addr, A(o->Value), size, false);
    B.CreateFence(AtomicOrdering::SequentiallyConsistent);"""
DST_STORE = """    B.CreateCall(
        m.helper("bbr_tso_unaligned_store", m.Void, {I64, I64, I32}),
        {addr, A(o->Value), c32(size)});"""
V5_LOAD = V4_LOAD.replace(SRC_LOAD, DST_LOAD)
V5_STORE = V4_STORE.replace(SRC_STORE, DST_STORE)

def patch(upstream: Path, helpers: Path) -> None:
    code_path = upstream / "src/lower_ops.cpp"
    version_path = upstream / "src/version.h"
    runtime_helpers = upstream / "helpers/bbr_x87_all.c"
    original = code_path.read_text(encoding="utf-8")
    version = version_path.read_text(encoding="utf-8")
    old_helpers = runtime_helpers.read_text(encoding="utf-8")
    source = helpers.read_text(encoding="utf-8")
    if (V5_LOAD in original and V5_STORE in original and V5_VERSION in version and
            "uint64_t bbr_tso_unaligned_load(" in old_helpers):
        print("TSO v5 outlined fallback already applied")
        return
    if (V4_LOAD not in original or V4_STORE not in original or
            version.count(V4_VERSION) != 1 or
            original.count("case OP_LOADMEMTSO:") != 1 or
            original.count("case OP_STOREMEMTSO:") != 1 or
            original.count(SRC_LOAD) != 1 or original.count(SRC_STORE) != 1 or
            "uint64_t bbr_tso_unaligned_load(" in old_helpers or
            "void bbr_tso_unaligned_store(" in old_helpers or
            source.count("uint64_t bbr_tso_unaligned_load(") != 1 or
            source.count("void bbr_tso_unaligned_store(") != 1):
        raise RuntimeError("Unknown v4 compiler or helper source: v5 compiler unchanged")
    next_code = original.replace(V4_LOAD, V5_LOAD).replace(V4_STORE, V5_STORE)
    assert next_code.count("case OP_LOADMEMTSO:") == 1
    assert next_code.count("case OP_STOREMEMTSO:") == 1
    assert next_code.count("AtomicOrdering::Acquire") >= 1
    assert next_code.count("AtomicOrdering::Release") >= 1
    assert next_code.count('m.helper("bbr_tso_unaligned_load"') == 1
    assert next_code.count('m.helper("bbr_tso_unaligned_store"') == 1
    runtime_helpers.write_text(old_helpers + "\n" + source)
    version_path.write_text(version.replace(V4_VERSION, V5_VERSION))
    code_path.write_text(next_code)
    print("TSO v5: aligned atomic fast path unchanged; full-fence unaligned fallback outlined")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("upstream", type=Path)
    ap.add_argument("--helper-source", type=Path,
                    default=Path(__file__).with_name("bbr_tso_unaligned_helpers.c"))
    args=ap.parse_args()
    patch(args.upstream, args.helper_source)
