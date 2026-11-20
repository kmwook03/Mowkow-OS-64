#!/usr/bin/env python3
"""Offline, disposable Linux/mlibc static-link probe for the UNIX P0 study."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tarfile


INPUTS = Path(__file__).resolve().parents[1] / "experiments" / "unix_p0"


def run(command: list[str], work: Path, env: dict[str, str],
        *, timeout: int = 600) -> str:
    """Keep exact commands and diagnostics, including failed build stages."""
    with (work / "build.log").open("a") as log:
        log.write("$ " + shlex.join(command) + "\n")
        log.flush()
        result = subprocess.run(command, cwd=work, env=env, text=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=timeout)
        log.write(result.stdout)
    result.check_returncode()
    return result.stdout


def unpack(archive: Path, destination: Path, expected_hash: str) -> None:
    """Only extract authenticated archives, with Python's path/link filter."""
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected_hash:
        raise ValueError(f"source SHA256 mismatch: {archive}")
    destination.mkdir(parents=True)
    with tarfile.open(archive) as source:
        roots = {Path(member.name).parts[0] for member in source.getmembers()}
        if len(roots) != 1:
            raise ValueError(f"expected one archive root: {archive}")
        source.extractall(destination, filter="data")
    root = destination / roots.pop()
    for child in root.iterdir():
        child.rename(destination / child.name)
    root.rmdir()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archives", type=Path, required=True,
                        help="directory with NAME.tar.gz from sources.json")
    parser.add_argument("--work", type=Path, required=True,
                        help="new directory; never reuse or overwrite a build")
    parser.add_argument("--linux-headers", type=Path, required=True,
                        help="isolated Linux UAPI include directory")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("this probe only executes on x86_64 Linux")
    if args.jobs < 1 or args.jobs > 32:
        parser.error("jobs must be between 1 and 32")
    headers = args.linux_headers.resolve()
    if not all((headers / part).is_dir() for part in ("linux", "asm", "asm-generic")):
        parser.error("Linux UAPI headers need linux, asm, and asm-generic directories")
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    archives = args.archives.resolve()
    manifest = json.loads((INPUTS / "sources.json").read_text())
    (work / "sources.json").write_text(json.dumps(manifest, indent=2) + "\n")
    source = work / "source"
    unpack(archives / "mlibc.tar.gz", source, manifest["mlibc"]["sha256"])
    for name, details in manifest.items():
        if name == "mlibc":
            continue
        dest = source / "subprojects" / name
        unpack(archives / f"{name}.tar.gz", dest, details["sha256"])
        overlay = source / "subprojects" / "packagefiles" / name
        if overlay.is_dir():
            shutil.copytree(overlay, dest, dirs_exist_ok=True)
    env = dict(os.environ, CC="gcc", CXX="g++", LC_ALL="C")
    for tool in ("gcc", "g++", "ld", "meson", "ninja", "readelf"):
        run([tool, "--version"], work, env)
    run(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(source), "-i",
         str(INPUTS / "0001-puts-newline-result.patch")], work, env)
    build = work / "build"
    sysroot = work / "sysroot"
    run(["meson", "setup", str(build), str(source), "--prefix=/usr",
         "--libdir=lib", "--wrap-mode=nodownload", "-Ddefault_library=static",
         "-Dbuildtype=release", "-Duse_freestnd_hdrs=enabled",
         f"-Dlinux_kernel_headers={headers}"], work, env)
    run(["ninja", "-C", str(build), f"-j{args.jobs}"], work, env)
    run(["ninja", "-C", str(build), "install"], work,
        dict(env, DESTDIR=str(sysroot)))
    lib = sysroot / "usr" / "lib"
    compiler_include = run(["gcc", "-print-file-name=include"], work, env).strip()
    compiler_objects = {
        name: run(["gcc", f"-print-file-name={name}.o"], work, env).strip()
        for name in ("crtbegin", "crtend")
    }
    binary = work / "probe.elf"
    run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-nostdinc", "-isystem", str(sysroot / "usr" / "include"),
         "-isystem", compiler_include, "-nostdlib", "-static", "-no-pie",
         "-Wl,-z,noexecstack", f"-Wl,-Map={work / 'probe.map'}",
         str(lib / "crt1.o"), str(lib / "crti.o"), compiler_objects["crtbegin"],
         str(INPUTS / "libc_probe64.c"), f"-L{lib}", "-Wl,--start-group",
         "-lc", "-lm", "-lgcc", "-lgcc_eh", "-Wl,--end-group",
         compiler_objects["crtend"], str(lib / "crtn.o"), "-o", str(binary)],
        work, env)
    elf = run(["readelf", "-hlWd", str(binary)], work, env)
    (work / "elf.log").write_text(elf)
    if "INTERP" in elf or "DYNAMIC" in elf or "TLS" not in elf:
        raise RuntimeError("expected static ELF with PT_TLS and no dynamic loader")
    undefined = run(["nm", "-u", str(binary)], work, env)
    if undefined.strip():
        raise RuntimeError(f"unresolved probe symbols: {undefined}")
    result = subprocess.run([str(binary), "probe"], cwd=work,
                            env=dict(env, MOWKOW_P0="static"), input="p0-input\n",
                            text=True, capture_output=True, timeout=30)
    expected = "p0: argv env auxv alloc io tls errno pthread PASS\np0: atexit\n"
    report = {"returncode": result.returncode, "stdout": result.stdout,
              "stderr": result.stderr, "target": "x86_64-linux-mlibc",
              "mowkow_runtime_tested": False,
              "elf_sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
    (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    if result.returncode != 37 or result.stdout != expected or result.stderr:
        raise RuntimeError(f"probe failed; see {work / 'result.json'}")
    with open("/dev/full", "wb") as full:
        failure = subprocess.run([str(binary), "full"], cwd=work, env=env,
                                 stdout=full, stderr=subprocess.PIPE, timeout=30)
    report["puts_full_returncode"] = failure.returncode
    (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    if failure.returncode != 38 or failure.stderr:
        raise RuntimeError("puts newline write failure was not reported")
    print(f"Linux/mlibc static probe PASS (Mowkow runtime untested): {work}")


if __name__ == "__main__":
    main()
