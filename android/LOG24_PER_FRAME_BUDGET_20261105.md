# CPU cost budget from log 24

**Status: scenario calculations, not measured device or end-to-end savings.**

Observed: 36.50 flips/s, 27.397 ms/flip; approximately 2131.4 draw-record calls and 1588.7 dispatch-record calls per flip.

Phase windows and flip windows are asynchronous. Dispatches handled by HLE or native copy/clear paths can return before BindBuffers, so phase call counts overstate coverage when assigned to every binding stage. No subtraction for that is possible from this log.

## Ring cursor: per-stage cost, not per-draw cost

Assume TWO populated graphics stages per draw and ONE compute stage per dispatch, with every counted call reaching binding. The log does not establish those assumptions. The test measures one stage list, despite the original ns/packet label.

Scenario binding stages/flip = 2 × 2131.4 + 1588.7 = 5851.6.

| Slots / ring bindings per stage | Saved ns/stage | CPU ms/flip, draw thread | Relative to 27.4 ms frame duration, NOT FPS gain |
| --- | ---: | ---: | ---: |
| 4 / 2 | 4.170 | 0.0244 | 0.089% |
| 8 / 4 | 11.945 | 0.0699 | 0.255% |
| 16 / 16 | 43.195 | 0.2528 | 0.923% |
| 40 / 40 | 271.570 | 1.5891 | 5.800% |

Multiply each row by the fraction of eligible calls actually covered. For one populated graphics stage use a factor 0.636; for five use 2.093. These are alternative scenarios, not measured stage counts or physical bounds.

Ring lookup runs on the DRAW recording thread (stage B). Saving its CPU time does not directly remove CPU time from the COMMAND thread (stage A). It can affect throughput only through dependencies, contention or a changed bottleneck.

## Memory queries: coverage is not recorded

The log has no per-thread clamp miss counts, copy sizes or crossing distribution. An actual total cannot be calculated. Hot clamp hits save approximately zero. Use the sensitivity table below; do not substitute total buffer binds for misses.

| Case | Saved ns/call | 1,000 eligible calls/flip | 10,000 eligible calls/flip | Eligible calls/flip needed for 1 ms CPU saving |
| --- | ---: | ---: | ---: | ---: |
| clamp miss, rotating 512 mappings | 25.68 | 0.0257 ms | 0.2568 ms | 38,941 |
| 128 B copy, rotating mappings | 25.99 | 0.0260 ms | 0.2599 ms | 38,476 |
| 48 KiB copy, across mappings/gaps | 116.52 | 0.1165 ms | 1.1652 ms | 8,582 |

The two copy rows are different workloads, not additive savings for one copy. Each eligible call belongs in one category. Split memory savings by CPU thread; do not sum parallel threads into critical-path time.

## Gate for current and future changes

Report before approving an APK: (1) old/new absolute cost and benchmark platform; (2) actual eligible count per frame, by thread, or explicitly missing coverage; (3) saved CPU ms/frame by thread; (4) dependency/critical-path coverage; (5) regressions and invalidation cases. Unknown coverage means unquantified game impact, not zero cost and not a guessed benefit.

Do not multiply a function percentage by whole-frame time. Do not infer an FPS gain from a reduction on a noncritical thread. Do not add overlapping sample phases or GPU waits.

Cost source: https://github.com/erickmendes22/tlou-remastered-android/actions/runs/42018906780
Platform: GitHub ubuntu-24.04-arm; not the phone.

