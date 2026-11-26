#!/usr/bin/env bash
# Package the full native TLOU v0.2.0 for the EXISTING AOTNative v2 package/signer.
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
[[ -s "$out/bin/gow3-probe" ]] || { echo "Missing TLOU ARM64 native runtime" >&2; exit 2; }
cmp -s "$out/aot-backend/libgow3cpu.so" "$out/lib/libgow3cpu.so" || {
  echo "Refusing AOTNative APK: linked CPU is not the native AOT engine" >&2; exit 8;
}
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
stage="$out/apk-jni/arm64-v8a"
assets="$out/apk-assets/runtime"
# The CPU and GPU are rebuilt on the v0.2.0 release source branch.
# No FEX backend is accepted in the packaged CPU library.
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
# Imported proprietary Qualcomm drivers require Android's vendor linker namespace.
# Build the audited upstream loader and hooks from immutable source; no prebuilt
# binary from another emulator and no replacement for the device's mapper ABI.
adreno_src="$out/adrenotools-src"
adreno_build="$out/adrenotools-build"
adreno_rev=8fae8ce254dfc1344527e05301e43f37dea2df80
if [[ ! -d "$adreno_src/.git" ]]; then
  git clone https://github.com/bylaws/libadrenotools.git "$adreno_src"
fi
git -C "$adreno_src" checkout --force "$adreno_rev"
git -C "$adreno_src" submodule update --init --recursive
# Custom driver failure must not masquerade as success on the system ICD.
python3 - "$adreno_src/src/hook/hook_impl.cpp" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1])
s=p.read_text()
old='LOGI("hook_android_dlopen_ext: falling back!");'
new='''LOGI("hook_android_dlopen_ext: falling back!");
        if (hook_params->featureFlags & ADRENOTOOLS_DRIVER_CUSTOM) {
            std::fprintf(stderr, "TLOU imported Qualcomm: vendor hook failed; refusing system fallback\\n");
            return static_cast<void*>(nullptr);
        }'''
assert s.count(old)==1
s=s.replace(old,new)
old='return handle;\n    } else {'
new='std::fprintf(stderr, "TLOU imported Qualcomm: custom ICD loaded: %s%s\\n", hook_params->customDriverDir.c_str(), hook_params->customDriverName.c_str());\n        return handle;\n    } else {'
assert s.count(old)==1
p.write_text(s.replace(old,new))
PY
cmake -S "$adreno_src" -B "$adreno_build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-30 \
  -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build "$adreno_build" --parallel 2
for lib in libadrenotools.so libhook_impl.so libmain_hook.so libfile_redirect_hook.so libgsl_alloc_hook.so; do
  built_lib=$(find "$adreno_build" -name "$lib" -type f -print -quit)
  [[ -s "$built_lib" ]] || { echo "Missing Qualcomm namespace library $lib" >&2; exit 8; }
  require_command_regex 'Machine:.*AArch64' "$llvm/llvm-readelf" -h "$built_lib"
  cp "$built_lib" "$stage/$lib"
done
mkdir -p "$out/apk-assets/licenses"
cp "$adreno_src/LICENSE" "$out/apk-assets/licenses/libadrenotools.txt"

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
# The AOTNative v2 identity must already be restored from its canonical cache.
# Gradle's debug variant uses its original package suffix and signing keystore.
test -s "$HOME/.android/debug.keystore" || {
  echo "ERROR: original AOTNative v2 signing keystore missing" >&2; exit 8;
}
cert=$(keytool -exportcert -alias androiddebugkey -keystore "$HOME/.android/debug.keystore" -storepass android 2>/dev/null | sha256sum | cut -d ' ' -f1)
expected=adbdd4efd7cb981ce6a30303de83b635800b0960c68276d9247962918cc7328c
[[ "$cert" == "$expected" ]] || {
  echo "ERROR: AOTNative v2 signer certificate mismatch; refusing a new signing identity" >&2; exit 8;
}
gradle -p "$root/android-launcher" :app:assembleDebug --no-daemon --max-workers=2
built="$root/android-launcher/app/build/outputs/apk/debug/app-debug.apk"
[[ -s "$built" ]] || { echo "Missing actual AOTNative debug APK" >&2; exit 8; }
apk="$root/out/release/TheLastOfUsRemastered-Android-v0.2.0-AOTNative-v2-arm64.apk"
cp "$built" "$apk"
"$android_tools/apksigner" verify --verbose "$apk"
require_command_regex "$expected" "$android_tools/apksigner" verify --print-certs "$apk"
require_command_regex "package: name='com[.]daedalus[.]tlou[.]remastered[.]aotnative2' versionCode='200'" "$android_tools/aapt" dump badging "$apk"
require_command_regex 'lib/arm64-v8a/libgow3gpu[.]so' unzip -Z -1 "$apk"
require_command_regex 'lib/arm64-v8a/libgow3cpu[.]so' unzip -Z -1 "$apk"
require_command_regex 'lib/arm64-v8a/libgow3present[.]so' unzip -Z -1 "$apk"
for lib in libadrenotools libhook_impl libmain_hook libfile_redirect_hook libgsl_alloc_hook; do
  require_command_regex "lib/arm64-v8a/${lib}[.]so" unzip -Z -1 "$apk"
done
require_command_regex 'assets/runtime/gow3-probe' unzip -Z -1 "$apk"
require_command_regex 'assets/tools[.]zip' unzip -Z -1 "$apk"
require_command_regex 'assets/runtime-aot/libgow3cpu[.]so' unzip -Z -1 "$apk"
require_command_regex 'assets/runtime-aot/libbbcpu[.]so' unzip -Z -1 "$apk"
require_command_regex 'lib/arm64-v8a/libpython3[.]13[.]so' unzip -Z -1 "$apk"
python3 android/bionic/check_tools_apk.py "$apk"
# Verify that the actual CPU installed with the APK is the AOT engine, not FEX.
python3 - "$apk" "$out/aot-backend/libgow3cpu.so" <<'PY'
from pathlib import Path
from zipfile import ZipFile
import hashlib,sys
apk=Path(sys.argv[1])
expected=Path(sys.argv[2]).read_bytes()
with ZipFile(apk) as z:
    actual=z.read('lib/arm64-v8a/libgow3cpu.so')
    stage=z.read('assets/runtime-aot/libgow3cpu.so')
assert hashlib.sha256(actual).digest()==hashlib.sha256(expected).digest(), 'Packaged CPU differs from AOT backend'
assert hashlib.sha256(stage).digest()==hashlib.sha256(expected).digest(), 'Runtime AOT CPU differs from linked engine'
print('Full native AOT CPU verified byte-identical in APK and staged engine')
PY
sha256sum "$apk" > "$apk.sha256"
echo "TLOU 0.2.0 AOTNative v2 signed and verified: $apk"
