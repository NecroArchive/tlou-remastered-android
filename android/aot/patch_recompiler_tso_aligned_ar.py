#!/usr/bin/env python3
"""Opt-in AOT TSO scalar lowering, keeping old full-fence fallback for unaligned/SIMD.

Only naturally-aligned GPR 8/16/32/64-bit accesses use LLVM acquire/release
atomic loads/stores; this emits LDAR/STLR on ARMv8.0+. Unaligned accesses,
all SIMD/FPR accesses and unusual sizes retain the original double DMB
implementation. This is a controlled compiler experiment, NOT a claim of
whole-game or multicore correctness.
"""
from __future__ import annotations

import argparse
from pathlib import Path
from patch_recompiler_tso import AFTER

OLD_VERSION = '#define BBR_COMPILER_VERSION "3-tlou-string"'
NEW_VERSION = '#define BBR_COMPILER_VERSION "4-tlou-tso-aligned-ar-v1"'

# Exact v3 lowering is retained verbatim as the conservative fallback.
OLD_LOAD = "  case OP_LOADMEMTSO: {" + AFTER.split("  case OP_LOADMEMTSO: {", 1)[1].split("  case OP_STOREMEM: {", 1)[0]
OLD_STORE = "  case OP_STOREMEMTSO: {" + AFTER.split("  case OP_STOREMEMTSO: {", 1)[1]

NEW_LOAD = """  case OP_LOADMEMTSO: {
    auto* o = op->C<IROp_LoadMemTSO>();
    if (size > (o->Class == RegClass::FPR ? 16u : 8u))
      return false;
    // Preserve the exact two-fence lowering for SIMD and all sizes not
    // supported by natural scalar ARM64 acquire loads.
    if (o->Class == RegClass::FPR ||
        (size != 1 && size != 2 && size != 4 && size != 8)) {
      B.CreateFence(AtomicOrdering::SequentiallyConsistent);
      Value* value = loadVal(memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale),
                             size, o->Class == RegClass::FPR);
      B.CreateFence(AtomicOrdering::SequentiallyConsistent);
      def(node, value);
      return true;
    }
    Value* addr = memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale);
    Value* ptr = B.CreateIntToPtr(addr, Ptr);
    auto acquire = [&]() -> Value* {
      auto* ld = B.CreateAlignedLoad(iN(size * 8), ptr, Align(size));
      ld->setAtomic(AtomicOrdering::Acquire);
      // Match the original volatile spin-loop policy (atomics also prevent
      // repeated guest loads from being hoisted out of polling loops).
      ld->setVolatile(quietLoop.count(curBlock) != 0);
      return zx(ld);
    };
    if (size == 1) {
      def(node, acquire());
      return true;
    }
    // A guest x86 address can be unaligned. LLVM atomics MUST NOT be marked
    // naturally aligned unless the runtime address has been checked.
    Value* naturally_aligned = B.CreateICmpEQ(
        B.CreateAnd(addr, c64(size - 1)), c64(0));
    auto* aligned_bb = BasicBlock::Create(B.getContext(), "tso.acquire.aligned", B.GetInsertBlock()->getParent());
    auto* fallback_bb = BasicBlock::Create(B.getContext(), "tso.acquire.unaligned", B.GetInsertBlock()->getParent());
    auto* join_bb = BasicBlock::Create(B.getContext(), "tso.acquire.join", B.GetInsertBlock()->getParent());
    B.CreateCondBr(naturally_aligned, aligned_bb, fallback_bb);
    B.SetInsertPoint(aligned_bb);
    Value* aligned_value = acquire();
    B.CreateBr(join_bb);
    B.SetInsertPoint(fallback_bb);
    B.CreateFence(AtomicOrdering::SequentiallyConsistent);
    Value* fallback_value = loadVal(addr, size, false);
    B.CreateFence(AtomicOrdering::SequentiallyConsistent);
    B.CreateBr(join_bb);
    B.SetInsertPoint(join_bb);
    auto* result = B.CreatePHI(I64, 2);
    result->addIncoming(aligned_value, aligned_bb);
    result->addIncoming(fallback_value, fallback_bb);
    def(node, result);
    return true;
  }
"""

NEW_STORE = """  case OP_STOREMEMTSO: {
    auto* o = op->C<IROp_StoreMemTSO>();
    if (size > (o->Class == RegClass::FPR ? 16u : 8u))
      return false;
    if (o->Class == RegClass::FPR ||
        (size != 1 && size != 2 && size != 4 && size != 8)) {
      B.CreateFence(AtomicOrdering::SequentiallyConsistent);
      storeVal(memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale),
               A(o->Value), size, o->Class == RegClass::FPR);
      B.CreateFence(AtomicOrdering::SequentiallyConsistent);
      return true;
    }
    Value* addr = memAddr(o->Addr, o->Offset, o->OffsetType, o->OffsetScale);
    Value* ptr = B.CreateIntToPtr(addr, Ptr);
    auto release = [&]() {
      auto* st = B.CreateAlignedStore(tr(A(o->Value), size * 8), ptr, Align(size));
      st->setAtomic(AtomicOrdering::Release);
    };
    if (size == 1) {
      release();
      return true;
    }
    Value* naturally_aligned = B.CreateICmpEQ(
        B.CreateAnd(addr, c64(size - 1)), c64(0));
    auto* aligned_bb = BasicBlock::Create(B.getContext(), "tso.release.aligned", B.GetInsertBlock()->getParent());
    auto* fallback_bb = BasicBlock::Create(B.getContext(), "tso.release.unaligned", B.GetInsertBlock()->getParent());
    auto* join_bb = BasicBlock::Create(B.getContext(), "tso.release.join", B.GetInsertBlock()->getParent());
    B.CreateCondBr(naturally_aligned, aligned_bb, fallback_bb);
    B.SetInsertPoint(aligned_bb);
    release();
    B.CreateBr(join_bb);
    B.SetInsertPoint(fallback_bb);
    B.CreateFence(AtomicOrdering::SequentiallyConsistent);
    storeVal(addr, A(o->Value), size, false);
    B.CreateFence(AtomicOrdering::SequentiallyConsistent);
    B.CreateBr(join_bb);
    B.SetInsertPoint(join_bb);
    return true;
  }"""


def patch(upstream: Path) -> None:
    code_path = upstream / "src/lower_ops.cpp"
    version_path = upstream / "src/version.h"
    code = code_path.read_text(encoding="utf-8")
    version = version_path.read_text(encoding="utf-8")
    if (NEW_LOAD in code and NEW_STORE in code and NEW_VERSION in version):
        print("TLOU aligned acquire/release experiment already applied")
        return
    if (code.count(OLD_LOAD) != 1 or code.count(OLD_STORE) != 1 or
        version.count(OLD_VERSION) != 1 or
        code.count("case OP_LOADMEMTSO:") != 1 or
        code.count("case OP_STOREMEMTSO:") != 1):
        raise RuntimeError("Unknown compiler source or shard version; no patch applied")
    modified = code.replace(OLD_LOAD, NEW_LOAD).replace(OLD_STORE, NEW_STORE)
    assert modified.count("case OP_LOADMEMTSO:") == 1
    assert modified.count("case OP_STOREMEMTSO:") == 1
    assert modified.count("AtomicOrdering::Acquire") >= 1
    assert modified.count("AtomicOrdering::Release") >= 1
    # Only change files after EVERY precondition succeeds.
    version_path.write_text(version.replace(OLD_VERSION, NEW_VERSION), encoding="utf-8")
    code_path.write_text(modified, encoding="utf-8")
    print("TLOU opt-in aligned TSO acquire/release compiled-source patch; v4 shards required")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("upstream", type=Path)
    patch(parser.parse_args().upstream)
