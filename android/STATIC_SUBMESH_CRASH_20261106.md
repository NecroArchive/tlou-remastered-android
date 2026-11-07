# Static collision-result bounds crash, Android 01.00

Evidence: user `TLOU-Android-Logs(4).zip`, **runtime-previous.log**, launched
2026-11-05 22:20:07 America/Sao_Paulo. The current runtime is a separate run
and is not the crash under investigation. The supplied SELF has SHA-256
`e5f0aa322f60bd90637cc10d83de08c77885b1921084021c630e04ed3d6febf7`.

The previous run stops at the game's `iSubMesh < m_numSubMeshInstances`
assertion in `GetSubMeshInstance`, mesh.h:1233, followed by RIP/address zero.
The guest frame chain includes ELF-relative F76664, 486A, AAD921 and A8FFC2.
F7665F calls F74EC0 to consume completed collision/ray hits. Its first record
is still being processed (preserved r14=0); r12=0x7620 is 30 * 0x3f0, the
static mesh record stride. Thus the static path is implicated, rather than
the other inlined assertion in the dynamic path.

At F7517F the static path tests a hit's submesh index against its count,
then asserts when invalid. The neighboring dynamic path, F753BE, already
skips out-of-range hits via F75410. The patch applies the same behavior to
the static path: reject nonpositive counts and unsigned out-of-range indices
(including negative/sentinel indices) before forming or reading the submesh
address, then continue the existing result loop. It does not substitute
index zero, disable collisions, return out of the whole result consumer,
or disable the assert handler globally. Valid hits retain the original
address calculation and processing. The existing event release and result
queue cleanup remain in their original order.

This prevents the identified fatal access; it does **not** establish why
the producer returned an invalid hit. Streaming/unload activity occurs
before the failure but the log alone cannot prove a lifetime race or stale
GPU data. A discarded invalid hit can omit that one collision effect.
Device testing is needed to establish whether another route still crashes.

Validation:
- Original write signatures and surrounding function/loop guards checked
  against the supplied SELF, via reconstructed ELF program headers.
- `test_submesh_bounds.py` executes the actual production replacement bytes
  with Unicorn: original failure reproduced; count/index boundary and negative
  cases skip without a submesh data read; valid cases produce identical reads
  and addresses with original and patched instructions.
- Preparation contract tests update expected write counts and retain atomic
  validation/failure behavior. The patch table is refreshed at every launch.
- The APK reuses the complete, verified native payload from successful run
  42136621594 / source 0916e9f. Its physics fix for hash 697cfcc4, slot 6 is
  preserved byte-for-byte, with Readback 1. Only the known Python patch-table
  member in tools.zip is replaced; every other installer member is compared.
  No native rebuild or optimization experiment is needed for this patch.
