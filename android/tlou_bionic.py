#!/usr/bin/env python3
"""TLOU Remastered game-side preparation for an Android/Bionic ARM64 port.

This does not substitute the x86-64 Windows renderer/loader with the GOW3 renderer.
It creates the verified game image and version-gated patches used by a future
TLOU-specific FEX/ARM64 loader. No game content is shipped with the port.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import sys

TITLE_ID = "CUSA00552"
# This exact base SELF is shared by the newly provided executable; accept
# alternative regional SFO identifiers only when the game executable matches
# byte-for-byte. The patch writer independently validates the same SHA.
VERIFIED_SELF_SHA256 = "e5f0aa322f60bd90637cc10d83de08c77885b1921084021c630e04ed3d6febf7"
VERIFIED_SELF_111 = {
    "3cfa475f88ca68485b6b9230e62b84e195cdbe19e6f7295c110b8dcf8f20884e",
    "31f3b0d120803878d10a7184d139c545de334394dd7dd6ca6058a7c995e484a7",
}
VERIFIED_TLOU_SELFS = frozenset({VERIFIED_SELF_SHA256} | VERIFIED_SELF_111)
# PC port v0.1.6 (2026-11-04) explicitly added the Japanese CUSA00554
# alongside 00556 and 00557. This is only a metadata ALLOWLIST: other
# regional executables still require an exact approved SELF SHA-256 and
# byte-by-byte patch validation before they can be launched.
REGIONAL_TITLES = {"CUSA00552", "CUSA00554", "CUSA00556", "CUSA00557", "CUSA00559"}
_BOOT_FILES = ("eboot.bin", "sce_sys/param.sfo", "sce_module/libc.prx")


class PortError(Exception):
    pass


def read_sfo(path: Path) -> dict[str, str]:
    """Read the bounded subset of PSF needed to reject mixed-title updates."""
    raw = path.read_bytes()
    if len(raw) < 20 or len(raw) > 1024 * 1024 or raw[:4] != b"\x00PSF":
        raise PortError(f"Invalid param.sfo: {path}")
    _, key_start, value_start, count = struct.unpack_from("<4I", raw, 4)
    if count > 4096 or 20 + count * 16 > len(raw):
        raise PortError(f"Invalid SFO index: {path}")
    result: dict[str, str] = {}
    for i in range(count):
        off = 20 + i * 16
        key_off, fmt, length, _, data_off = struct.unpack_from("<HHIII", raw, off)
        start = key_start + key_off
        end = value_start + data_off
        if not (0 <= start < len(raw)) or length > len(raw) or end > len(raw) - length:
            raise PortError(f"Invalid SFO entry: {path}")
        stop = raw.find(b"\x00", start)
        if stop == -1 or stop > value_start:
            raise PortError(f"Unterminated SFO key: {path}")
        key = raw[start:stop].decode("utf-8", errors="replace")
        if fmt == 0x0404 and length == 4:
            val = str(struct.unpack_from("<I", raw, end)[0])
        else:
            val = raw[end:end+length].split(b"\x00", 1)[0].decode("utf-8", errors="replace")
        result[key] = val
    return result


def require_game(folder: Path, *, is_update: bool = False) -> dict[str, str]:
    for relative in _BOOT_FILES:
        # Updates can inherit the base libc and, if resource-only, the eboot.
        if is_update and (relative.startswith("sce_module/") or relative == "eboot.bin"):
            continue
        if not (folder / relative).is_file():
            raise PortError(f"Missing {relative}: {folder}")
    metadata = read_sfo(folder / "sce_sys/param.sfo")
    title = metadata.get("TITLE_ID")
    if is_update:
        if title not in REGIONAL_TITLES:
            raise PortError(f"Wrong PS4 update title: {title!r}")
    elif title != TITLE_ID:
        # Regional TITLE_ID alone does not prove compatible execution.
        if (title not in REGIONAL_TITLES or
            hashlib.sha256((folder / "eboot.bin").read_bytes()).hexdigest() not in VERIFIED_TLOU_SELFS):
            raise PortError(f"Wrong PS4 title: {title!r}; expected verified TLOU region")
        metadata["PORT_PROFILE"] = "TLOU-VERIFIED-SELF-SHA256"
        print(f"Recognized TLOU regional SFO {title!r} via verified SELF.", flush=True)
    return metadata


def validate_game_assets(folder: Path) -> None:
    """Inspect available PSARC files without rejecting valid installed layouts.

    In the user's working 01.00 installation, /app0 assets are already mounted
    without a physical dvdroot_ps4 directory under the selected game folder.
    The native runtime resolves them at launch. This preflight therefore
    cannot infer installation corruption from a missing optional directory.
    Never hash or rewrite game data.
    """
    root = folder / "dvdroot_ps4"
    if not root.is_dir():
        # Valid legacy app0 layout; absence is not an error and should not
        # confuse the player with a recurring warning during normal launch.
        return
    main = root / "build/ps4/main"
    if not main.is_dir():
        print("Aviso: build/ps4/main não existe sob dvdroot_ps4; "
              "sem verificação PSARC opcional.", flush=True)
        return
    if not any(main.iterdir()):
        print("Aviso: diretório build/ps4/main vazio; "
              "não impedindo a preparação do jogo.", flush=True)
        return
    archives = 0
    for archive in main.glob("*.psarc"):
        if not archive.is_file():
            continue
        # PSARC: fixed 32-byte big-endian header, then a bounded TOC. Just
        # checking "PSAR" accepts files that are truncated immediately after
        # the signature (and produces a misleading "prepared" success).
        with archive.open("rb") as data:
            header = data.read(32)
        size = archive.stat().st_size
        if len(header) != 32 or header[:4] != b"PSAR":
            raise PortError(f"Arquivo PSARC inválido ou truncado: {archive.name}. Reextraia o PKG.")
        toc_size, entry_size, file_count, block_size = struct.unpack_from(">4I", header, 12)
        if (not 30 <= entry_size <= 256 or file_count == 0 or file_count > 1_000_000 or
            block_size == 0 or block_size > 16 * 1024 * 1024 or
            toc_size < 32 + entry_size * file_count or toc_size > size):
            raise PortError(f"PSARC com índice incompleto ou inconsistente: {archive.name}. Reextraia o PKG.")
        archives += 1
    if not archives:
        print("Aviso: nenhum PSARC na raiz; recursos podem estar extraídos. Não é prova de instalação íntegra.", flush=True)


def _link_file(source: Path, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.is_symlink() or dest.is_file():
        dest.unlink()
    # App-private storage can hold symlinks; shared game storage stays read-only.
    dest.symlink_to(source.resolve())


def build_view(game: Path, update: Path | None, out: Path) -> Path:
    """Merge game/update as a read-only symlink view, never altering base data.

    Huge unmodified directories remain one link; expand only directories
    on paths that an update actually replaces.
    """
    view = out / "tlou-view"
    if view.exists() or view.is_symlink():
        if view.is_symlink() or view.is_file():
            view.unlink()
        else:
            shutil.rmtree(view)
    view.mkdir(parents=True)

    for entry in game.iterdir():
        if entry.name in {"sce_sys", "sce_module"} and entry.is_dir():
            for file in entry.rglob("*"):
                if file.is_file():
                    _link_file(file, view / entry.name / file.relative_to(entry))
        elif entry.is_file() or entry.is_dir():
            _link_file(entry, view / entry.name)

    def overlay(source: Path, target: Path):
        if source.is_symlink():
            raise PortError(f"Update contains a symbolic link: {source.name}")
        if source.is_dir():
            if target.is_symlink():
                previous = target.resolve(strict=True)
                if not previous.is_dir():
                    raise PortError(f"Update directory conflicts with file: {target.name}")
                target.unlink()
                target.mkdir()
                for item in previous.iterdir():
                    _link_file(item, target / item.name)
            elif target.exists() and not target.is_dir():
                raise PortError(f"Update directory conflicts with file: {target.name}")
            else:
                target.mkdir(parents=True, exist_ok=True)
            for item in source.iterdir():
                overlay(item, target / item.name)
        elif source.is_file():
            if target.is_dir() and not target.is_symlink():
                raise PortError(f"Update file conflicts with directory: {target.name}")
            _link_file(source, target)
        else:
            raise PortError(f"Unsupported update item: {source.name}")

    if update:
        for entry in update.iterdir():
            overlay(entry, view / entry.name)
    return view


def load_patch_module(source: Path):
    path = source / "tools/probe_tlou.py"
    if not path.is_file():
        raise PortError(f"Missing game-specific patch definitions: {path}")
    spec = importlib.util.spec_from_file_location("tlou_desktop_patches", path)
    if spec is None or spec.loader is None:
        raise PortError(f"Cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def prepare(source: Path, game: Path, update: Path | None, out: Path,
            *, flashlight: bool = False) -> dict:
    # Never create or delete files inside the player's game dump.
    if out == game or out.is_relative_to(game) or (update and (out == update or out.is_relative_to(update))):
        raise PortError("--out must be outside the game and update folders")
    base = require_game(game)
    validate_game_assets(game)
    new = require_game(update, is_update=True) if update else None
    if new and new["TITLE_ID"] != base["TITLE_ID"]:
        raise PortError(f"Update CUSA {new['TITLE_ID']} does not match base {base['TITLE_ID']}")
    out.mkdir(parents=True, exist_ok=True)
    game_view = build_view(game, update, out)
    scripts = source / "gow3_pc/scripts"
    if not scripts.is_dir():
        raise PortError(f"Missing TLOU preparation scripts: {scripts}")
    for name in ("prepare.py", "link_modules.py", "content_profile.py"):
        command = [sys.executable, str(scripts / name), str(game_view), "--out", str(out)]
        subprocess.run(command, check=True)
    if not (out / "boot-linked.bin").is_file() or not (out / "content.bin").is_file():
        raise PortError("Preparation returned without the runtime image and content profile")
    from_module = load_patch_module(source)
    patches = dict(from_module.FIXES)
    patches["job-sleep"] = from_module.JOB_SLEEP
    # The base executable has different function locations from desktop 01.11.
    # Load only this Android module; never substitute offsets from another game.
    profile_path = source / "android/tlou_patches.py"
    if profile_path.is_file():
        profile_spec = importlib.util.spec_from_file_location("tlou_android_profiles", profile_path)
        profile_module = importlib.util.module_from_spec(profile_spec)
        profile_spec.loader.exec_module(profile_module)
        patches = profile_module.select(game_view / "eboot.bin", patches)
    if flashlight:
        patches["flashlight"] = from_module.CHEATS["flashlight"]
    patch_file = out / "patches.bin"
    patch_file.unlink(missing_ok=True)
    patched = from_module.image_patches(out / "eboot.elf", patch_file, patches)
    return {
        "title_id": base.get("TITLE_ID", TITLE_ID),
        "base_version": base.get("APP_VER"),
        "update_version": new.get("APP_VER") if new else None,
        "prepared_view": str(game_view),
        "boot": str(out / "boot-linked.bin"),
        "content": str(out / "content.bin"),
        "patches": str(patched) if patched else None,
        "version_specific_patches_verified": bool(patched),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1],
                        help="Checkout of tlou-remastered-android with gow3_pc/scripts")
    parser.add_argument("--game", type=Path, required=True, help="Owned CUSA00552 dump")
    parser.add_argument("--update", type=Path, help="Optional CUSA00552 update folder")
    parser.add_argument("--out", type=Path, required=True, help="App-private preparation directory")
    parser.add_argument("--flashlight", action="store_true", help="Optional verified 1.11 cheat")
    args = parser.parse_args(argv)
    try:
        result = prepare(args.source.resolve(), args.game.resolve(),
                         args.update.resolve() if args.update else None,
                         args.out.resolve(), flashlight=args.flashlight)
    except (PortError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"TLOU Android prepare: {exc}\n")
    for key, value in result.items():
        print(f"{key}={value}")
    print("Prepared only. ARM64 FEX loader and TLOU-specific Vulkan integration are not yet complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
