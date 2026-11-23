#!/usr/bin/env python3
"""Execute real static mlibc probes on a disposable x86 Mowkow image."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import shutil
import tempfile

import mkfat32_64
from process_regression64 import ProcessConsole

ROOT = Path(__file__).resolve().parents[2]


class LibcConsole(ProcessConsole):
    def _check_alive(self) -> None:
        super()._check_alive()
        if any(marker in self.text() for marker in
               ("libc: FAIL", "__ensure(", "종료 코드: 127")):
            raise AssertionError(f"libc probe failed; see {self.log_path}")


def build_probe(folder: Path, architecture: str, sysroot: Path) -> tuple[Path, Path]:
    prefix = "x86_64-elf" if architecture == "x86_64" else "aarch64-none-elf"
    lib = sysroot / "usr/lib"
    compiler = prefix + "-gcc"
    compiler_objects = {name: subprocess.check_output(
        [compiler, f"-print-file-name={name}.o"], text=True).strip()
        for name in ("crtbegin", "crtend")}
    probe, launcher = folder / f"{architecture}-libc.elf", folder / f"{architecture}-launch.elf"
    compiler_include = subprocess.check_output(
        [compiler, "-print-file-name=include"], text=True).strip()
    common = [compiler, "-D__mowkow__", "-D_GNU_SOURCE", "-std=c11", "-O2",
              "-fno-stack-protector",
              "-ffreestanding", "-fno-pie", "-no-pie", "-nostdinc", "-nostdlib", "-static",
              "-isystem", str(sysroot / "usr/include"), "-isystem", compiler_include,
              "-Wl,-z,noexecstack",
              "-Wall", "-Wextra", "-Werror"]
    common += ["-mno-red-zone"] if architecture == "x86_64" else ["-mno-outline-atomics"]
    subprocess.run(common + ["-Wl,-T," + str(lib / "unix64.ld"), str(lib / "crt1.o"),
                            compiler_objects["crtbegin"],
                            str(ROOT / "64bit/tests/unix_libc_probe64.c"),
                            str(ROOT / "64bit/tests/unix_signal_return64.S"), f"-L{lib}",
                            "-Wl,--start-group", "-lc", "-lgcc",
                            "-Wl,--end-group", compiler_objects["crtend"], "-o", str(probe)],
                   check=True, timeout=60)
    subprocess.run(common + ["-DEXEC_LAUNCHER64", "-Wl,-T," +
                            str(ROOT / "64bit/app64/app64.ld"),
                            str(ROOT / "64bit/tests/unix_exec_entry64.S"),
                            str(ROOT / "64bit/tests/unix_libc_launcher64.c"),
                            str(lib / "syscall64.o"), "-o", str(launcher)],
                   check=True, timeout=30)
    headers = subprocess.check_output([prefix + "-readelf", "-lW", str(probe)], text=True)
    if "TLS" not in headers or "INTERP" in headers or "DYNAMIC" in headers:
        raise AssertionError("expected a static ELF with TLS and no dynamic loader")
    if subprocess.check_output([prefix + "-nm", "-u", str(probe)], text=True).strip():
        raise AssertionError("undefined symbols in static probe")
    return probe, launcher


def run(sysroot: Path, aarch64: Path | None, ahci: bool, log: str) -> None:
    with tempfile.TemporaryDirectory(prefix="unix-libc64-") as temporary:
        folder = Path(temporary)
        probe, launcher = build_probe(folder, "x86_64", sysroot)
        # Preserve the exact executable beside serial diagnostics for addr2line.
        Path(log).resolve().parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(probe, str(Path(log).resolve()) + ".elf")
        if aarch64 is not None:
            build_probe(folder, "aarch64", aarch64)
        data = folder / "input.txt"
        data.write_bytes(b"libc-input\n")
        image = folder / "disk.img"
        mkfat32_64.build(image, (ROOT / "build64/boot/boot64.bin").read_bytes(),
                        (ROOT / "build64/boot/loader64.bin").read_bytes(),
                        (ROOT / "build64/kernel/kernel64.bin").read_bytes(),
                        (ROOT / "common/font/H04.FNT").read_bytes(),
                        [f"libclaunch={launcher}", f"libcprobe={probe}", f"libc.txt={data}"])
        with LibcConsole(str(image), ahci, log) as console:
            console.setup_windows()
            before = len(console.text())
            for index in range(2):
                console.focus(index)
                console.type(f"libclaunch {index + 1}")
                console.modes[index] = console.hangul
                console._send(["ret"])
                console.wait_seen(before, f"libc {index + 1}: FORK PASS\n", 90)
                console.wait_seen(before, f"libc {index + 1}: WAIT\n", 20)
            for index in range(2):
                console.focus(index)
                offset = len(console.text())
                console.type("ok")
                console._send(["ret"])
                console.wait_seen(offset, f"libc {index + 1}: PASS\n", 20)
                console.wait_seen(offset, f"libc {index + 1}: ATEXIT\n", 20)
                console.wait_seen(offset, "libc: DTOR\n", 20)
                console.wait_seen(offset, "종료 코드: 37\n", 20)
            output = console.text()[before:]
            if (output.count("종료 코드: 37") != 2 or "FAIL" in output or
                    output.count("USER EXCEPTION") != 2):
                raise AssertionError(output)
        print("mlibc startup, constructors, auxv, alloc, stdio, files, futex timeout, "
              "TLS/errno isolation, fork/exec/wait/OFD, child fault, internal signal/FP return, "
              "atexit: PASS")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sysroot", type=Path, required=True)
    parser.add_argument("--aarch64-sysroot", type=Path)
    parser.add_argument("--ahci", action="store_true")
    parser.add_argument("--log", default="/tmp/mowkow-unix-libc64.log")
    args = parser.parse_args()
    run(args.sysroot.resolve(), args.aarch64_sysroot, args.ahci, args.log)


if __name__ == "__main__":
    main()
