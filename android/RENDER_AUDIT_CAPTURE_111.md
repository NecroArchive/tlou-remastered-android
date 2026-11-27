# Remaining investigation: 1.11 shadow frequency and secondary vegetation

The guarded shadow-distance patch is integrated in sun_shadow_quality, off by default. The actual original/replacement x86 blocks were executed in a local x86-64 harness for 100,011 randomized finite nonnegative distances and uint32 cascade counts; the count and the 0.75 scaled distance matched. Downstream clamps and the alternate renderer path are unchanged. Not an Android performance claim.

All ten audit instruction spans were compared across six user-supplied SELFs (two SHA-256 variants), 60 matches. The native slot catalog verifies original instruction bytes and executable segment bounds. Slots are not installed unless the launch preference is enabled. No game registers, matrices, buffers, draws, or shadow flags are modified. Native coverage and device behavior remain to be checked.

Capture fields (a,b,c,d,e are hexadecimal, UINT64_MAX denotes a failed read):
- frustum: cascade index, object pointer, validity byte, hash of 64 bytes at +0xc0.
- shadow_job: cascade index, object pointer, validity byte, atlas mode, Cache Sun Shadow flag.
- atlas_clear: atlas resource pointer, layer count, context.
- shadow_submit: cascade index, context, job pointer.
- atlas_resolve: atlas resource pointer, layer count, context.
- shadow_enqueue: cascade index, job argument pointer, two scheduling arguments.
- resolve_enqueue: three scheduling arguments.
- background_classify: prototype pointer, instance pointer, metadata pointer, billboard classification byte.
- billboard_transform: model instance pointer, transform buffer pointer, transform count.
- billboard_upload: upload object pointer, matching transform buffer pointer, upload byte count.

Timestamps are monotonic nanoseconds, not confirmed frame numbers. Context and enqueue order help correlate events; a pointer match alone does not prove stable ownership across frames. The bounded log records at most 1024 samples per site. Upload matching is by a rolling set of 512 recent transform pointers, not proof of plant identity. Keep other visual patches unchanged, especially disable-sunlight-shadows OFF. A short outdoor sequence with camera still then moving and a dynamic caster crossing distant shadows is needed. Compare frustum hashes/validity, clear/resolve resource and layers, submission ordering, and billboard classification/count/upload; only then choose a per-cascade reuse or draw reduction policy. Do not benchmark with audit enabled. Missing hooks or memory samples must be treated as missing evidence, not zeros.

The static trace also located the generic transform setter b764f0 and generic upload b77d98: the same setter is called for interactive objects, so reducing that global count would affect unrelated geometry. The billboard loop aa263d allocates and publishes the full buffer before filling transforms, so shortening only its fill loop is unsafe. The shadow clear call a8a355 receives total cascade count; do not skip far submissions while their atlas layers are erased. No frequency or density patch is applied.
