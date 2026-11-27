# Follow-up on logs 27/28
Parent ef86e7f03a6bf03ad356b993cda68cad3de7e6b3; existing graphics settings and AOT catalog retained. Logs 27/28 show EOS host-copy waits ~200-259 ms/5 s, roughly 1300 EOP cap flushes/5 s (two thirds age), 2192-2456 submissions/5 s in log 27, zero completed-at-return, ~40-48k timeline queries on one thread. These are overlapping wall times, not GPU occupancy.

1. EOS fast path checks live page ownership and exact pending readback pieces, not the permanent 64 KiB historical bit. EOP/DMA queues are tested for page overlap; EOS never consumes their unrelated entries. Global deferred-fence and image-download gates stay intact, GDS stays synchronous. Counters separate each reason.
2. Pending readback lookup uses the existing maintained window index before exact scan. Force poll lands cached completed prefix and queries at most once when necessary. Enable the existing completed-only SRT prefetch, limited to two candidates per observed completion and at least three accesses; no speculative wait or submit. Same late CPU/GPU ownership checks and fallback.
3. Bounded EOP group cap 24 events/700 us (previous 16/450); all unmet guest waits, faults and natural submits retain their existing paths. This is tuning based on measured age caps, not an assertion of faster gameplay.
4. Opportunistic draw/pending-op polls share a 50 us query gate. Mandatory waits and resource pressure refreshes unchanged. Skip unconditional post-submit query except timestamp collection samples.
5. Recorder recycles and dequeues the next published FIFO chunk under one lock without a false idle state; never reorder a command or relax guest-copy completion.

No new graphics patch. No device performance or stability measurement on this revision yet.
