# Log 24: unchanged gameplay limit, measured local reductions

## Decision

Do not publish another APK from these changes. Keep them on the validation branch.
The new tests establish lower work and lower native ARM64 cost for specific functions;
they do not establish a substantial reduction of the game's command-thread bottleneck.
The Android release branch and v0.30 APK remain unchanged.

**Per-frame follow-up:** [LOG24_PER_FRAME_BUDGET_20261105.md](LOG24_PER_FRAME_BUDGET_20261105.md)
converts absolute saved ns/call into CPU ms/flip scenarios using observed draw/dispatch
rates. The original ring benchmark's `ns/packet` label refers to ONE stage list, not
a complete draw. Its output now says `ns/stage`. Missing device coverage is explicit.

## Separate archive comparison

Read `tlou/runtime.log` separately from log archives 22(2), 23 and 24(1).
Do not compare against `runtime-previous.log` inside archive 24.
Use the first two heavy gameplay windows with no pending draws and no worker compilation.
These windows follow the loading transition. There are no camera/frame IDs establishing
an identical scene, so this is a comparable workload check, not an exact scene A/B.

| Archive | Flips/s | Command CPU ms/flip, approximately | GPU tick waits ms/flip, approximately |
| --- | --- | --- | --- |
| 22, first two clean heavy windows | 36.3 / 35.6 | 21.16 / 21.49 | 5.87 / 5.98 |
| 23, first two clean heavy windows | 37.1 / 36.7 | 20.70 / 20.71 | 5.84 / 5.93 |
| 24, first two clean heavy windows | 36.5 / 36.5 | 20.96 / 20.77 | 5.98 / 5.92 |

Command CPU is thread utilization times the measurement interval, normalized by flips.
Waits are overlapping instrumented wall times, not GPU execution time. Do not sum these
columns into an exact critical path. Window boundaries are approximate.
Log 24 gives no convincing gameplay gain over log 23. In particular, the native PM4
checksum function benchmark improvement did not produce a measurable gain here.

The new samples identify about 58,000 compute dispatches and 78,000 draws per second in
the heavy windows. Compute packet submission averages about 2.97–3.42 us in the selected
samples; recording buffers averages 3.30–3.53 us. Sample means include clock overhead and
only 38 selected calls per window. Do not multiply these means by all calls to construct
a budget, or treat them as representative per-shader costs.

## Candidate 1: avoid repeated mapping searches under one read lock

`runtime_memory_clamp` searched the same immutable table twice at the same address.
`runtime_memory_copy_backing` searched again for every successive mapping/gap.
Keep one starting binary search and walk forward while holding the existing read lock.
Keep the existing generation checks, thread cache, reservations, physical alias offsets,
gap zero filling, memcpy behavior and lock scope. No guest data is retained or reused.

The test includes the actual production C file and a frozen v0.30 reference, not a model
of the proposed code. 50,000 randomized cases compare lengths and bytes, including holes,
reservations, aliases, zero sizes, cache hits/misses and write-lock generation changes.
Guard bytes before/after the output must remain identical. ASan/UBSan also pass.

Compiler instrumentation in a separate correctness binary counts entries into the real
`vma_index`; it is absent from the benchmark and runtime binaries. Required work gates:

| Operation | Old binary searches | New binary searches |
| --- | --- | --- |
| Clamp cache miss | 2 | 1 |
| Clamp cache hit | 0 | 0 |
| Three-region copy | 4 | 1 |

Native ARM64, two benchmark invocations, nine alternating post-warmup pairs per case:

| Case | Old ns/call | New ns/call |
| --- | --- | --- |
| Clamp, one hot mapping | 3.44–3.47 | 3.44 |
| Clamp, rotating 512 mappings | 70.43–70.95 | 45.01 |
| Clamp, cross mappings | 73.14–73.55 | 45.17–45.47 |
| Copy 128 B, hot mapping | 7.31 | 7.25–7.27 |
| Copy 128 B, rotating mappings | 81.84–82.46 | 55.48–56.84 |
| Copy 48 KiB, across mappings/gaps | 851.63–873.34 | 733.99–757.94 |

On x86-64 the hot clamp regresses from about 3.00 ns to 3.82–4.09 ns, despite unchanged
logical cache work. That regression is retained in the evidence, not hidden in an average.
Other x86 cases improve. This is another reason not to promote the candidate as a general
release optimization from the isolated ARM64 result alone.

The synthetic mapping distribution and cache-miss frequency are not measured gameplay
distributions. The tables guarantee neither a particular number of saved ms/frame nor FPS.

## Candidate 2: consume packet-local ring bindings in order

`CollectRingBindings` appends eligible buffers in ascending buffer index, at most one per
index. `BindBuffers` visits indices in the same order. Replace each restarted linear
search with a cursor scoped to that invocation, and locate the shader stage once.
No descriptors, guest bytes, access states or handles are reused between draws.
The cursor requires strictly increasing consumer indices and sorted producer bindings;
the actual producer/consumer loops meet that contract. Do not use it for arbitrary queries.

100,000 randomized sorted/sparse/empty packet lists return the exact same pointers as the
old linear search. Deterministic comparison counting through the production template
requires 820 -> 80 comparisons for 40 bound buffers. ASan/UBSan pass.

ARM64 lookup-only cost in ns per stage list (not a whole draw), two runs:

| Buffer slots / ring bindings | Old | New |
| --- | --- | --- |
| 1 / 1 | 1.47 | 1.32 |
| 4 / 2 | 8.00 | 3.83 |
| 8 / 4 | 19.45–19.75 | 7.65–7.66 |
| 16 / 16 | 62.23–62.51 | 18.87–19.48 |
| 40 / 40 | 319.68–320.09 | 47.97–48.66 |

This benchmark intentionally excludes Vulkan, buffer tracking and repeated stage lookup:
it proves the cursor's local cost, not `BindBuffers` total time. At eight slots the saving
is only about 12 ns/packet. A large relative improvement in this lookup alone is not a
foundation for promising a meaningful gameplay improvement. The full rasterizer integration
has not been cross-compiled or run on the phone at this checkpoint.

## Reproduction and gate

GitHub Actions run: https://github.com/erickmendes22/tlou-remastered-android/actions/runs/42018906780
Both native architectures and their ASan/UBSan checks passed. The subsequent validation
commit adds deterministic operation-count assertions and preserves the experimental
rasterizer integration on the validation branch only.

Tests: `test_memory_walk_cost.c` and `test_ring_binding_cursor.cpp` in `gow3_pc/tests`.
Build with C11/pthreads or C++20 respectively, `-O2 -Wall -Wextra -Werror`.
For memory operation counts add `-finstrument-functions -DTEST_VMA_LOOKUP_COUNTS` and
run `--correctness-only`. For sanitizers use `-O1 -g -fsanitize=address,undefined`.
Benchmark timing binaries must not include function instrumentation or sanitizers.

The current changes do not remove SRT producer waits and must not be described as doing so.
Promotion needs evidence of material absolute savings in the actual resource path, then
native runtime compilation. An APK is not the next gate simply because unit tests pass.
