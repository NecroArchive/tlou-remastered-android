# TLOU Remastered — experimental local ARM64 recompilation

This branch does **not** replace FEX or produce a playable AOT APK yet.

TLOU already prepares its PS4 x86-64 executable and two modules into a G3PROBE5 image. The [Bloodborne Rebuild Tool](https://github.com/ducvd89/Bloodborne-android-rebuild-tool) v1.3.0 uses the same boot-image table structure, but expects the signature BBPROBE5. The adapter makes a private copy, changes **only** those eight bytes and invokes the tool's FEX IR → LLVM → AArch64 compiler.

The player's eboot, libc.prx and libSceFios2.prx never leave the device and **must not** be committed or attached to CI.

## Running inside an Android environment that can access the game and compiler

The game folder needs eboot.bin, sce_module/libc.prx, sce_module/libSceFios2.prx and sce_sys/param.sfo. The TLOU app can provide its existing boot-linked.bin. Use an app-private output directory, not shared storage.

```sh
python3 android/aot/aot_local.py \
  --game /private/tlou-game \
  --boot /private/tlou-data/boot-linked.bin \
  --out /private/tlou-aot-work \
  --compiler /private/tools/libbbrecomp.so \
  --bundle-dir /private/tools/bundle \
  --jobs 3 --shards 64
```

Without --boot the script invokes the repo's gow3_pc/scripts/prepare.py and link_modules.py on the local game folder. --dry-run validates and stages the image without compiling. The compiler supports --resume to reuse validated completed shards.

The app does not yet ship or start libbbrecomp.so; it needs to be provisioned locally or bundled and invoked from the Android launcher in a later change. Do not reuse Bloodborne game-specific patch catalogs.

## TLOU 1.11 on-device test: actual first native startup failure

Logs collected 2026-11-12 show the Android linker successfully chose the
native AOT backend ("Guest CPU: ahead-of-time units only", not FEX), the
Vulkan renderer initialized, and the first linked `libc.prx` initializer at
guest offset `+0x1c30000` failed. This is **not** a missing FDE or a game
patch: the upstream compiler with `--tso` marks 60,598 of 75,413 units
(80.35%) dirty because LLVM lowering does not implement `LoadMemTSO` and
`StoreMemTSO`. The miss logger calls these "patched" (there is no
compiled native variant), which is misleading in this case.

The fix is in `android/aot/patch_recompiler_tso.py`: the pinned LLVM compiler
must handle scalar and up-to-128-bit SIMD TSO memory ops with conservative
system-scope sequentially consistent fences. The patch also bumps the
compiler's shard identity to `2-tlou-tso` so no old object files are reused.
The synthetic `test_aot_quality.py` rejects old compiler versions and trap
counts above 0.25% or any remaining TSO trap. The local builder refuses to
call an 80%-dirty native library "ready". This does not yet prove gameplay,
FPS, or x87 numerical equivalence. A host-native rebuild alone is not an
on-device test.

## Integrated experimental app path

The experimental APK build can include the official, SHA-256-pinned Android
`bb-recomp` 1.3.0 binary with `TLOU_EXPERIMENTAL_LOCAL_AOT=1` during
`android/bionic/package_apk.sh`. `android/bionic/stage_tools.py` places the
compiler, LLVM bundle and this frontend in `assets/tools.zip`.
The **Settings → CPU and performance → Compile ARM64 on this device** action
prepares the selected base + update, uses its merged `tlou-view` and emits
`files/tlou-aot-work/aot/libtlouaot.so`. The original game is not changed.
The app currently **does not execute this library**.

## Verified compatibility facts

- The executable text is compared byte-for-byte against the prepared image
  (accounting for the linker's documented FS→GS rewrite). A mixed 1.00/1.11
  source is refused before translation.
- The pinned FEX CPUState layout matches the AOT v2 GuestState offsets: see
  workflow `tlou-aot-abi-check.yml`.
- The pinned TLOU FEX defaults to **TSO enabled**, unlike Bloodborne's AOT
  compiler default. The builder uses conservative `--tso` and a portable
  `--host armv8.2`. The bundled compiler's TSO option enables additional
  vector/memcpy barriers compared with the exact TLOU default; this can
  impact performance and is not yet a 1:1 translation profile.
- TLOU FEX defaults to full-precision x87, but bb-recomp v1.3.0 always
  lowers reduced-precision x87. Until x87 functions are handled with a
  semantically correct backend or FEX fallback, the generated code must
  **not** replace the production guest CPU.
- Runtime fibers, native callback state restoration, dirty unit invalidation,
  special hooks and patch variants are still to be integrated.

CI source checks validate the compiler bundle and the AOT/GuestState
preconditions; they do **not** validate full TLOU execution or an FPS gain.

## What remains

1. Verify the real G3PROBE5 image payload is accepted by the bb-recomp scanner and the user's specific game image generates a complete libtlouaot.so, its metadata, and a function coverage report.
2. Build an **independent** TLOU guest_cpu_* AOT backend, preserving the FEX guest ABI, fibers and guest register state, indirect calls, imports, returns, invalidated code and current patches. Use a hybrid AOT/FEX fallback until coverage is proven.
3. Validate game startup, saves, cutscenes, physics, visuals, stability, and same-scene CPU/FPS. This builder by itself does **not** prove performance improvements.

The original FEX backend and production APK are unchanged.
