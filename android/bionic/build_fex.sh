#!/usr/bin/env bash
# Build the exact embedded FEX guest CPU adapter used by the TLOU ARM64 loader.
# Does not build a launcher or package an APK before the real TLOU renderer is ported.
set -Eeuo pipefail
cd "$(dirname "$0")/../.."
root=$PWD
ndk=${TLOU_NDK:-${ANDROID_NDK_HOME:-}}
: "${ndk:?Set TLOU_NDK or ANDROID_NDK_HOME}"
api=${TLOU_ANDROID_API:-30}
output=${TLOU_BIONIC_OUT:-$root/out/android-bionic}
fex=${TLOU_FEX_SOURCE:-$root/.local-deps/FEX}
fex_ref=fb925d6fc5cc8576bf0f9ae6978fc6d912485593
if [[ ! -f "$fex/CMakeLists.txt" ]]; then
  if [[ -n ${TLOU_FEX_SOURCE:-} ]]; then
    echo "TLOU_FEX_SOURCE does not contain FEX source: $fex" >&2; exit 2
  fi
  mkdir -p "$(dirname "$fex")"
  git clone https://github.com/FEX-Emu/FEX.git "$fex"
fi
if [[ ${TLOU_ALLOW_DIFFERENT_FEX:-0} != 1 ]]; then
  actual=$(git -C "$fex" rev-parse HEAD)
  if [[ $actual != "$fex_ref" ]]; then
    if [[ -n ${TLOU_FEX_SOURCE:-} ]]; then
      echo "Wrong FEX source commit $actual; expected $fex_ref" >&2; exit 2
    fi
    git -C "$fex" fetch origin "$fex_ref"
    git -C "$fex" checkout --detach "$fex_ref"
  fi
fi
git -C "$fex" submodule update --init --recursive
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH CMAKE_PREFIX_PATH
cmake -S "$fex" -B "$output/fex" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-$api" -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_FEX_INCLUDE="$root/gow3_pc/cpu/fex_project.cmake" \
  -DBUILD_TESTING=OFF -DBUILD_THUNKS=OFF -DBUILD_FEXCONFIG=OFF -DBUILD_STEAM_SUPPORT=OFF \
  -DBUILD_FEX_LINUX_TESTS=OFF -DENABLE_ZYDIS=OFF -DTUNE_ARCH=armv8.2-a -DTUNE_CPU=generic \
  -DTLOU_AOT_ABI_CHECK="${TLOU_AOT_ABI_CHECK:-OFF}" \
  -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF
cmake --build "$output/fex" --target gow3cpu --parallel "${TLOU_JOBS:-2}"
mkdir -p "$output/lib"
cp "$output/fex/libgow3cpu.so" "$output/lib/libgow3cpu.so"
cp "$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" "$output/lib/"
echo "Built TLOU guest CPU FEX bridge (without TLOU GPU or APK): $output/lib/libgow3cpu.so"
