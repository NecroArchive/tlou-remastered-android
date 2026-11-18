#!/usr/bin/env bash
# Package the ARM64 AOT-only TLOU loader and Vulkan GPU for Android.
set -Eeuo pipefail
trap 'echo "TLOU APK packaging failed at line $LINENO: $BASH_COMMAND" >&2' ERR
cd "$(dirname "$0")/../.."
root=$PWD
out=$root/out/android-bionic
ndk="$TLOU_NDK"
if [[ -z "$ndk" ]]; then ndk="$ANDROID_NDK_HOME"; fi
[[ -n "$ndk" ]] || { echo "Set TLOU_NDK or ANDROID_NDK_HOME" >&2; exit 2; }
llvm=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin
sdk="$ANDROID_HOME"
if [[ -z "$sdk" ]]; then sdk="$ANDROID_SDK_ROOT"; fi
[[ -n "$sdk" ]] || { echo "Set ANDROID_HOME or ANDROID_SDK_ROOT" >&2; exit 2; }
build_tools=35.0.0
android_tools="$sdk/build-tools/$build_tools"
[[ -s "$out/bin/gow3-probe" ]] || { echo "Missing TLOU ARM64 AOT loader" >&2; exit 2; }
for library in libgow3cpu.so libgow3gpu.so libgow3present.so libc++_shared.so; do
  [[ -s "$out/lib/$library" ]] || { echo "Missing native $library" >&2; exit 2; }
done
# Never pipe a verbose producer to grep -q under pipefail: a successful early
# match can close stdout and terminate readelf/unzip/aapt with SIGPIPE (141).
# Capture the producer's complete output first, preserving its real exit code.
require_command_regex() {
  local pattern="$1"
  shift
  local output
  output="$("$@")" || return $?
  if [[ ! "$output" =~ $pattern ]]; then
    printf 'TLOU packaging check failed: pattern %s not found in output of %s\n' "$pattern" "$1" >&2
    return 1
  fi
}
require_command_regex 'Machine:.*AArch64' "$llvm/llvm-readelf" -h "$out/bin/gow3-probe"
require_command_regex '/system/bin/linker64' "$llvm/llvm-readelf" -l "$out/bin/gow3-probe"
for library in libgow3cpu.so libgow3gpu.so libgow3present.so libc++_shared.so; do
  require_command_regex 'Machine:.*AArch64' "$llvm/llvm-readelf" -h "$out/lib/$library"
done
# Release 0.2.0 must have exactly the same native CPU in the package and
# in the AOT guest runtime; reject old FEX-built libgow3cpu.so from any cache.
test -s "$out/lib/libgow3cpu.so"
test -s "$out/apk-assets/runtime-aot/libgow3cpu.so"
cmp "$out/lib/libgow3cpu.so" "$out/apk-assets/runtime-aot/libgow3cpu.so" || {
  echo "Refusing mismatched CPU implementations (FEX vs ARM64 AOT)" >&2
  exit 8
}
require_command_regex 'guest_cpu_fiber_switch' "$llvm/llvm-readelf" --dyn-syms --wide "$out/lib/libgow3cpu.so"
require_command_regex 'bbr_dispatch' "$llvm/llvm-readelf" --dyn-syms --wide "$out/lib/libgow3cpu.so"
stage="$out/apk-jni/arm64-v8a"
assets="$out/apk-assets/runtime"
# The original v0.1.1 full-native workflow calls this packager after it has
# built Bionic, ARM64 AOT, Vulkan, Python and the PKG tools. Keep that job
# hierarchy untouched. Stage the mandatory v0.2.0 ARM64 engine here.
if [[ ! -s "$out/apk-assets/runtime-aot/libgow3cpu.so" ||
      ! -s "$out/apk-assets/runtime-aot/libbbcpu.so" ]]; then
  bash android/bionic/build_aot_cpu.sh
fi
# Build the pinned, TLOU-patched TSO compiler from SOURCE in this same release
# job when no verified archive is supplied. Never silently ship upstream
# bb-recomp 1.3.0: it lacks the required TSO lowering.
compiler_archive=${TLOU_AOT_COMPILER_ARCHIVE:-$out/aot-tso-compiler/bb-recomp-android-aarch64.tar.xz}
if [[ ! -s "$compiler_archive" ]]; then
  echo "Building TLOU ARM64 TSO compiler from pinned sources within original native release job"
  ANDROID_NDK_HOME="$ndk" TLOU_AOT_BUILD_JOBS="${TLOU_JOBS:-3}" \
    bash android/aot/build_recompiler_tso.sh
fi
[[ -s "$compiler_archive" ]] || {
  echo "Mandatory patched compiler archive unavailable: $compiler_archive" >&2
  exit 8
}
if [[ -s "$compiler_archive.sha256" ]]; then
  (cd "$(dirname "$compiler_archive")" &&
    sha256sum -c "$(basename "$compiler_archive").sha256")
fi
TLOU_TSO_REQUIRED=1 TLOU_AOT_COMPILER_ARCHIVE="$compiler_archive" \
  bash android/bionic/stage_aot_compiler.sh
for required in "$out/apk-assets/runtime-aot/libgow3cpu.so" \
                "$out/apk-assets/runtime-aot/libbbcpu.so" \
                "$out/aot-compiler/bin/libbbrecomp.so" \
                "$out/aot-compiler/upstream.sha256"; do
  [[ -s "$required" ]] || { echo "Missing mandatory v0.2.0 AOT payload: $required" >&2; exit 8; }
done
# Sanity check AArch64 runtime: the compiler and CPU backend must match the
# pinned AOT ABI, not any accidental FEX-only package.
require_command_regex 'Machine:.*AArch64' "$llvm/llvm-readelf" -h "$out/apk-assets/runtime-aot/libgow3cpu.so"
require_command_regex 'Machine:.*AArch64' "$llvm/llvm-readelf" -h "$out/aot-compiler/bin/libbbrecomp.so"
python3 android/bionic/stage_tools.py
mkdir -p "$stage" "$assets" "$root/out/release"
# Pinned official Turnip 27 R2 / Gen8 V39 patched. Never use a rolling Mesa
# "latest" archive; the exact upstream asset digest is pinned for audit.
# This runs ONLY when packaging is explicitly requested, never during commits.
turnip_dir="$out/apk-assets/drivers"
turnip_zip="$turnip_dir/turnip-v27-r2-a6xx-a7xx.zip"
turnip_sha="8d30be022582d0c03b1918c019540491d00439e14e9e5e1359fe0bb19004f781"
turnip_gen8_zip="$turnip_dir/turnip-gen8-v39-patched-a8xx.zip"
turnip_gen8_sha="bd457b30f195cf93a334b5302c221b1b5cb59c79c485935decf95d560a38c8a2"
mkdir -p "$turnip_dir"
if [[ ! -s "$turnip_zip" ]] || ! echo "$turnip_sha  $turnip_zip" | sha256sum -c --status; then
  tmp="$turnip_zip.download"
  rm -f "$tmp"
  curl -fL --retry 3 --connect-timeout 20 --max-time 120 \
    "https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases/download/v39/Turnip_v27.0.0-R2.zip" \
    -o "$tmp"
  echo "$turnip_sha  $tmp" | sha256sum -c
  require_command_regex 'meta.json' unzip -Z -1 "$tmp"
  require_command_regex 'libvulkan_freedreno.so' unzip -Z -1 "$tmp"
  mv "$tmp" "$turnip_zip"
fi
echo "$turnip_sha  $turnip_zip" | sha256sum -c
# The Gen8 patched package is distinct from R1 and is pinned to the upstream digest.
if [[ ! -s "$turnip_gen8_zip" ]] || ! echo "$turnip_gen8_sha  $turnip_gen8_zip" | sha256sum -c --status; then
  temp="$turnip_gen8_zip.download"
  rm -f "$temp"
  curl -fL --retry 3 --connect-timeout 20 --max-time 120 \
    "https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases/download/v39/Turnip_Gen8_V39_Patched.zip" -o "$temp"
  echo "$turnip_gen8_sha  $temp" | sha256sum -c
  require_command_regex 'meta.json' unzip -Z -1 "$temp"
  require_command_regex 'libvulkan_freedreno.so' unzip -Z -1 "$temp"
  mv "$temp" "$turnip_gen8_zip"
fi
echo "$turnip_gen8_sha  $turnip_gen8_zip" | sha256sum -c
cp "$out/bin/gow3-probe" "$assets/gow3-probe"
for lib in libgow3cpu.so libgow3gpu.so libgow3present.so libc++_shared.so; do
  cp "$out/lib/$lib" "$stage/$lib"
done
gradle -p "$root/android-launcher" :app:assembleRelease --no-daemon --max-workers=2
unsigned="$root/android-launcher/app/build/outputs/apk/release/app-release-unsigned.apk"
[[ -s "$unsigned" ]] || { echo "Gradle did not produce an unsigned APK" >&2; exit 2; }
aligned="$out/tlou-release-aligned.apk"
apk="$root/out/release/TheLastOfUsRemastered-Android-v0.2.0-arm64.apk"
"$android_tools/zipalign" -f -p 4 "$unsigned" "$aligned"
key="$out/experimental-development.jks"
# A different key would make updates impossible. Never regenerate a release key.
if [[ ! -s "$key" ]]; then
  echo "ERROR: existing TLOU signing identity missing; refusing a differently signed APK" >&2
  exit 8
fi
"$android_tools/apksigner" sign --ks "$key" --ks-key-alias tlou-bionic \
  --ks-pass pass:TLOU-Bionic-development --key-pass pass:TLOU-Bionic-development \
  --out "$apk" "$aligned"
"$android_tools/apksigner" verify --verbose "$apk"
require_command_regex "package: name='com[.]daedalus[.]tlou[.]remastered'" "$android_tools/aapt" dump badging "$apk"
require_command_regex 'lib/arm64-v8a/libgow3gpu[.]so' unzip -Z -1 "$apk"
require_command_regex 'lib/arm64-v8a/libgow3cpu[.]so' unzip -Z -1 "$apk"
require_command_regex 'lib/arm64-v8a/libgow3present[.]so' unzip -Z -1 "$apk"
require_command_regex 'assets/runtime/gow3-probe' unzip -Z -1 "$apk"
require_command_regex 'assets/tools[.]zip' unzip -Z -1 "$apk"
require_command_regex 'assets/runtime-aot/libgow3cpu[.]so' unzip -Z -1 "$apk"
require_command_regex 'assets/runtime-aot/libbbcpu[.]so' unzip -Z -1 "$apk"
require_command_regex 'lib/arm64-v8a/libpython3[.]13[.]so' unzip -Z -1 "$apk"
sha256sum "$apk" > "$apk.sha256"
echo "TLOU v0.2.0 APK signed and validated: $apk"
