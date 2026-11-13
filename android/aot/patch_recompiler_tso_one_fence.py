#!/usr/bin/env python3
"""EXPERIMENT ONLY: opt-in TLOU AOT x86-TSO one-fence lowering.

NOT production-ready: confirm correctness with ARM64 litmus tests and matched
FEX/AOT gameplay before using for any APK. The original compiler is unchanged.
The candidate mapping places dmb ish AFTER guest loads and BEFORE guest stores;
this preserves the ordered Load->Load, Load->Store and Store->Store pairs while
allowing the Store->Load relaxation defined by x86-TSO. This is a hypothesis,
not a completed architecture-level equivalence proof.
"""
from pathlib import Path
import argparse

ORIGINAL_LOAD = """    B.CreateFence(AtomicOrdering::SequentiallyConsistent);
    Value* value = loadVal(memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale),
                           size, o->Class == RegClass::FPR);
    B.CreateFence(AtomicOrdering::SequentiallyConsistent);"""
OPT_LOAD = """    Value* value = loadVal(memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale),
                           size, o->Class == RegClass::FPR);
    B.CreateFence(AtomicOrdering::SequentiallyConsistent);"""
ORIGINAL_STORE = """    B.CreateFence(AtomicOrdering::SequentiallyConsistent);
    storeVal(memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale),
             A(o->Value), size, o->Class == RegClass::FPR);
    B.CreateFence(AtomicOrdering::SequentiallyConsistent);"""
OPT_STORE = """    B.CreateFence(AtomicOrdering::SequentiallyConsistent);
    storeVal(memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale),
             A(o->Value), size, o->Class == RegClass::FPR);"""
OLD_VERSION = '#define BBR_COMPILER_VERSION "3-tlou-string"'
NEW_VERSION = '#define BBR_COMPILER_VERSION "4-tlou-tso-one-fence"'

def patch(upstream: Path) -> None:
    code, version = upstream / "src/lower_ops.cpp", upstream / "src/version.h"
    before, old = code.read_text(encoding="utf-8"), version.read_text(encoding="utf-8")
    if (before.count(OPT_LOAD) == 1 and before.count(OPT_STORE) == 1 and
            old.count(NEW_VERSION) == 1):
        print("Experimental TSO mapping already installed")
        return
    if (before.count(ORIGINAL_LOAD) != 1 or before.count(ORIGINAL_STORE) != 1 or
            old.count(OLD_VERSION) != 1 or
            before.count("case OP_LOADMEMTSO:") != 1 or
            before.count("case OP_STOREMEMTSO:") != 1):
        raise RuntimeError("Unexpected LLVM TSO lowering: compiler left unchanged")
    after = before.replace(ORIGINAL_LOAD, OPT_LOAD).replace(ORIGINAL_STORE, OPT_STORE)
    if after.count(OPT_LOAD) != 1 or after.count(OPT_STORE) != 1:
        raise RuntimeError("Unexpected optimization result: compiler left unchanged")
    # v4 invalidates ALL old --resume shards; there must be no mixing with v3.
    code.write_text(after, encoding="utf-8")
    version.write_text(old.replace(OLD_VERSION, NEW_VERSION), encoding="utf-8")
    print("One-fence candidate installed. No binary or APK built.")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("upstream", type=Path)
    patch(ap.parse_args().upstream)
