# TLOU Remastered 01.11 — graphic-options audit (2026-11-08)

Input: user's 24,220,371-byte 01.11 SELF variants (SHA256 prefixes `3cfa475f...`, `31f3b0d1...`). A third supplied SELF duplicates the first. The two unique variants differ by 28 bytes outside the seven new light-shadow guard addresses. **No copyrighted executable is committed.**

## Implemented, optional and disabled by default

| Option | Renderer instruction(s) | Adjacent untouched instruction(s) | Explanation |
| --- | --- | --- | --- |
| Disable omni-light shadows | `0xA63BBA` | `0xA63BCA` | Read of shadow category `+0x14D` forced false, while omni non-shadow category `+0x14F` remains available |
| Disable spot/projector-light shadows | `0xA6445F`, `0xA652A4` | `0xA6446F`, `0xA652B8` | Spot shadow category `+0x14E` forced false, leaving spot/projector non-shadow `+0x150` untouched |

The render state base at `0xA638C4` is `0x181A770`. The game's own debug settings at `0x8BB2CE` and `0x8BB2FD` name corresponding **Enable Omni-Shadow Lights** and **Enable Spot/Projector-Shadow Lights** categories. We modify consumer reads, not initial settings that could be overwritten. Each replacement is eight bytes (`xor al,al` plus six NOPs); existing branch and non-shadow access instructions are untouched. All input/original bytes have been compared with both supplied SELF variants.

This is an unmeasured feature experiment, **not a proved FPS improvement**. Verify in the game that non-shadow lighting remains visible, shadowmaps are not left stale, and gameplay/rendering is stable. The game version **01.00 is unchanged**; no 01.00 executable was available in this set to validate these offsets.

## Investigated but not safe to expose yet

* **Screen-space reflections (SSR)**: present as a renderer stage `.DrawScreenSpaceReflections` at stage table `0x14F2A68`, and a debug parameter group `Screen Space Reflections` at `0xB111B1`. That group exposes trace parameters, but not an independently validated enable/disable branch. Blindly deleting the graph stage or zeroing trace steps may leave resource dependencies invalid. No SSR patch added.
* **High-resolution sun/local shadow buffers**: render-setting controls at `0x8B49F4` and `0x8B4A5C`. These identify options but not all runtime consumer paths. No writes added.
* **Cloud shadows / volumetric effects**: game option registration at `0x8BB6FE`; the buffer and render graph need producer/consumer tracing before safe bypass. No writes added.

All original-byte checks must remain strict, and all optional settings must remain **off by default** until measured on hardware.
