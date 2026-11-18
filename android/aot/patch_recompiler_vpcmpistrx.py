#!/usr/bin/env python3
"""Patch bb-recomp 1.3.0 LLVM lowering for PCMPISTRI/M and VEX variants.

The generated ARM64 .so links bbr_vpcmpistrx from the compiler's existing
helpers-aarch64.o, not from FEX or the Android app. Thus TLOU's libc.prx
strcmp/strncpy routines can run without a dirty-unit AOT miss.
"""
from pathlib import Path
import argparse

MARK = "  case OP_VEXTRACTTOGPR: {"
ADD = r'''  case OP_VPCMPISTRX: {
    // FEX IR's intermediate string-compare mask and NZCV bits. The helper
    // is inside helpers-aarch64.o so no new runtime dependency is introduced.
    auto* o = op->C<IROp_VPCMPISTRX>();
    Value* lhs = A(o->LHS);
    Value* rhs = A(o->RHS);
    Value* r = B.CreateCall(
        m.helper("bbr_vpcmpistrx", I32, {I64, I64, I64, I64, I32}),
        {B.CreateExtractElement(lhs, uint64_t(0)),
         B.CreateExtractElement(lhs, uint64_t(1)),
         B.CreateExtractElement(rhs, uint64_t(0)),
         B.CreateExtractElement(rhs, uint64_t(1)),
         c32(uint32_t(o->Control))});
    def(node, zx(r));
    return true;
  }
'''


def patch(upstream: Path, helper_source: Path) -> None:
    vector = upstream / "src/lower_vector.cpp"
    s = vector.read_text()
    if s.count(MARK) != 1:
        raise ValueError("Unsupported bb-recomp version: missing vector dispatch anchor")
    if s.count("case OP_VPCMPISTRX:"):
        raise ValueError("bb-recomp vector compare lowering already exists")
    vector.write_text(s.replace(MARK, ADD + MARK))
    helpers = upstream / "helpers/bbr_x87_all.c"
    original = helpers.read_text()
    helper = helper_source.read_text()
    if "uint32_t bbr_vpcmpistrx(" in original:
        raise ValueError("Duplicate AOT compare helper")
    if helper.count("uint32_t bbr_vpcmpistrx(") != 1:
        raise ValueError("Invalid compare helper source")
    helpers.write_text(original + "\n" + helper)
    version = upstream / "src/version.h"
    v = version.read_text()
    marker = '#define BBR_COMPILER_VERSION "2-tlou-tso"'
    if v.count(marker) != 1:
        raise ValueError("The TSO fix must be applied BEFORE the string compare fix")
    version.write_text(v.replace(marker, '#define BBR_COMPILER_VERSION "3-tlou-string"'))
    print("AArch64 VPCMPISTRX LLVM lowering and bundled SSE4.2 helper enabled. Shards invalidated (v3).")


if __name__ == "__main__":
    arg = argparse.ArgumentParser()
    arg.add_argument("upstream", type=Path)
    arg.add_argument("--helper-source", type=Path,
                     default=Path(__file__).with_name("bbr_vpcmpistrx.c"))
    args = arg.parse_args()
    patch(args.upstream, args.helper_source)
