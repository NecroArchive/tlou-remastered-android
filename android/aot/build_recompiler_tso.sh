#!/usr/bin/env bash
# Rebuild pinned bb-recomp Android compiler with conservative x86 TSO lowering.
# Produces only developer tools, never any game data.
set -Eeuo pipefail
trap 'echo "TLOU AOT compiler build failed at $LINENO: $BASH_COMMAND" >&2' ERR
cd "$(dirname "$0")/../.."
root=$PWD
pin=6ebb33b0c729a3942fddaa03965360c699d7d900
tool="$root/.local-deps/bb-recomp-tlou"
ndk="${ANDROID_NDK_HOME:-${ANDROID_HOME:-/usr/local/lib/android/sdk}/ndk/29.0.14206865}"
export BB_NDK="$ndk"
export BB_JOBS="${TLOU_AOT_BUILD_JOBS:-4}"
export BB_RECOMP_VERSION=1.3.0-tlou-tso1
export BB_X86_DEPS="$tool/.local-deps"
export BB_FEX_TREE="$tool/out/recomp-fex"
test -x "$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
mkdir -p "$tool"
if [[ ! -d "$tool/.git" ]]; then
  git -C "$tool" init -q
  git -C "$tool" remote add origin https://github.com/ducvd89/Bloodborne-android-rebuild-tool.git
fi
git -C "$tool" fetch --depth=1 origin "$pin"
git -C "$tool" checkout --detach -f FETCH_HEAD
test "$(git -C "$tool" rev-parse HEAD)" = "$pin"
python3 "$root/android/aot/patch_recompiler_tso.py" "$tool"

ver=23.1.3
deps="$tool/.local-deps"
x86llvm="$deps/llvm/x/LLVM-$ver-Linux-X64"
llvm_src="$deps/llvm/src/llvm-project-$ver.src"
download() {
  local url=$1 path=$2
  if [[ ! -s $path ]]; then
    mkdir -p "$(dirname "$path")"
    curl --fail --location --retry 4 --retry-delay 4 \
      --connect-timeout 30 "$url" -o "$path.partial"
    mv "$path.partial" "$path"
  fi
}
if [[ ! -x "$x86llvm/bin/clang" ]]; then
  x="$deps/llvm/dl/LLVM-$ver-Linux-X64.tar.zst"
  download "https://github.com/llvm/llvm-project/releases/download/llvmorg-$ver/LLVM-$ver-Linux-X64.tar.zst" "$x"
  mkdir -p "$deps/llvm/x"
  (cd "$deps/llvm/x" && zstd -d --long=31 -c "$x" | tar -x)
fi
if [[ ! -d "$llvm_src/llvm" ]]; then
  x="$deps/llvm/dl/llvm-project-$ver.src.tar.xz"
  download "https://github.com/llvm/llvm-project/releases/download/llvmorg-$ver/llvm-project-$ver.src.tar.xz" "$x"
  mkdir -p "$deps/llvm/src"
  tar -xf "$x" -C "$deps/llvm/src"
fi
download "https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-$ver/llvm/LICENSE.TXT" "$deps/llvm/LICENSE.TXT"
download "https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.xz" "$deps/deps/dl/zlib-1.3.1.tar.xz"
download "https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz" "$deps/deps/dl/zstd-1.5.7.tar.gz"
if [[ ! -f "$deps/mimalloc/mimalloc-3.6.0/src/static.c" ]]; then
  mkdir -p "$deps/mimalloc"
  download "https://github.com/microsoft/mimalloc/archive/refs/tags/v3.6.0.tar.gz" "$deps/mimalloc/mimalloc-v3.6.0.tar.gz"
  tar -xzf "$deps/mimalloc/mimalloc-v3.6.0.tar.gz" -C "$deps/mimalloc"
fi
(
  cd "$tool"
  bash fetch-fex.sh
  bash arm64/build.sh android
  bash arm64/package-android.sh
)
compiler="$tool/out/android/libbbrecomp.so"
tarball="$tool/out/dist/bb-recomp-android-aarch64.tar.xz"
test -s "$compiler" && test -s "$tarball"
readelf -h "$compiler" | grep -Eq 'Machine:.*AArch64'
readelf -l "$compiler" | grep -F '/system/bin/linker64'
grep -q 'BBR_COMPILER_VERSION "2-tlou-tso"' "$tool/src/version.h"
grep -q 'case OP_LOADMEMTSO:' "$tool/src/lower_ops.cpp"
grep -q 'case OP_STOREMEMTSO:' "$tool/src/lower_ops.cpp"
mkdir -p "$root/out/android-bionic/aot-tso-compiler"
cp "$tarball" "$root/out/android-bionic/aot-tso-compiler/"
(cd "$root/out/android-bionic/aot-tso-compiler" && sha256sum bb-recomp-android-aarch64.tar.xz > bb-recomp-android-aarch64.tar.xz.sha256)
printf '%s\n' "Upstream bb-recomp: $pin" \
  "Compiler shard version: 2-tlou-tso" \
  "TSO lowering: seq-cst fences around aligned/unaligned x86 accesses" \
  "Support: GPR up to 64-bit / FPR up to 128-bit; unsupported widths stay dirty" \
  "No game files included" > "$root/out/android-bionic/aot-tso-compiler/README-TLOU-TSO.txt"
echo "Finished patched AArch64 compiler archive: $tarball"
