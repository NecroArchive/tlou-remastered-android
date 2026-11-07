#!/usr/bin/env python3
"""Reuse only the native/installer payload of a pinned, successfully verified APK."""
from pathlib import Path, PurePosixPath
import copy
import hashlib
import io
import subprocess
import sys
import zipfile

BASE = "0916e9f78d3ed1fc5b4747f4c2846da40d4d8761"
APK_SHA = "43f9543decfd3d5f9a717937e0404f320ec853fe0c1add242c55fae32316c6cb"
ROOT = Path(__file__).resolve().parents[2]
# These exact source changes do not alter any native binary. Only the
# known executable patch table may replace one source member in tools.zip.
PREPARER_SOURCES = {"android/tlou_patches.py"}
PACKAGING_ONLY = {"android/bionic/reuse_apk.py", "android/tests/test_launcher_reuse.py",
                  "android/tests/test_submesh_bounds.py", "android/tests/test_tlou_patches.py",
                  "android/STATIC_SUBMESH_CRASH_20261106.md"}


def unchanged():
    paths = subprocess.check_output([
        "git", "diff", "--name-only", BASE, "HEAD", "--", "gow3_pc", "ci",
        "android", "tools", ".gitmodules",
        "android-launcher/app/src/main/java/ro/cobrabm/fexdroid/Gow3Present.java"], cwd=ROOT, text=True).splitlines()
    return not (set(paths) - PACKAGING_ONLY - PREPARER_SOURCES)


def payload(name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("Unsafe APK path")
    if name.startswith("lib/arm64-v8a/"):
        return Path("apk-jni") / Path(name).relative_to("lib")
    if name == "assets/tools.zip" or name.startswith(("assets/runtime/", "assets/drivers/")):
        return Path("apk-assets") / Path(name).relative_to("assets")
    return None


def refresh_patch_source(archive):
    original = archive.read_bytes()
    with zipfile.ZipFile(io.BytesIO(original)) as old:
        replacements = {"source/"+p: (ROOT/p).read_bytes() for p in PREPARER_SOURCES}
        if all(old.read(p) == data for p, data in replacements.items()):
            return
        output = io.BytesIO()
        with zipfile.ZipFile(output,"w",zipfile.ZIP_DEFLATED,compresslevel=6) as new:
            for info in old.infolist():
                new.writestr(copy.copy(info),replacements.get(info.filename,old.read(info.filename)))
        # Every interpreter, extension, extractor, license and other source byte
        # must survive repackaging. Refuse an unexpected extra/missing member.
        with zipfile.ZipFile(io.BytesIO(output.getvalue())) as new:
            assert new.namelist() == old.namelist()
            for name in old.namelist():
                assert new.read(name) == replacements.get(name,old.read(name)), name
        archive.write_bytes(output.getvalue())


def stage(apk):
    if not unchanged():
        raise ValueError("Native or installer source changed: full build required")
    if hashlib.sha256(apk.read_bytes()).hexdigest() != APK_SHA:
        raise ValueError("APK does not match pinned successful source build")
    with zipfile.ZipFile(apk) as z:
        for item in z.infolist():
            target = payload(item.filename)
            if target is None or item.is_dir():
                continue
            dest = ROOT / "out/android-bionic" / target
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(z.read(item))
    refresh_patch_source(ROOT / "out/android-bionic/apk-assets/tools.zip")
    print("Verified native physics runtime and drivers staged; executable patch table refreshed")


if __name__ == "__main__":
    if sys.argv[1:] == ["--check"]:
        print(str(unchanged()).lower())
    else:
        stage(Path(sys.argv[1]))
