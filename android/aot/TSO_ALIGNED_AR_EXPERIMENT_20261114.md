# TLOU ARM64 AOT — TSO aligned scalar experiment (2026-11-14)

## Goal and safety

The existing AOT compiler v3 `3-tlou-string` lowers x86-TSO `LoadMemTSO` and
`StoreMemTSO` with **two full DMB ISH barriers** around each native memory
access. Those fences preserve safety but consume significant CPU time.

This experiment targets ONLY 8-, 16-, 32- and 64-bit GPR TSO accesses when
the actual guest address is naturally aligned. It emits LLVM atomic
Acquire/Release (AArch64 LDAR/STLR); if not aligned, the original double
sequentially-consistent fence code still runs. SIMD/FPR operations use the
original full-fence path. The original production compiler is **unchanged**
unless `TLOU_TSO_ALIGNED_AR_EXPERIMENT=1` is explicitly set when building
the compiler.

Important: a successful ARM64 microbenchmark is NOT a complete x86-TSO proof,
particularly for mixed scalar/SIMD operations, overlapping writes, exceptions,
or GPU/CPU memory coherency. Full game validation is mandatory.

## Code

- `android/aot/patch_recompiler_tso_aligned_ar.py`: opt-in LLVM compiler
  source patch against verified pinned bb-recomp version `3-tlou-string`.
- `android/aot/build_recompiler_tso.sh`: opt-in compiler build preserving
  default v3 semantics; resulting shard version **`4-tlou-tso-aligned-ar-v1`**.
- `android/aot/test_tso_aligned_ar.py`: regression tests of exact patch,
  upstream-version guards, LLVM alignment check and conservative fallbacks.
- `gow3_pc/tests/test_tlou_aot_tso_guarded.cpp`: synthetic exact-value
  benchmarks for GPR sizes 1, 2, 4, 8 bytes and 0/10/50% misalignment.
- `gow3_pc/tests/test_tlou_aot_tso_litmus.cpp`: ARM64 concurrent MP, LB,
  SB and IRIW screenings. Statistical tests alone cannot prove correctness.
- `android-launcher/.../RuntimeAot.java`: **experimental branch only**
  requires compiler v4 metadata and rejects pre-existing v3 game libraries,
  ensuring a complete AOT rebuild. Original APK unchanged.
- `android/aot/aot_local.py`: accepts known v3 or v4 compiler metadata
  when checking coverage, but experimental app's runtime requires v4.
- `android/bionic/stage_aot_compiler.sh`: validates v4 byte signature only
  when experimental flag is enabled; original v3 remains default.
- `.github/workflows/tlou-aot-tso-guarded-proof.yml`: gated LLVM23 compiler
  build runs only if both pinned-source and multicore ARM64 tests pass.

## Results already measured — native ARM64 CI

Proof run: https://github.com/erickmendes22/tlou-remastered-android/actions/runs/43611453066
- 64-bit, aligned: full-fence **19.00 ns/pair**, acquire/release **4.88 ns/pair**.
- 64-bit, 10% unaligned: **19.16 -> 6.61 ns/pair**.
- 64-bit, 50% unaligned: **19.80 -> 12.84 ns/pair**.
- Checksums identical on all sizes and alignments.
- 35k rounds each: MP, LB and IRIW reported zero forbidden outcomes.
- SB allowed outcome [0,0] was not observed; that is not evidence of invalidity
  because stronger ordering is allowed.
- These results are microbenchmarks **not on-device TLOU FPS predictions**.

Compiler source from the *actual pinned upstream commit* was patched successfully
with exact guards and unchanged fallback. Experimental LLVM23 AArch64 compiler
build run: https://github.com/erickmendes22/tlou-remastered-android/actions/runs/43611614645.
Check status before claiming a compiled artifact exists.

Experimental app-side v4 validation:
https://github.com/erickmendes22/tlou-remastered-android/actions/runs/43612128778

## Next gates

1. Finish compiler build, verify real Android AArch64 `libbbrecomp.so`,
   `4-tlou-tso-aligned-ar-v1` and `.sha256` archive. Resolve compile
   errors; never replace v3 in production silently.
2. Create experimental **same-signing-key, same-package** APK from original
   signing branch `experiment/tlou-local-aot-20261112`. Work-in-progress
   template: `.github/workflows/tlou-aot-tso-v4-original-signer-apk.yml`.
   This pins stable GPU run 43589435245, source code commit
   `38352876a98e59594935dcce1441511511c537d4`, and new compiler run
   43611614645. It has only `workflow_dispatch`, not a push trigger:
   deliberately **do not start APK** before the compiler artifact succeeds.
3. Confirm APK package `com.daedalus.tlou.remastered.aotnative2`, unchanged
   certificate SHA-256 `adbdd4efd7cb981ce6a30303de83b635800b0960c68276d9247962918cc7328c`.
   The existing AOT game library MUST be recompiled, since v3 units are
   incompatible with v4.
4. In device gameplay compare identical scenes and cold clocks against stable
   log 17/19; check correct physics, job threads, crashes and readbacks.
5. If regressions are observed, keep original v3 compiler/runtime/APK intact;
   no fallback to an unsafe weakened TSO model.
