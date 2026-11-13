#!/usr/bin/env bash
# Pin and stage the upstream Android ARM64 *compiler*, not any game binaries.
# The compiler runs on the player's phone against their own PS4 dump.
set -Eeuo pipefail
cd "$(dirname "$0")/../.."
out="$PWD/out/android-bionic/aot-compiler"
cache="$PWD/.local-deps/android/bb-recomp-v1.3.0-aarch64.tar.xz"
url="https://github.com/ducvd89/Bloodborne-android-rebuild-tool/releases/download/v1.3.0/bb-recomp-android-aarch64.tar.xz"
digest="e0598aebe5b148419d06484e2f02aa19a2a2743915cfdd87a45da814f34d0681"
# TLOU's patched compiler must be downloaded from our source-build action.
# Never silently use the official compiler for a TSO-enabled TLOU image:
# upstream 1.3.0 marks ~80% of its units dirty with unsupported LoadMemTSO.
if [[ -n "${TLOU_AOT_COMPILER_ARCHIVE:-}" ]]; then
  cache="$TLOU_AOT_COMPILER_ARCHIVE"
  [[ -s "$cache" ]] || { echo "Patched TSO compiler archive is missing: $cache" >&2; exit 1; }
  digest=$(sha256sum "$cache" | awk '{print $1}')
else
  echo "WARNING: original upstream bb-recomp does not lower TSO loads/stores." >&2
fi
if [[ -e "$out/bin/libbbrecomp.so" && -d "$out/bundle" && -f "$out/upstream.sha256" ]]; then
  if [[ "$(cat "$out/upstream.sha256")" == "$digest" ]]; then
    echo "Pinned on-device ARM64 compiler already staged"
    exit 0
  fi
fi
mkdir -p "$(dirname "$cache")"
if [[ -z "${TLOU_AOT_COMPILER_ARCHIVE:-}" ]] && { [[ ! -s "$cache" ]] || ! echo "$digest  $cache" | sha256sum -c --status; }; then
  tmp="$cache.download"
  rm -f "$tmp"
  curl -fL --retry 3 --connect-timeout 25 --max-time 300 "$url" -o "$tmp"
  echo "$digest  $tmp" | sha256sum -c
  mv "$tmp" "$cache"
fi
echo "$digest  $cache" | sha256sum -c
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/extract" "$stage/payload/bin"
tar -xJf "$cache" -C "$stage/extract" --no-same-owner --no-same-permissions
binary=$(find "$stage/extract" -type f -name libbbrecomp.so -print -quit)
bundle=$(find "$stage/extract" -type d -name bundle -print -quit)
[[ -n "$binary" && -s "$binary" ]] || { echo "Missing upstream AArch64 compiler" >&2; exit 1; }
[[ -n "$bundle" ]] || { echo "Missing upstream LLVM/linker bundle" >&2; exit 1; }
# Do not bundle the Bloodborne game-specific variants/catalogs.
cp "$binary" "$stage/payload/bin/libbbrecomp.so"
chmod 755 "$stage/payload/bin/libbbrecomp.so"
cp -a "$bundle" "$stage/payload/bundle"
upstream_licenses=$(find "$stage/extract" -type d -name licenses -print -quit)
if [[ -n "$upstream_licenses" ]]; then cp -a "$upstream_licenses" "$stage/payload/licenses"; fi
readelf -h "$stage/payload/bin/libbbrecomp.so" | grep -Eq 'Machine:.*AArch64'
printf '%s\n' "$digest" > "$stage/payload/upstream.sha256"
rm -rf "$out"
mkdir -p "$(dirname "$out")"
mv "$stage/payload" "$out"
echo "Staged verified AArch64 recompilation tool (no game files, sha256=$digest): $out"
