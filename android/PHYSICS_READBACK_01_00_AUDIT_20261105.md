# TLOU Android 01.00 — physics-result EOP coverage (2026-11-05)

## Evidence examined

- User-provided `TLOU-Android-Logs(3).zip`, two runtime sessions.
- Device log identifies `CUSA00552`, version `01.00`, 720p and Turnip Gen8 v38 Patched.
- The contemporaneous v0.1 APK's `lib/arm64-v8a/libgow3gpu.so` **contains** the
  `Physics result readbacks`, `EOP readbacks (5 s)`, and
  `GOW3_EOP_READBACK_SHADERS` diagnostic strings. The old selective EOP code is
  compiled into that native library.
- Neither runtime session logs a physics-result/EOP readback, and neither has
  the PC-specific GCN hashes `106beafd` or `e30509e8`. That is evidence of
  **no observed matches**, not a definitive claim that no matching dispatch
  occurred anywhere. Those messages are conditional on results being queued.
- The original PC fix for the stretching hair, the `RangeSet` implementation
  with `std::map` of disjoint half-open intervals, is already present in
  `video_core/buffer_cache/range_set.h`. Do not port it a second time.
- The checked `eboot.bin` SHA-256 is
  `e5f0aa322f60bd90637cc10d83de08c77885b1921084021c630e04ed3d6febf7`.
  It establishes the known game binary, **not** the runtime shader hashes:
  shader programs can be loaded separately from the game content.

## Bounded implementation on the Android branch

- Preserve `GOW3_READBACKS=1` and `GOW3_EOP_READBACK=1`.
- Recognize the two PC shader hashes and their slots exactly as before.
- An Android-only supplementary route accepts **only** exact known result
  record layouts: slot 6: 89 × 48 = 4272 bytes; slot 5: 135 × 48 = 6480 bytes;
  only written compute storage bindings, with EOP mode enabled, and only for
  hashes not already on the PC list. No other buffer is matched by this route.
- The matched range uses the existing asynchronous, read-protected EOP
  download, tied to the relevant compute-release/graphics fence. This is
  **not Readback 2** or a global wait.
- The fallback can be disabled with `TLOU_PHYSICS_SHAPE_READBACK=0`.
- Temporary `TLOU_PHYSICS_COVERAGE=1` records up to 40 distinct likely physics
  compute-binding signatures; it logs hash, slot, stride, record count, bound
  size, PC-hash recognition and exact-layout fallback recognition.
  No additional readback, fence wait, pipeline capture or GPU timing is added.
- A host C++20 source-level check of the exact-layout predicate passed 203,776
  combinations, including negative cases. This verifies the matcher, **not**
  the real Android shader coverage or visual correctness.

## Interpretation of the next diagnostic capture

- `TLOU physics coverage: ... pc=1`: original hash-specific path is receiving
  this binding. Check the `EOP readbacks` counter and fence behavior.
- `... pc=0 shape=1`: alternate hash with one of the validated record shapes;
  the Android narrow fallback queues the regular EOP readback.
- `... pc=0 shape=0`: the hashes/layouts being seen do not qualify; inspect
  slot, stride, record count and size before extending any readback coverage.
- No coverage lines at all during deformation: this diagnostic did not see a
  candidate of the selected size/stride/slot, so the fallback cannot be
  credited with fixing the bug. Review skinning bindings and CPU/GPU ranges.
- Check `Physics result readbacks`, `EOP readbacks`, and whether the
  deformation persists. Do **not** claim a fix or FPS improvement without
  a controlled device test on the same save/scene.

No new APK is asserted by this document.
