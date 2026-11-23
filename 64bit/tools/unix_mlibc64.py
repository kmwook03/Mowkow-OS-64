#!/usr/bin/env python3
"""Build pinned mlibc for Mowkow offline in a new, disposable directory."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil

from unix_libc_probe64 import INPUTS, run, unpack

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "64bit/unix64/mlibc"


def build(archives: Path, work: Path, architecture: str, jobs: int, sdk: Path) -> None:
    for required in ("usr/include/mowkow/unix64.h", "usr/lib/crt1.o",
                     "usr/lib/syscall64.o", "usr/lib/unix64.ld"):
        if not (sdk / required).is_file():
            raise FileNotFoundError(f"build unix64-sdk first: {sdk / required}")
    work.mkdir(parents=True, exist_ok=False)
    manifest = json.loads((INPUTS / "sources.json").read_text())
    source = work / "source"
    unpack(archives / "mlibc.tar.gz", source, manifest["mlibc"]["sha256"])
    for name, details in manifest.items():
        if name == "mlibc":
            continue
        destination = source / "subprojects" / name
        unpack(archives / f"{name}.tar.gz", destination, details["sha256"])
        overlay = source / "subprojects/packagefiles" / name
        if overlay.is_dir():
            shutil.copytree(overlay, destination, dirs_exist_ok=True)
    port = source / "sysdeps/mowkow"
    shutil.copytree(PORT, port)
    # Reuse pinned libc ABI definitions, never Linux sysdeps or host headers.
    shutil.copytree(source / "sysdeps/linux/include/abi-bits", port / "include/abi-bits")
    shutil.copytree(sdk / "usr/include/mowkow", port / "include/mowkow")
    shutil.copyfile(ROOT / "64bit/unix64/crt/syscall64.c", port / "syscall64.c")
    meson = source / "meson.build"
    text = meson.read_text()
    anchor = "if host_machine.system() == 'linux'\n"
    assert text.count(anchor) == 1
    text = text.replace(anchor, "if host_machine.system() == 'mowkow'\n"
                        "\trtld_include_dirs += include_directories('sysdeps/mowkow/include')\n"
                        "\tlibc_include_dirs += include_directories('sysdeps/mowkow/include')\n"
                        "\tsubdir('sysdeps/mowkow')\nel" + anchor)
    meson.write_text(text)
    prefix = "x86_64-elf" if architecture == "x86_64" else "aarch64-none-elf"
    flags = ["-D__mowkow__", "-D_GNU_SOURCE", "-fno-stack-protector", "-ftls-model=initial-exec"]
    flags += ["-mno-red-zone"] if architecture == "x86_64" else ["-mno-outline-atomics"]
    cross = work / "cross.ini"
    cross.write_text(
        "[binaries]\n" + "\n".join(
            f"{key} = '{prefix}-{tool}'" for key, tool in
            (("c", "gcc"), ("cpp", "g++"), ("ar", "ar"), ("strip", "strip"))) +
        "\n[host_machine]\nsystem = 'mowkow'\n" +
        f"cpu_family = '{architecture}'\ncpu = '{architecture}'\nendian = 'little'\n" +
        "[properties]\nneeds_exe_wrapper = true\n[built-in options]\n" +
        f"c_args = {flags!r}\ncpp_args = {flags!r}\n")
    env = dict(os.environ, LC_ALL="C")
    for tool in (f"{prefix}-gcc", f"{prefix}-g++", "meson", "ninja"):
        run([tool, "--version"], work, env)
    run(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(source), "-i",
         str(INPUTS / "0001-puts-newline-result.patch")], work, env)
    run(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(source), "-i",
         str(PORT / "0001-bare-metal-fast-types.patch")], work, env)
    output = work / "build"
    run(["meson", "setup", str(output), str(source), "--cross-file", str(cross),
         "--prefix=/usr", "--libdir=lib", "--wrap-mode=nodownload",
         "-Ddefault_library=static", "-Dbuildtype=release", "-Duse_freestnd_hdrs=enabled",
         "-Dlinux_option=disabled", "-Dbsd_option=enabled"], work, env)
    run(["ninja", "-C", str(output), f"-j{jobs}"], work, env)
    sysroot = work / "sysroot"
    run(["ninja", "-C", str(output), "install"], work, dict(env, DESTDIR=str(sysroot)))
    shutil.copytree(sdk, sysroot, dirs_exist_ok=True)
    (work / "sources.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Mowkow {architecture} mlibc built: {sysroot}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archives", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--architecture", choices=("x86_64", "aarch64"), required=True)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--sdk", type=Path, help="architecture SDK seed sysroot")
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32:
        parser.error("jobs must be between 1 and 32")
    sdk = args.sdk or ROOT / "build64/unix64" / args.architecture / "sysroot"
    build(args.archives.resolve(), args.work.resolve(), args.architecture, args.jobs, sdk.resolve())


if __name__ == "__main__":
    main()
