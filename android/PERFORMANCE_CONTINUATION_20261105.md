# TLOU Android CPU performance continuation — evidence and no-APK decisions (2026-11-05)

## Status and runtime evidence

This report is a checkpoint for a native-only optimization investigation, **not a claim of Android FPS improvement**. Sources: user-supplied archived `TLOU-Android-Logs (24)(1).zip`, `TLOU-Android-Logs (24).zip`, `TLOU-Android-Logs (23).zip` and original code at `a6e4577`. These archives represent DIFFERENT builds and launch settings: never mix their rates to infer per-frame costs.

Latest heavy-scene windows in `(24)(1)`: 36.5 flips/s, 0 shader/pipeline compiles, command thread ~75.8%, draw ~65.4%, Vulkan recorder ~51.0% of one CPU. GPU-ticks waits 1084.7 ms / 5.02s (~5.92 ms per game frame) are overlapping instrumented waits, not GPU duration. Command CPU ~20.77 ms/frame, draw CPU ~17.92 ms/frame, recorder CPU ~13.97 ms/frame. They run on separate threads and MUST NOT be added.

`TLOU CPU phases` (sampled thread CPU), stable window (~5 s): 289439 command-dispatch / 5.02 s -> ~1580 dispatches/frame and 388918 command-draw / 5.00 s -> ~2131 draws/frame at 36.5 fps. In one stable window:
- command-dispatch pipeline 1.421 us/dispatch -> estimated 2.25 ms CPU/frame; submit-dispatch 2.970 us -> 4.69 ms CPU/frame;
- dispatch-record buffers 3.533 us/dispatch -> estimated 5.58 ms CPU/frame; this is an inclusive sampled stage, not a proved savings opportunity;
- command-draw pipeline 1.225 us/draw -> ~2.61 ms CPU/frame (another stable sample was 2.181 us/draw);
- sample counts only ~38 calls / ~289,439 dispatches: use this to prioritize engineering, never promise a precise FPS gain.

## Older debug log is not a current baseline

The separate `TLOU-Android-Logs (24).zip` shows ~22–23 fps (not 36.5) with `TLOU_FAST_RENDER_CORES=1`. It pinned GPU-command, draw-recorder, Vulkan-recorder **and guest main** to CPUs 6,7, despite launch affinity allowing CPUs 0–7. The newer log's `launch.log` does NOT enable that option. This is a serious historical contention/confounding factor; do not reinstate the pinning and do not claim it single-handedly explains the FPS difference.

Old runs recorded up to 792,504 small stream copies and 942,225 static arena binds in ~5 sec; at ~22.6 fps this is about **7,013 copies/frame** and **8,338 arena binds/frame** IN THE OLD RUN, not new counts. They also showed draw stage-A pipe drain waits ~27–29% wall with compute (~11.7–12.8%) and SyncSrtTables (~4.9–5.9%), but those are older debug runs; removing their ordering is not safe. Vulkan readback producer waits and the 1 MiB lazy/poisoned-page guest-byte merge must remain correct.

## Changes native-validated here

Branch `tlou-draw-pipe-counter-gate`:
- Skip draw pipe per-packet FencedRDTSC x2 and busy_cycles atomic when `GOW3_FRAME_STATS` is unset. ARM64 native benchmark saving 63.3 ns / packet vs legacy; 3720 draw+dispatch/frame -> **~0.236 ms CPU/frame only under a one-packet-per-draw/dispatch assumption**. The actual mix can differ. Optional diagnostic mode preserves the timer.
- Coalesce pending-write-filter 64 KiB granules and GPU-written bitmap atomic ORs by 64-bit word; preserve exact modulo/collision behavior, single-window fast path, and concurrent atomic updates. ARM64 synthetic 512 granules (32 MiB of virtual address coverage): old/new 626.7/17.3 ns pending and 297.9/17.5 ns atomic. **No current in-game histogram of 32 MiB write records**: these strong isolated ratios cannot be multiplied by the total count of small uploads or passed off as frame/FPS gains.
- Random equivalence, alias/collision, concurrent writers, ASan, UBSan on x86-64 and native ARM64 succeeded:
  https://github.com/erickmendes22/tlou-remastered-android/actions/runs/42029669177
- The **full native-only ARM64 Bionic/FEX/Vulkan renderer/runtime build succeeded**, without APK:
  https://github.com/erickmendes22/tlou-remastered-android/actions/runs/42031046809

Separate branch `tlou-stream-copy-directcache`: exact-key direct-mapped GOW3_STREAM_REUSE=2 optional cache, legacy mode=1, default mode=0 unchanged. ARM64 native synthetic, 4000 lookups/frame, direct map vs existing map: 0.14–0.21 ms/frame, depending on artificial duplicate rate. It has fewer repeat hits on collisions. **REJECT for promotion**: this comparison is NOT vs default-off (no lookup), observed savings are marginal and in-game hit coverage is unknown:
  https://github.com/erickmendes22/tlou-remastered-android/actions/runs/42032611952

## Concrete gates and safe future targets

1. The largest measured **CURRENT** CPU work is compute BindBuffers (sampled ~5.6ms/frame) and command dispatch packet construction (~4.7ms/frame). Investigate exact shader/buffer residency state with a low-overhead stage-level phase split or offline trace: calculate saved nanoseconds per real call x actual ~1580 dispatches/frame, and avoid eliding per-buffer readback/dirty/alias ownership.
2. The current ~5.9ms/frame `gpu-ticks` are instrumented waits, not automatically duplicated Vulkan submissions. Previous temporal SRT prefetch and compute fence experiments had no sustained improvement. No unsafe speculative readback or removed producer signal.
3. SotC DrawReplay/FAST_BIND's 2483 consecutive indirect duplicate draws/frame and +2.74FPS are game-specific. TLOU only has ~2131 draws/frame total, without a measured repeat/equivalence rate. Do not blindly port its reuse/epoch shortcuts.
4. The device baseline is existing log 23/24, not a new OFF/ON run with unrelated launch changes. Only combine native-proven corrections into a *candidate APK* once the costed path predicts a meaningful frame-time improvement and correctness gates pass.

Readback 1 and existing CPU/GPU write/guest state ordering stay untouched. No APK created by this work.
