# TLOU Android — log 16 CPU critical path (7 November 2026)

## Input and source integrity

User test ZIP: `TLOU-Android-Logs (16)(3).zip` (not committed).
Development base: `tlou-ui-approval-log32-20261105` @ `b9e2fec`.
Target device: Nubia NX789J, Adreno with Turnip Gen8 V38 patched, direct 720p, Readback 1, Full geometry.

Heavy steady windows: about 39–41 FPS, command thread ~94–95% of one core,
draw thread ~83–84%, record thread ~55%. Shader compilation is absent in
these windows. These thread CPU times overlap and must NOT be added.

`command-postdraw` reports about 740–760k packets per 5 seconds and sampled
`ring-collect` about 2.1–3.2 microseconds/packet, corresponding to roughly
8–12ms per displayed frame *if sampled calls represent the steady mix*.
The counts are real, but the product of sampled mean and total calls is an
estimate, not a continuous CPU profiler. This is the immediate high-coverage
hot path; shader compile, power throttling and VSync are not the explanation.

The constant ring maps as HOST_COHERENT on this device, so the recently
introduced batch flush does not save noncoherent VMA barriers here.
Changing more flush grouping is low priority.

## Change with direct correctness argument

`Rasterizer::PendingWriteOverlaps` prunes on every 64th eligible overlap
query. Previously it unconditionally reset the 64-KiB bloom and range bounds
even when `std::erase_if` removed nothing. A no-op erase does not change the
range list, pending bounds, bloom coverage or spatial index, so retaining them
is equivalent. After an actual erase all are still rebuilt.

Synthetic randomized native model (400 sequences * 1300 operations, with
up to 2048 intervals, random arrivals, retirements and overlap queries)
passed on x86-64 and ARM64. A no-retirement microbenchmark improved the
*prune bookkeeping routine* by about 2.2x (64 active ranges) to 9.6x
(1024 active ranges). A hypothetical 250k-query workload with 1024 ranges
saved about 11.5ms total on one host run, only ~46ns/query: multiplied by a
few thousand queries/frame, this is <1ms/frame. DO NOT call it a meaningful
FPS gain or cut a special APK for this alone.

## Android policy correction

`PortSettings` had `GOW3_ASYNC_READBACK=1` despite source comments warning
that global protection of unrelated pending downloads had caused extra
faults/regression. `BufferCache::SafeReadbacks` enables global protection
only when value == 1. Without the variable, `SyncSmallGpuWrites` remains
asynchronous by default (it disables asynchronous copies only when value
equals 0). The explicit `DownloadRangesAsync(eop_readbacks,true,true)`
still protects fence/physics results, preserving Readback 1 and Full sync.
Thus *remove the variable*, do not set it to 0.

Also retire an SRT temporal prefetch mode already shown to have no sustained
FPS advantage, and skip the already-benchmarked per-packet draw-pipe timer
when frame profiling is disabled. These are policy cleanup, not a demonstrated
new FPS result.

## Still outstanding

- Subdivide `CollectRingBindings` into shader-sharp extraction, VMA clamp,
  GPU modified test, pending-write query, guest copy, ring allocate. A
  sampled phase total alone cannot prove which internal operation to cache.
- For compute, attribute the `dispatch-record buffers` scope between
  `GetSharp`, `ClampRangeSize`, `ObtainBuffer`, descriptor writes, and
  texture/physics access. Never skip readback or GPU/CPU dirty operations.
- Require matched-device 5s windows, 1% lows, on-screen geometry/physics
  and crashes before claiming a faster release. No ARM JIT changes made.

## CI

- x86-64 and native ARM64 correctness: workflow 42372787457 (success).
- Android source checks: workflow 42372753530 (success).
- Candidate maintained as **draft PR #2**; no benchmarked in-game gain and
  no APK release claimed.
