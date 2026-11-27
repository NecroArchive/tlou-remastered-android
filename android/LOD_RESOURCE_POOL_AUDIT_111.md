# 1.11 geometry LOD and timeline resource reuse (partial integration)

Base: 3ef2cc418a8b1867b6899d06ce04bba206b8262d. Both user-owned 24,220,371-byte SELFs were inspected; SHA-256 3cfa475f88ca68485b6b9230e62b84e195cdbe19e6f7295c110b8dcf8f20884e and 31f3b0d120803878d10a7184d139c545de334394dd7dd6ca6058a7c995e484a7. No game file committed.

The geometry selector at 0xACF0D0 uses three scale terms (default 1.0) and the independent offset. At 0xACF111 its third scale operand is loaded via the address of global foreground LOD scale; only this address calculation is redirected to existing 0.75f at 0x10D0D10. Existing arithmetic, level-count checks, clamping, and separate disappearance check at 0xACF395 are unchanged. The visibility collector at 0xA97821 retains its original scale. This is an optional variant, off by default; actual visual quality and FPS need on-device testing. The catalog revision must change so old AOT code cannot silently ignore it.

ResourcePool previously captured gpu_tick by value in search; the refreshed completion value was not used, and the search after the hint queried the driver before searching the cached prefix. Search the whole ring on the cached tick first and repeat with the proven refreshed tick only if an older submission can have completed. TrimRing shares the existing StagingCompletion snapshot; never frees an uncompleted buffer. No relaxed synchronization.

This branch is not the requested complete performance build. Distance/frequency of shadows, SSR off, secondary vegetation and remaining CPU/GPU hot-path work are not validated here. No FPS gain or gameplay stability is claimed.
