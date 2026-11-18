# TLOU AOT x86-TSO v5 — evidence and rollback (2026-11-14)

## Ground truth: real phone logs

Comparison on Android 16 / Redmagic 11 Pro / 720p / TLOU 1.11:

| | TSO v3 (log 19) | TSO v4 (log 21) |
|---|---:|---:|
| compiler_version | 3-tlou-string | 4-tlou-tso-aligned-ar-v1 |
| AOT native units | 75,439 | 75,441 |
| unsupported AOT units | 0 | 0 |
| game codegen .text | 75.58 MB | 94.53 MB |
| generated shared library | 85.79 MB | 104.78 MB |
| average flips/sec (last 6 full 5s windows) | 56.8 | 56.8 |
| recording wait (last 5s) | 764.3 ms | 765.3 ms |
| command thread CPU (last 5s) | 67.9% | 68.3% |
| draw thread CPU (last 5s) | 64.7% | 65.0% |

These samples are similar, not synchronized recordings with frame-by-frame
matching, so interpret as **no demonstrated gain** rather than exact equality.
Do not infer FPS from synthetic benchmarks.

v4 used an alignment branch and full inlined DMB/LDR/DMB and DMB/STR/DMB
fallback on every 16/32/64-bit scalar guest memory access. This generated
18.95 MB more .text (25.1%) and likely increased instruction cache pressure.
The additional code is a concrete explanation to test, not a proven sole
cause of unchanged FPS.

## v5 candidate: preserve barriers, share cold code

- Fast path is UNCHANGED from v4: naturally aligned 8/16/32/64-bit scalar
  LLVM atomic Acquire/Release lowered by LLVM to ARM64 LDAR/STLR.
- Unaligned GPR 16/32/64-bit slow path calls
  `bbr_tso_unaligned_load` / `bbr_tso_unaligned_store` from the existing
  `helpers-aarch64.o` bundle. Each helper retains a full sequentially-
  consistent barrier BEFORE and AFTER the access.
- The same-sized scalar memcpy is exact-byte and remains limited to 2/4/8
  bytes. SIMD/FPR operations stay at the previous conservative inlined
  implementation, entirely unchanged.
- All v5 changes require BOTH build flags
  `TLOU_TSO_ALIGNED_AR_EXPERIMENT=1` and
  `TLOU_TSO_OUTLINE_UNALIGNED_EXPERIMENT=1`.
  Original v3 is the default, and v4 is still selectable if only the
  first flag is set. Shard version is
  `5-tlou-tso-outlined-unaligned-v1`; no old AOT artifacts may be reused.
- Gameplay testing must check readback/mprotect fault handling when the
  native PC is inside a cold helper. Generic Linux fault-retry test passed;
  this does NOT prove Android custom signal integration.

## Evidence from ARM64 CI

v5 source guard / benchmark:
https://github.com/erickmendes22/tlou-remastered-android/actions/runs/43658571940

Native ARM64 64-bit read+write pair:
- 0% unaligned: v4 5.26 ns, v5 5.20 ns
- 10% unaligned: v4 6.79 ns, v5 5.88 ns
- 50% unaligned: v4 12.97 ns, v5 8.66 ns

All byte checks and 35k-round MP/LB/IRIW statistical screens passed.
`test_tso_outlined.py` also includes a Linux mprotect/SIGSEGV test where a
64-bit memory access crosses a protected page; helper resumed after fault
handling with exact data and DMB-before/after codegen. Passing these tests
does **not** prove full x86-TSO, GPU readback correctness, or phone FPS.

Pinned compiler build run:
https://github.com/erickmendes22/tlou-remastered-android/actions/runs/43658666646

Original signer branch has a staged, **not auto-triggered** APK workflow:
`.github/workflows/tlou-aot-tso-v5-original-signer-apk.yml`.
It references the successful v5 compiler artifact when available and
preserves the previously verified GPU, application ID, and signing certificate.
Only enable after the compiler job succeeds.

## Decision rule

Do not deploy v5 to the standard FEX/v3 path. First compile actual v5
compiler and produce a same-signer APK; user must recompile 1.11 AOT native
library. On device compare 5s windows of the identical gameplay area, actual
codegen .text size, CPU critical and recording waits, readbacks, physics,
shader compile stalls, and crashes.

If no improvement, keep baseline v3 and do not claim that another memory
barrier shortcut will automatically improve FPS. Memory model and physics
integrity take priority over synthetic speed.
