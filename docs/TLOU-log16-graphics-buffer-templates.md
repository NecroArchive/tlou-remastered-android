# Log 16 cold-device reference and graphics buffer-only candidate

## Device evidence

The user confirmed that the pointer correction in `8cd5d59` restored the image.
They then clarified that log 15 used a hot device and supplied
`TLOU-Android-Logs (16).zip` as the colder test. Do not attribute the difference
between these sessions to a code regression or optimization.

Selecting gameplay intervals below 75 flips/s and fewer than 100 pending draws:

| Session | Samples | Median flips/s | Median recording wait per 5 s | Median host-copy wait per 5 s |
|---|---:|---:|---:|---:|
| Log 15, hot | 8 | 54.55 | 843.25 ms | 562.65 ms |
| Log 16, colder | 7 | 56.9 | 796.6 ms | 532.8 ms |

Log 16 includes a 60.7 flips/s interval. The strong-core clock samples during
gameplay are generally 3.76-4.19 GHz; log 15 fell to 3.13 GHz. The waits are
overlapping wall-time measurements, not independently additive costs. Scene,
warm-up and clock differences prevent a controlled before/after FPS claim.

## Rejected probe

A real software-Vulkan probe recorded identical push-constant, descriptor and
dispatch streams with and without repeating `vkCmdBindPipeline` for an already
bound compute pipeline. It saved only 6-14 ns/dispatch locally, equivalent to
0.011-0.025 ms at 1800 dispatches/frame. No production bind-skip change was made.

## Candidate

Extend the existing compact buffer-only descriptor template to graphics pipelines
whose compiled stages have no image or sampler descriptors. Count the immutable
buffer layout once after pipeline-layout creation. Select the Vulkan template's
GRAPHICS bind point for graphics; COMPUTE is unchanged. The existing per-call
guard still checks binding/count/type, source-array bounds and exact source
pointers, and copies fresh descriptor infos for every draw. Mixed image/sampler
layouts, fewer than four buffers, unavailable entry points and unusual writes
retain the prior path. No draw, barrier, resource access, readback, game shader,
image resolution, UI setting or synchronization wait is removed.

Use the shared production C++ wrapper with its explicit raw-void-pointer cast;
do not reintroduce the Vulkan-Hpp typed-pointer overload defect.

## Validation

The graphics test creates actual vertex/fragment shaders, an integer color
attachment and graphics pipelines. It renders and reads back 64 pixels through
each descriptor API for 2/4/8/16-buffer layouts, changing offsets and ranges per
draw and comparing every output against the CPU-computed expected value.
The two-buffer case is tested for API parity but remains ineligible in production.
The same production C++ wrapper is used. All local output checks passed, as did
the validation layer with synchronization checks enabled. Compute checks and the
100000-payload Vulkan-Hpp pointer regression test remain included.

Local Mesa llvmpipe/LLVM19 recording cost (alternating trials, two warmups and
40 measured samples, median driver ns/draw; source snapshots excluded):

| Buffers | Ordinary push | Template push |
|---:|---:|---:|
| 4 | 133.8 | 61.7 |
| 8 | 179.1 | 72.5 |
| 16 | 508.9 | 104.3 |

These are software-driver binding costs, not TLOU frame times or Adreno results.
The runtime distribution of eligible graphics layouts is not established.
Native ARM64/x86-64 pixel, pointer and benchmark gates must pass before the full
Bionic renderer compilation and same-signed APK packaging proceed.
