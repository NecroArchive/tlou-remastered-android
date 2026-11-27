# 1.11 investigation: distant sun shadows and secondary vegetation

Status: static evidence and distance candidate, NOT a shipped patch. Both shadow changes must share one optional UI/AOT group. No active patch definitions, settings, shader rules, or release workflow changed in this audit.

## Executables
All six available user-owned 24,220,371-byte SELF files were reopened. Two SHA-256 variants:
- 3cfa475f88ca68485b6b9230e62b84e195cdbe19e6f7295c110b8dcf8f20884e (4)
- 31f3b0d120803878d10a7184d139c545de334394dd7dd6ca6058a7c995e484a7 (2)
Offsets below are linked text offsets; raw SELF text starts at 0xac90 in these six files. No SELF stored here.

## Shadow distance
Registration 0x8bd783/0x8bd7f2/0x8bd843/0x8bd894/0x8bd8ef establishes override and cascade fields 0x39c/0x3a8/0x3ac/0x3b0/0x3b4. 0x8bd940 registers max sun shadow distance (field 0xf0). Real consumer 0xb9b190, called at 0xb9b693 before sun frustum construction. Normal path limits max distance by fields +0xf0, +0x1038 and camera +0x24c. Subsequent max/ordering/clamp logic stays intact.

Candidate replaces 64 bytes at 0xb9b2a2..0xb9b2e1. It eliminates redundant integer count cap (min unsigned(count,3) equivalent to original signed compare then unsigned final cap, including negative bit patterns), adds multiplication of the already clamped max distance by existing read-only float 0.75 at 0x10d0d10, preserves original three limits, next instruction at 0xb9b2e2, and the separate alternate branch. Original count arithmetic checked with 100011 integer cases and algebra. Assembled and disassembled locally. The original 64 bytes and float guard matched all six files. No direct rel32 branch targets interior identified by conservative raw scan; one raw short-branch candidate is an operand byte of vmovd, not a branch. Still requires native AOT variant coverage and device testing. Do not conflate maximum distance with proportional reduction in every cascade or FPS.

Original:
`498b4f10c5fa1091f0000000c5ea5d9138100000b904000000c5ea5d904c020000418b84249003000083f8050f4cc8b80300000083f9030f42c18983b0000000`

Candidate:
`498b4f10c5fa1091f0000000c5ea5d9138100000c5ea5d904c020000c5ea59154a5a5300418b8c2490030000b80300000083f9030f42c18983b0000000909090`

Read-only guard 0x10d0d10: `0000403f`. This is the same guarded constant used by the LOD variant, not a new write.

## Shadow update frequency
- 0x8bd546/0x8bd54d: Cache Sun Shadow registration, field 0x151 at render settings base 0x181a770. 0x8bd57a: separate cached light position field 0x152.
- 0xb9be1a calls per-cascade frustum builder 0xb99860; 0xb998ad initializes validity byte +0x276. 0xb99b76 checks cache toggle, 0xb99b84/0xb99bc0 use containment factors 0.52/0.60, 0xb99bda invalidates reuse on bounds movement. Other branches invalidate it as well.
- 0xa8a302–0xa8a355 uses the toggle and validity byte to decide a call in the first-cascade clear path; later rendering still occurs (0xa8a460, 0xa8a768). Do NOT call that a frame-rate throttle.
- 0xa91890..0xa91a1c queues successive sun shadow draw jobs with per-cascade index. 0xa91a22..0xa91a44 schedules separate ResolveSunlightShadow callback 0xa8a7c0. Cannot safely discard a whole job or reuse texture with mismatched matrices/atlas.
- Required next proof: capture per-cascade id, cache validity, current/previous matrix, atlas image identity, dynamic caster/dirty event, frame id, job completion; establish whether far cascades can retain valid depth on skipped frames. Do not disable invalidation or omit resolve.

## Vegetation
13 exact grass strings all belong to material-name lists (stone, grass, dirt, clay, etc.), not draw density. kLayerVegetation and kLayerVegetationBlocker are not a validated render-only selector. Draw Foliage Pusher Sphere reference 0x66e24e is a debug label, not density. kModFlagWind at 0xc2f0c7 is the JSON mesh parser (json-mesh.cpp), not proof of grass-only membership; wind includes other objects. HasInstanceFlags and SetInstanceFlags refs 0xae30cb/0xae33b1 are generic. Global Disable Background Meshes / Disable Alphablend would remove unrelated scenery and must not be used.
Required next proof: actual vegetation mesh/submesh/material metadata or scene capture identifying nonessential render-only instances, then trace the render submission/LOD selection and confirm collision/AI paths are separate. No density percentage or safe executable write established.

14 additional trace spans (cache/renderer/mesh registration) were compared across all six binaries; each matched. This validates shared addresses/bytes only, not functional performance or safe skipping.
