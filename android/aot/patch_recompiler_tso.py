#!/usr/bin/env python3
"""Add conservative x86-TSO memory lowering to pinned bb-recomp 1.3.0.

The upstream compiler accepts --tso and the FEX decoder emits OP_LOADMEMTSO
and OP_STOREMEMTSO, but the LLVM backend lacks these cases, silently marking
most native units dirty with bbr_trap. Do not paper over these ops by disabling
TSO or replacing their memory semantics with relaxed loads/stores.

This prototype emits full sequentially consistent fences on both sides of the
guest access. That is conservative and intentionally slower; after gameplay
correctness is established, we can optimize with per-size acquire/release
instructions and compare outcomes to FEX on real PS4 code.
"""
from __future__ import annotations
import argparse
from pathlib import Path

BEFORE = """  case OP_LOADMEM: {
    auto* o = op->C<IROp_LoadMem>();
    def(node, loadVal(memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale), size, o->Class == RegClass::FPR));
    return true;
  }
  case OP_STOREMEM: {
    auto* o = op->C<IROp_StoreMem>();
    storeVal(memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale), A(o->Value), size, o->Class == RegClass::FPR);
    return true;
  }"""

AFTER = """  case OP_LOADMEM: {
    auto* o = op->C<IROp_LoadMem>();
    def(node, loadVal(memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale), size, o->Class == RegClass::FPR));
    return true;
  }
  case OP_LOADMEMTSO: {
    auto* o = op->C<IROp_LoadMemTSO>();
    // Preserve x86 TSO ordering including Store->Load. An ordinary load is
    // insufficient on ARM and using LLVM atomic acquire with Align(1) on an
    // unaligned PS4 pointer would violate the atomic alignment contract.
    // Strong fences cover all integer and SIMD accesses conservatively.
    if (size > (o->Class == RegClass::FPR ? 16u : 8u))
      return false; // genuinely unsupported width: retain explicit dirty trap
    B.CreateFence(AtomicOrdering::SequentiallyConsistent);
    Value* value = loadVal(memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale),
                           size, o->Class == RegClass::FPR);
    B.CreateFence(AtomicOrdering::SequentiallyConsistent);
    def(node, value);
    return true;
  }
  case OP_STOREMEM: {
    auto* o = op->C<IROp_StoreMem>();
    storeVal(memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale), A(o->Value), size, o->Class == RegClass::FPR);
    return true;
  }
  case OP_STOREMEMTSO: {
    auto* o = op->C<IROp_StoreMemTSO>();
    if (size > (o->Class == RegClass::FPR ? 16u : 8u))
      return false;
    B.CreateFence(AtomicOrdering::SequentiallyConsistent);
    storeVal(memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale),
             A(o->Value), size, o->Class == RegClass::FPR);
    B.CreateFence(AtomicOrdering::SequentiallyConsistent);
    return true;
  }"""


def patch(root: Path) -> Path:
    file = root / "src/lower_ops.cpp"
    data = file.read_text(encoding="utf-8")
    if data.count(BEFORE) != 1:
        if data.count(AFTER) == 1:
            print("Conservative TSO lowering already installed")
            return file
        raise RuntimeError("Upstream changed: refusing to patch LLVM lowering by guesswork")
    file.write_text(data.replace(BEFORE, AFTER), encoding="utf-8")
    # The original 1.3.0 tool uses compiler-version 1 for resume/shard keys.
    # The memory lowering fix changes generated code, so invalidate all old
    # shards even when the image and command-line inputs are identical.
    version = root / "src/version.h"
    version_text = version.read_text(encoding="utf-8")
    old = '#define BBR_COMPILER_VERSION "1"'
    if version_text.count(old) != 1:
        raise RuntimeError("Cannot safely bump bb-recomp compiler shard version")
    version.write_text(version_text.replace(
        old, '#define BBR_COMPILER_VERSION "2-tlou-tso"'), encoding="utf-8")
    assert file.read_text().count("case OP_LOADMEMTSO:") == 1
    assert file.read_text().count("case OP_STOREMEMTSO:") == 1
    print("Conservative ARM64 TSO lowering patched: scalar and SIMD <=128 bits")
    return file


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("upstream", type=Path)
    a = ap.parse_args()
    patch(a.upstream)
