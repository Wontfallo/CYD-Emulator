"""Build the portable download: unzip anywhere on 64-bit Windows, no Python install needed.

    python tools/make_portable.py            -> dist/cydemu-portable-win64-v<version>.zip

The bundle carries its own Python runtime, assembled from the interpreter running this script
(a python.org install), in the same shape as python.org's "embeddable package":
python.exe + pythonXY.dll + the extension modules + the standard library in pythonXY.zip, and
a ._pth file that pins sys.path so nothing on the host machine can leak in.
"""
from __future__ import annotations

import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT.parent
sys.path.insert(0, str(ROOT))
from cydemu import __version__  # noqa: E402

NAME = "cydemu-portable-win64"
DIST = ROOT / "dist"
OUT = DIST / NAME

STDLIB_SKIP_DIRS = {"site-packages", "test", "tests", "tkinter", "idlelib", "turtledemo", "ensurepip",
                    "lib2to3", "pydoc_data", "venv", "__pycache__", "__phello__", "idle_test"}
DLL_SKIP = ("tcl", "tk", "_tkinter", "_test", "_ctypes_test", "xxlimited")

LAUNCHERS = {
    "Flash firmware (drop a .bin here).cmd": r'''@echo off
rem Drag an ESP32 firmware .bin (app or merged image) or a build folder onto this file.
setlocal
if "%~1"=="" (
    echo Drag a firmware .bin onto this file, or run:  cydemu flash path\to\firmware.bin
    pause
    exit /b 1
)
call "%~dp0cydemu.cmd" flash "%~1"
if errorlevel 1 ( pause & exit /b 1 )
call "%~dp0cydemu.cmd" view --open
''',
    "Flash demo firmware.cmd": r'''@echo off
rem Flashes the bundled HYDRO-DECK demo firmware onto a factory-fresh virtual CYD and opens the live view.
setlocal
call "%~dp0cydemu.cmd" flash "%~dp0examples\hydro-deck-ili9341" --erase
if errorlevel 1 ( pause & exit /b 1 )
call "%~dp0cydemu.cmd" view --open
''',
    "Open live view.cmd": r'''@echo off
call "%~dp0cydemu.cmd" view --open
if errorlevel 1 pause
''',
    "Stop virtual CYD.cmd": r'''@echo off
call "%~dp0cydemu.cmd" stop
''',
}


def copy_tree(src: Path, dst: Path, ignore=None) -> None:
    shutil.copytree(src, dst, ignore=ignore, dirs_exist_ok=True)


def build_python(dst: Path) -> None:
    base = Path(sys.base_prefix)
    ver = f"{sys.version_info.major}{sys.version_info.minor}"
    dst.mkdir(parents=True)
    for name in ("python.exe", "pythonw.exe", f"python{ver}.dll", "python3.dll",
                 "vcruntime140.dll", "vcruntime140_1.dll", "LICENSE.txt"):
        if (base / name).exists():
            shutil.copy2(base / name, dst / name)
    for f in sorted((base / "DLLs").iterdir()):
        if f.suffix.lower() in (".pyd", ".dll") and not f.name.lower().startswith(DLL_SKIP):
            shutil.copy2(f, dst / f.name)
    lib = base / "Lib"
    with zipfile.ZipFile(dst / f"python{ver}.zip", "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in sorted(lib.rglob("*.py")):
            rel = f.relative_to(lib)
            if any(part in STDLIB_SKIP_DIRS for part in rel.parts):
                continue
            z.write(f, rel.as_posix())
    # sys.path, relative to python.exe: the stdlib zip, the extension modules, and the app root
    (dst / f"python{ver}._pth").write_text(f"python{ver}.zip\n.\n..\n", "ascii")


def main() -> None:
    for need in (ROOT / "core" / "cyd_core.dll", ROOT / "vendor" / "qemu" / "libqemu-xtensa.dll"):
        if not need.exists():
            raise SystemExit(f"missing {need}")
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)

    pyc = shutil.ignore_patterns("__pycache__", "*.pyc")
    copy_tree(ROOT / "cydemu", OUT / "cydemu", pyc)
    copy_tree(ROOT / "vendor" / "qemu", OUT / "vendor" / "qemu")
    copy_tree(ROOT / "defaults", OUT / "defaults")
    (OUT / "core").mkdir()
    for name in ("cyd_core.dll", "cyd_core.c", "build.bat"):
        shutil.copy2(ROOT / "core" / name, OUT / "core" / name)
    for name in ("README.md", "LICENSE", "cydemu.cmd"):
        shutil.copy2(ROOT / name, OUT / name)

    demo = OUT / "examples" / "hydro-deck-ili9341"
    demo.mkdir(parents=True)
    dist = REPO / "firmware_dist"
    for src, dst in (("HYDRO-DECK_E32R28T_ILI9341_firmware.bin", "firmware.bin"),
                     ("HYDRO-DECK_E32R28T_ILI9341_bootloader.bin", "bootloader.bin"),
                     ("HYDRO-DECK_E32R28T_ILI9341_partitions.bin", "partitions.bin"),
                     ("boot_app0.bin", "boot_app0.bin")):
        shutil.copy2(dist / src, demo / dst)

    for name, body in LAUNCHERS.items():
        (OUT / name).write_text(body.replace("\n", "\r\n"), "ascii")

    build_python(OUT / "python")

    archive = DIST / f"{NAME}-v{__version__}.zip"
    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in sorted(OUT.rglob("*")):
            if f.is_file():
                z.write(f, (Path(NAME) / f.relative_to(OUT)).as_posix())
    size = sum(f.stat().st_size for f in OUT.rglob("*") if f.is_file())
    print(f"{OUT}  ({size / 1e6:.1f} MB unpacked)")
    print(f"{archive}  ({archive.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
