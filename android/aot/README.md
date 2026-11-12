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

## What remains

1. Verify the real G3PROBE5 image payload is accepted by the bb-recomp scanner and the user's specific game image generates a complete libtlouaot.so, its metadata, and a function coverage report.
2. Build an **independent** TLOU guest_cpu_* AOT backend, preserving the FEX guest ABI, fibers and guest register state, indirect calls, imports, returns, invalidated code and current patches. Use a hybrid AOT/FEX fallback until coverage is proven.
3. Validate game startup, saves, cutscenes, physics, visuals, stability, and same-scene CPU/FPS. This builder by itself does **not** prove performance improvements.

The original FEX backend and production APK are unchanged.
