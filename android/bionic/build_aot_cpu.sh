#!/usr/bin/env bash
# Build AOT guest CPU separately from the production FEX library.
# GPL-2.0-or-later code from Bloodborne-Android (pinned upstream revision).
set -Eeuo pipefail
cd "$(dirname "$0")/../.."
root=$PWD
ndk=${TLOU_NDK:-${ANDROID_NDK_HOME:-$root/.local-deps/android/sdk/ndk/android-ndk-r29}}
api=${TLOU_ANDROID_API:-30}
llvm="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
cc="$llvm/aarch64-linux-android${api}-clang++"
readelf="$llvm/llvm-readelf"
[[ -x $cc && -x $readelf ]] || { echo "AOT backend requires Android NDK: $ndk" >&2; exit 2; }
pin=b6d4cfb6972c84d4029f54765406ec143df3c30e
repo="$root/.local-deps/Bloodborne-Android-AOT"
if [[ ! -d "$repo/.git" ]]; then
  mkdir -p "$repo"
  git -C "$repo" init -q
  git -C "$repo" remote add origin https://github.com/ducvd89/Bloodborne-Android.git
  git -C "$repo" sparse-checkout init --cone
  git -C "$repo" sparse-checkout set cpu src/guest_cpu.h
fi
if [[ $(git -C "$repo" rev-parse HEAD 2>/dev/null || echo missing) != "$pin" ]]; then
  git -C "$repo" fetch --depth=1 origin "$pin"
  git -C "$repo" checkout --detach -f FETCH_HEAD
fi
test "$(git -C "$repo" rev-parse HEAD)" = "$pin"
out="$root/out/android-bionic/aot-backend"
python3 android/aot/port_runtime.py --upstream "$repo" --out "$out/src"
"$cc" -std=c++20 -O2 -g -Wall -Wextra -Werror -fPIC -shared \
  -fvisibility=hidden -fvisibility-inlines-hidden -fno-strict-aliasing \
  -I "$out/src" \
  "$out/src/guest_cpu_aot.cpp" "$out/src/guest_cpu_native.cpp" \
  "$out/src/host_call_arm64.S" \
  -Wl,-soname,libgow3cpu.so -Wl,--no-undefined -ldl -lm \
  -o "$out/libgow3cpu.so"
"$readelf" -h "$out/libgow3cpu.so" | grep -Eq 'Machine:.*AArch64'
"$readelf" -d "$out/libgow3cpu.so" | grep -F '[libc.so]'
"$readelf" --dyn-syms --wide "$out/libgow3cpu.so" > "$out/symbols.txt"
for symbol in guest_cpu_init guest_cpu_code guest_cpu_call \
    guest_cpu_fiber_create guest_cpu_fiber_save guest_cpu_fiber_entry \
    guest_cpu_fiber_switch guest_cpu_fiber_dispose guest_cpu_fiber_frame_pointer \
    guest_cpu_host_function guest_cpu_hook guest_cpu_tsc \
    bbr_dispatch bbr_dispatch_tail bbr_dirty_entry bbr_unwound; do
  grep -Eq "[[:space:]]${symbol}\$" "$out/symbols.txt" || {
    echo "AOT runtime missing required exported symbol $symbol" >&2
    exit 1
  }
done
mkdir -p "$root/out/android-bionic/apk-assets/runtime-aot"
cp "$out/libgow3cpu.so" "$root/out/android-bionic/apk-assets/runtime-aot/libgow3cpu.so"
sha256sum "$out/libgow3cpu.so" > "$out/libgow3cpu.so.sha256"
echo "TLOU native AOT engine built, with PS4 fibers. FEX production binary unchanged."
