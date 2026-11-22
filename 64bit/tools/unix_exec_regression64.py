#!/usr/bin/env python3
"""Raw exec/stack/TLS/FP probes on disposable images; this is not a libc test."""
from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path

import mkfat32_64
from process_regression64 import ProcessConsole

ROOT = Path(__file__).resolve().parents[2]


class ExecConsole(ProcessConsole):
    def _check_alive(self) -> None:
        super()._check_alive()
        if "exec: FAIL" in self.text() or "USER EXCEPTION" in self.text():
            raise AssertionError(f"execution probe failed; see {self.log_path}")


def build_probe(folder: Path, architecture: str, launcher: bool) -> Path:
    compiler = "x86_64-elf-gcc" if architecture == "x86_64" else "aarch64-none-elf-gcc"
    sysroot = ROOT / "build64/unix64" / architecture / "sysroot"
    output = folder / f"{architecture}-{'launcher' if launcher else 'probe'}.elf"
    script = ROOT / ("64bit/app64/app64.ld" if launcher else "64bit/unix64/unix64.ld")
    command = [compiler, "-O2", "-ffreestanding", "-fno-stack-protector", "-fno-pic",
               "-fno-pie", "-fno-asynchronous-unwind-tables", "-nostdlib", "-static",
               "-Wall", "-Wextra", "-Werror", "-I", str(sysroot / "usr/include"),
               "-Wl,-z,noexecstack", "-Wl,-T," + str(script), "-o", str(output)]
    command += ["-mno-red-zone"] if architecture == "x86_64" else ["-mgeneral-regs-only"]
    if launcher:
        command += ["-DEXEC_LAUNCHER64"]
    command += [str(ROOT / "64bit/tests/unix_exec_entry64.S"),
                str(ROOT / "64bit/tests/unix_exec_probe64.c"),
                str(sysroot / "usr/lib/syscall64.o")]
    subprocess.run(command, check=True, timeout=30)
    return output


def run(ahci: bool, log: str) -> None:
    with tempfile.TemporaryDirectory(prefix="unix-exec64-") as temporary:
        folder = Path(temporary)
        launcher = build_probe(folder, "x86_64", True)
        probe = build_probe(folder, "x86_64", False)
        build_probe(folder, "aarch64", True)
        build_probe(folder, "aarch64", False)
        bad = folder / "bad.elf"
        bad.write_bytes(b"invalid ELF")
        image = folder / "disk.img"
        mkfat32_64.build(image, (ROOT / "build64/boot/boot64.bin").read_bytes(),
                        (ROOT / "build64/boot/loader64.bin").read_bytes(),
                        (ROOT / "build64/kernel/kernel64.bin").read_bytes(),
                        (ROOT / "common/font/H04.FNT").read_bytes(),
                        [f"uxlaunch={launcher}", f"uxprobe={probe}", f"uxbad={bad}"])
        with ExecConsole(str(image), ahci, log) as console:
            console.setup_windows()
            before = len(console.text())
            for index in range(2):
                console.focus(index)
                console.type(f"uxlaunch {index + 1}")
                console.modes[index] = console.hangul
                console._send(["ret"])
                console.wait_seen(before, "exec: ROLLBACK PASS\n", 20)
            for index in range(2):
                console.wait_seen(before, f"exec {index + 1}: WAIT\n", 70)
            for index in range(2):
                console.focus(index)
                offset = len(console.text())
                console.type("ok")
                console._send(["ret"])
                console.wait_seen(offset, f"exec {index + 1}: PASS\n", 20)
                console.wait_seen(offset, "종료 코드: 0\n", 20)
            output = console.text()[before:]
            if "FAIL" in output or "EXCEPTION" in output or output.count("종료 코드: 0") != 2:
                raise AssertionError(output)
        print("exec rollback, repeated replacement, stack/auxv, "
              "TLS isolation, FP reset, sleep: PASS")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ahci", action="store_true")
    parser.add_argument("--log", default="/tmp/mowkow-unix-exec64.log")
    args = parser.parse_args()
    run(args.ahci, args.log)


if __name__ == "__main__":
    main()
