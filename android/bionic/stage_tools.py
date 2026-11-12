#!/usr/bin/env python3
"""Stage Android CPython, PKG extractor and this port's own preparation scripts."""
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def stage():
    native = ROOT / "out/android-bionic"
    python = native / "python"
    assets = native / "apk-assets"
    assets.mkdir(parents=True, exist_ok=True)
    archive = assets / "tools.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        z.write(python / "bin/python3.13", "python/bin/python3.13")
        z.write(python / "LICENSE-Python.txt", "licenses/LICENSE-Python.txt")
        for p in sorted((python / "lib").rglob("*")):
            if not p.is_file() or p.is_symlink():
                continue
            if any(x in {"__pycache__", "test", "tests", "idlelib", "tkinter", "ensurepip", "site-packages"}
                   or x.startswith("config-3.13") for x in p.parts):
                continue
            z.write(p, str(Path("python/lib") / p.relative_to(python / "lib")))
        z.write(native / "bin/orbis-pkg-util", "bin/orbis-pkg-util")
        for name in ("LICENSE-MIT", "LICENSE-APACHE"):
            z.write(ROOT / ".local-deps/android/orbis-pkg" / name, "licenses/orbis-" + name)
        for name in ("android/tlou_bionic.py", "android/device_prepare.py", "android/tlou_patches.py", "android/aot/aot_local.py", "tools/probe_tlou.py"):
            z.write(ROOT / name, "source/" + name)
        for p in sorted((ROOT / "gow3_pc/scripts").glob("*.py")):
            z.write(p, "source/gow3_pc/scripts/" + p.name)
        # Optional experimental on-device FEX-IR/LLVM AOT compiler. AOT is NOT
        # used by the game runtime yet. Never package any user's game code.
        aot = native / "aot-compiler"
        compiler = aot / "bin/libbbrecomp.so"
        if compiler.is_file():
            z.write(compiler, "bin/libbbrecomp.so")
            for folder in ("bundle", "licenses"):
                for file in sorted((aot / folder).rglob("*")):
                    if file.is_file() and not file.is_symlink():
                        z.write(file, str(Path("aot") / folder / file.relative_to(aot / folder)))
            z.write(aot / "upstream.sha256", "aot/upstream.sha256")
    jni = native / "apk-jni/arm64-v8a"
    jni.mkdir(parents=True, exist_ok=True)
    shutil.copy2(python / "lib/libpython3.13.so", jni / "libpython3.13.so")
    print(f"Staged on-device installer/preparer: {archive}")


if __name__ == "__main__":
    stage()
