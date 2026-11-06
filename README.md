# The Last of Us Remastered — Daedalus Android

An Android adaptation of the PS4 **The Last of Us Remastered**, based on
[ravencaio's PC port](https://github.com/ravencaio/tlou-remastered-pc).
The game runs through an ARM64 Bionic runtime, FEX CPU translation and Vulkan rendering.

**Version 0.1 · First public Android build**

[Português (Brasil)](README.pt-BR.md) · [Release notes](RELEASE_NOTES.md) ·
[Releases](https://github.com/erickmendes22/tlou-remastered-android/releases) · [Android source](https://github.com/erickmendes22/tlou-remastered-android/tree/tlou-ui-approval-log32-20261105) ·
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
- 30 or 60 FPS limit and VSync settings. Actual performance depends on the scene
  and device; the 60 FPS limit is not a performance guarantee.
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
- Your own PS4 game dump. The verified Android executable profile is **version
  01.00**, SHA-256
  `e5f0aa322f60bd90637cc10d83de08c77885b1921084021c630e04ed3d6febf7`.
- The reference title is **CUSA00552**. Other regional title IDs are accepted
  when they contain the same verified executable; a title ID alone does not
  establish patch compatibility.
- Enough storage for the extracted game. PKG installation also needs room for
  extraction while the original package is present.

Android gameplay testing has focused on **Redmagic 10 Pro / Snapdragon 8 Elite /
Adreno 830**. Compatibility and performance across other devices are still being
expanded. The Windows port's 01.11 requirements and PC FPS measurements do not
represent this Android build.

### Bundled driver selection

| GPU family | Default bundled driver |
| --- | --- |
| Adreno 8xx | Turnip Gen8 V38 Patched |
| Adreno 7xx / 6xx | Turnip 27.0.0 R1 |

Driver selection/import remains available in settings. These defaults do not
constitute validation of every GPU in those families.

## Installation

1. Install the APK from the project distribution. To update a previous compatible
   build, install over it to retain app data and saves.
2. Grant the requested file access, then choose the extracted game folder or
   install your PKG from the launcher.
3. Let automatic preparation finish and select **Play**.
4. Adjust resolution, patches and controls as needed. Swipe right from the left
   edge during gameplay to open the menu.

Use the app's save export before uninstalling or clearing its data.

## Status of version 0.1

This is an initial public build, with gameplay demonstrated on Android, rather
than a claim of complete compatibility or a full-game playthrough.

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
