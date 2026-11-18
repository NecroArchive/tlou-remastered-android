#!/usr/bin/env bash
# Bionic TLOU-specific native runtime: no game files, no glibc, no SDL.
set -Eeuo pipefail
trap 'printf "TLOU Bionic native build failed at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR
cd "$(dirname "$0")/../.."
root=$PWD
ndk=${TLOU_NDK:-${ANDROID_NDK_HOME:-}}
: "${ndk:?Set TLOU_NDK or ANDROID_NDK_HOME}"
export TLOU_NDK="$ndk"
api=${TLOU_ANDROID_API:-30}
out=$root/out/android-bionic
prefix=$root/.local-deps/android/bionic-prefix
llvm=$ndk/toolchains/llvm/prebuilt/linux-x86_64
if [[ ${1:-} != --link-only ]]; then
  bash android/bionic/build_deps.sh
  bash android/bionic/build_aot_cpu.sh
  bash android/bionic/build_gpu.sh
fi
# Both fresh and --link-only builds must use the AOT CPU that was produced
# from the current source. The loader must never link against an old FEX .so.
cmp "$out/lib/libgow3cpu.so" "$out/apk-assets/runtime-aot/libgow3cpu.so" || {
  echo "Refusing mixed FEX/AOT CPU runtime; rebuild native ARM64 AOT" >&2
  exit 3
}
for path in "$out/lib/libgow3gpu.so" "$out/lib/libgow3cpu.so" "$prefix/lib/libavcodec.a"; do
  [[ -f $path ]] || { echo "Missing Bionic build dependency: $path" >&2; exit 2; }
done
mkdir -p "$out/bin" "$out/lib" "$out/objects" "$out/atrac9"
cc="$llvm/bin/aarch64-linux-android${api}-clang"
for source in gow3_pc/third_party/LibAtrac9/C/src/*.c; do
  "$cc" -std=c99 -O2 -fPIC -w -c "$source" -o "$out/atrac9/$(basename "${source%.c}").o"
done
"$llvm/bin/llvm-ar" rcs "$out/libatrac9.a" "$out"/atrac9/*.o
objects=()
for source in gow3_pc/src/probe.c gow3_pc/src/runtime*.c gow3_pc/src/vulkan_smoke.c; do
  object="$out/objects/$(basename "${source%.c}").o"
  "$cc" -std=c11 -O2 -g -fPIE -Wall -Wextra -Werror -DGOW3_NO_SDL=1 -pthread \
    -I "$prefix/include" -I gow3_pc -I gow3_pc/src -c "$source" -o "$object"
  objects+=("$object")
done
"$cc" "${objects[@]}" "$out/libatrac9.a" \
  "$prefix/lib/libavformat.a" "$prefix/lib/libavcodec.a" "$prefix/lib/libswscale.a" \
  "$prefix/lib/libswresample.a" "$prefix/lib/libavutil.a" \
  -L "$out/lib" -lgow3gpu -lgow3cpu -lc++_shared -lvulkan -lm -ldl -llog -pthread \
  -Wl,--exclude-libs,libavcodec.a:libavformat.a:libavutil.a:libswscale.a:libswresample.a \
  -rdynamic -pie -Wl,-rpath,'$ORIGIN/../lib' -o "$out/bin/gow3-probe"
"$cc" -std=c11 -O2 -g -Wall -Wextra -Werror -fPIC -shared \
  -I gow3_pc/tools/android/present \
  gow3_pc/tools/android/bionic/present/gow3present.c \
  -lnativewindow -landroid -llog -ldl -o "$out/lib/libgow3present.so"
echo "TLOU native ARM64 AOT loader and Android presenter linked (device gameplay not yet validated)"
echo "  $out/bin/gow3-probe"
echo "  $out/lib/libgow3gpu.so"
echo "  $out/lib/libgow3cpu.so"
echo "  $out/lib/libgow3present.so"
