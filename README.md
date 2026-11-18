# The Last of Us Remastered — Daedalus Android

An Android adaptation of the PS4 **The Last of Us Remastered**, based on
[ravencaio's PC port](https://github.com/ravencaio/tlou-remastered-pc).
The game runs through an ARM64 Bionic runtime, native ARM64 (AOT) CPU compilation and Vulkan rendering.

**Version 0.2.0 · Native ARM64 Android release**

[Português (Brasil)](README.pt-BR.md) · [Release notes](RELEASE_NOTES.md) ·
[Releases](https://github.com/erickmendes22/tlou-remastered-android/releases) · [Android source](https://github.com/erickmendes22/tlou-remastered-android/tree/tlou-v020-from-stable-011) ·
[YouTube — @daedalusemulator](https://www.youtube.com/@daedalusemulator)

## What is included

- TLOU-themed launcher and app icon; automatic game preparation on the phone.
- Installation from PKG or selection of an extracted game folder.
- Touch controls and physical controller support; in-game menu opened by swiping
  from the left edge toward the right.
- Fullscreen display modes that can be changed while playing.
- Compact single-line FPS/GPU/CPU overlay, with position and visibility settings.
- Original rendering resolution plus 720p, 540p and 360p patches; optional SSAO,
  motion blur, depth of field, bloom, sunlight-shadow and subsurface patches.
- 30, 60 or 120 FPS limits and VSync settings. Actual performance depends on the scene
  and device; the FPS limit is not a performance guarantee.
- Bundled Vulkan drivers and driver import from ZIP or SO files.
- Save import/export as ZIP, plus an in-game save selector with actual slot
  metadata and thumbnails when supplied by the game.
- App interface in Brazilian Portuguese, English, Spanish, French, German,
  Italian, Turkish, Polish, Russian and Hindi. The system language is selected
  automatically, with English as fallback; game language is selected inside the game.
- Log viewing and ZIP export for troubleshooting.

The tested performance settings are applied internally. Experimental benchmark
switches are not exposed in the public interface.

## Requirements and supported game

- **Android 11 or later**, with a 64-bit ARM Android environment and Vulkan support.
- Your own PS4 game dump, **updated to version 01.11**. **Version 01.00 is not
  supported for gameplay or ARM64 compilation**; the launcher asks you to update
  to 01.11. An original 01.00 folder may still be used to install/migrate a valid
  01.11 update automatically. Executable patches use verified SHA-256, not CUSA alone.
- **CUSA00552** is the primary reference; recognized regional IDs such as
  CUSA00556 must use a known, supported executable. A title ID alone does not
  establish patch compatibility.
- Enough storage for the extracted game. PKG installation also needs room for
  extraction while the original package is present.

Android testing includes Redmagic 10 Pro and 11 Pro. Results vary by device,
scene and system configuration. PC benchmarks do not predict Android FPS.

### Bundled driver selection

| GPU family | Default bundled driver |
| --- | --- |
| Adreno 8xx | Turnip Gen8 V39 Patched |
| Adreno 7xx / 6xx | Turnip 27.0.0 R2 |

Driver selection/import remains available in settings. These defaults do not
constitute validation of every GPU in those families.

## Installation

1. Install the APK from the project distribution. To update a previous compatible
   build, install over it to retain app data and saves.
2. Grant the requested file access, then choose the extracted game folder or
   install your PKG from the launcher.
3. If the game is 01.00, install its 01.11 update first. The launcher can
   automatically migrate an existing valid 01.11 update. Then finish preparation
   and compile the native ARM64 CPU before selecting **Play**.
4. Adjust resolution, patches and controls as needed. Swipe right from the left
   edge during gameplay to open the menu.

Use the app's save export before uninstalling or clearing its data.

## Status of version 0.2.0

The CPU backend now requires native ARM64 (AOT) compilation of game version 01.11.
This update improves UI polish and update handling; gameplay has been demonstrated
on Android, but a complete playthrough and universal compatibility are not claimed.

- Shader compilation can cause pauses, especially on first visits to an area.
- Selective physics-result synchronization addresses the reported character
  deformation while keeping **Readback 1**. Readback 2 is not used by this build.
- A bounds guard prevents the identified static ray-hit path from accessing an
  invalid submesh. It preserves valid hits and skips the invalid result; the
  latest guard passed automated tests but still needs confirmation on the
  previously crashing device route.
- Other scene-specific graphics, collision or stability issues may remain.

For a bug report, export logs through **About → Export logs ZIP**, describe the
scene and steps, and specify whether the problem belongs to the current or
previous execution. Include the device, GPU, driver, game version and settings.
[Report an issue](https://github.com/erickmendes22/tlou-remastered-android/issues).

## Development

The Android implementation and verified build workflow are in the
[Android source tree](https://github.com/erickmendes22/tlou-remastered-android/tree/tlou-ui-approval-log32-20261105). See the [Android build guide](android/README.md).
The repository's historical Windows code remains available; it is not the APK
build procedure. Documentation changes alone do not require generating an APK.

## Credits and license

- **Android adaptation:** Erick Mendes / Daedalus Emulator.
- **PC reference:** [ravencaio/tlou-remastered-pc](https://github.com/ravencaio/tlou-remastered-pc).
- **CPU translation:** [FEX-Emu](https://github.com/FEX-Emu/FEX).
- **Vulkan drivers:** [Mesa / Turnip](https://gitlab.freedesktop.org/mesa/mesa) and
  [libadrenotools](https://github.com/bylaws/libadrenotools).
- Runtime foundations and contributions: BilalReloaded / bc160201983,
  FelipePrado0 / SpartanPort, SuperMedo, HayatoG and the shadPS4 video core.
- Other bundled components retain their own licenses, including SDL3, FFmpeg,
  CPython, LibAtrac9 and the PKG extraction tools. See [LICENSE](LICENSE) and
  the licenses shipped with the installer components. Detailed inherited attribution
  is retained in the [historical PC documentation](docs/PC_PORT_REFERENCE.md).

Project code is licensed under **GPL-2.0-or-later**. No game files are included
in this repository or APK. This is an unofficial project, unaffiliated with
Sony Interactive Entertainment or Naughty Dog.
