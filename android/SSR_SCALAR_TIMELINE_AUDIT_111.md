# SSR native off path, scalar update and timeline wait

Parent: 3a3f3e803dfd12f5996d9f52692612bc6b08080f. The 1.11 SSR source string assertions resolve to preparation at 0xB11780; its sole direct caller at 0xA93A79 is followed by the draw routine 0xB11E90 at 0xA93A84. Both are skipped by native disabled branch at 0xA93A3E -> 0xA93A89. Six original-byte guards match all six user-owned SELFs (four 3cfa475f..., two 31f3b0d1...). The patch changes only JE to JMP, preserving original disabled control flow. No asset committed. AOT variant profile v5; optional and off by default.

Android mandatory timeline waits use vkWaitSemaphores directly if cached completion is insufficient, eliminating the redundant pre-wait counter query while preserving the exact target and error/timeout path.

Scalar HLE write-through updates 4..256 aligned contiguous bytes in a single recorded updateBuffer command, preserving host write, destination hazard tracking, original dirty-range cleanup and fallback staging for other sizes. The data is snapshotted in the record chunk (or consumed synchronously in direct mode). This does not group separate scalar dispatches or remove the guest-order barrier. No device FPS/compatibility result yet.

This is not the complete requested build. Shadow distance/frequency, secondary vegetation, EOS ordering, proactive readback and AOT tuning remain under investigation. No APK dispatch.
