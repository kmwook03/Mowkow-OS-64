from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Boot64Test(unittest.TestCase):
    def test_normal_boot_order_and_failures(self) -> None:
        self.run_native(0)

    def test_explicit_destructive_boot_gate(self) -> None:
        self.run_native(1)

    def run_native(self, boot_tests: int) -> None:
        compiler = os.environ.get("HOST_CC", "cc")
        source = ROOT / "64bit" / "src64"
        flags = ["-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=undefined", "-fno-sanitize-recover=all",
                 "-ffunction-sections", "-fdata-sections",
                 f"-DMOWKOW64_BOOT_TESTS={boot_tests}",
                 "-idirafter", str(source / "include")]
        with tempfile.TemporaryDirectory(prefix="boot64-") as temporary:
            keyboard = Path(temporary) / "keyboard.o"
            fifo = Path(temporary) / "fifo.o"
            executable = Path(temporary) / "boot64_host"
            subprocess.run(
                [compiler, *flags, "-Dinit_keyboard64=driver_init_keyboard64",
                 "-c", str(source / "drivers" / "keyboard64.c"), "-o", str(keyboard)],
                check=True, cwd=ROOT,
            )
            subprocess.run(
                [compiler, *flags, "-Dfifo64_init=boot_fixture_fifo_init64",
                 "-c", str(source / "lib" / "fifo64.c"), "-o", str(fifo)],
                check=True, cwd=ROOT,
            )
            subprocess.run(
                [compiler, *flags, str(ROOT / "64bit" / "tests" / "boot64_host.c"),
                 str(source / "kernel" / "kernel64.c"),
                 str(source / "arch" / "x86_64" / "boot_diag64.c"),
                 str(source / "arch" / "x86_64" / "boot_smoke64.c"),
                 str(source / "kernel" / "sheet64.c"),
                 str(source / "kernel" / "framebuffer64.c"),
                 str(fifo),
                 str(source / "lib" / "draw64.c"),
                 str(keyboard), "-Wl,--gc-sections", "-o", str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
