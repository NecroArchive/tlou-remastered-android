# TLOU ARM64 SRT hot path — source investigation (2026-11-07)

Base: `tlou-ui-approval-log32-20261105` @ `e0b363a971e7290f377164444bf15552ec577f7d`.
This investigation does not change that base branch and must not trigger an APK.

## What the device logs do prove

Real gameplay, latest six stable no-compilation 5-second windows:

| Device log | Mean flips/s | Command core busy | Stage-A PostDraw ring-collect CPU time, sampled | SRT pipeline CPU time |
|--|--:|--:|--:|--:|
| TLOU-Android-Logs (16)(3).zip | 40.37 | 95.0% | 2.69 us/call at 3730 packets/frame, ~10.04 ms/frame | graphics pipeline ~5.51 ms/frame |
| TLOU-Android-Logs (19)(2).zip | 38.15 | 94.3% | 3.10 us/call at 3729 packets/frame, ~11.56 ms/frame | graphics pipeline ~4.95 ms/frame |
| TLOU-Android-Logs (20)(2).zip | 38.93 | 93.4% | 2.79 us/call at 3737 packets/frame, ~10.44 ms/frame | graphics pipeline ~4.45 ms/frame |

Extrapolations above multiply sparse sampled per-call CPU measurements by
actual calls/frame; they are **estimates**, not exhaustive traces.
Do **not** add the nested `command-draw submit-draw` and `command-postdraw`
times to produce a fake total. Stage B runs concurrently and must not be added
to Stage A. A fully GPU-limited interpretation is also unproven without GPU
busy timestamps.

Final 5-second window from log 20: mapped direct readbacks 19,229,
of which 19,209 did not need an extra submission. Their count is not 19,229
distinct GPU stalls. The preexisting dependent-table SRT grouping had no
compatible sibling groups in prior in-game tests; same-page SRT page-ahead
failed the synthetic timing gate. Do not reintroduce either as an FPS claim.

Draw-preparation workers reached effectively 100% coverage in log 20.
Yet `PipelineCache::TryPreparedPipeline` still calls `Info::RefreshFlatBuf`
per stage on the command thread because the worker's early read may be stale.
It invokes the native/portable SRT walker and the `SyncSrtTables` pre-walk.
Skipping this validation on a checksum or guest address alone is unsound.

## Newly isolated ARM64 deoptimization path

`flatten_extended_userdata_pass.cpp` constructs a native ARM64 SRT
walker via `Arm64Srt::Compile`, anonymous RW `mmap`, and RX `mprotect`.
The old code silently fell back to bytecode interpretation if the
executable mapping could not be created, with no log indicating this mode.
This patch logs the first failed mmap/mprotect and first working JIT.

`RunSrtWalker` formerly set `NativeSrtCode::enabled=false` forever
following **one** invalid guest pointer. In a title with transiently
unmapped resource tables, a walker could therefore execute through
the interpreter for every subsequent draw, even when source addresses
were valid. The scope and frequency of this actual failure remain
**unmeasured** on RedMagic; do not claim it has occurred or that FPS rose.

This branch replaces permanent deoptimization with bounded native
re-probes after 128, 256, 512 ... up to 32,768 interpreter calls. A
persistently bad walker therefore does not generate a SIGSEGV per draw.
Only one non-speculative caller probes at a time; speculative callers
keep their prior outer recovery boundary. The interpreter still runs
on the faulting draw; guest bytes, pointer semantics, SRT pre-walk,
readback ordering and packet snapshots are unchanged.

Native test:
`g++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread gow3_pc/tests/test_tlou_srt_native_retry.cpp -o /tmp/tlou-srt-retry && /tmp/tlou-srt-retry`.
The same logic was exercised with Clang and with GCC ASan+UBSan locally.
The runtime contract script also runs it on future Android native builds.

## Still unresolved — must verify before an APK claim

1. Is the native JIT actually created on the device? Look for first
   `SRT ARM64: native walker JIT active`, or the mmap/mprotect fallback reason.
2. How many SRT walkers enter interpreter mode during steady gameplay?
   A fault line now distinguishes actual native faults from speculation.
3. If mappings fail systematically, investigate the platform's permitted
   executable-code allocation; do not assume that FEX's own JIT strategy
   makes this different anonymous mmap executable.
4. After JIT mode is established, the larger measured hot spot remains the
   ~10.4 ms/frame Stage-A `CollectRingBindings` phase. The optional Stage-A
   sampled breakdown draft `b07497c` can distinguish clamp, GPU-dirty,
   pending-write and actual memory copies, **without moving snapshots**.
5. On-device matched heavy-scene A/B must confirm CPU time, FPS, 1% lows,
   visuals, physics and saves. Host tests validate fallback control only.

No APK, release branch change, speed claim, readback mode change,
or GPU synchronization change is authorized by these findings.
