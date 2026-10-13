from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class CrtSyscall64Test(unittest.TestCase):
    def test_signed_io_and_puts_progress(self) -> None:
        # Substitute native gates at link time; production wrappers are unchanged.
        names = ("write", "read", "open", "close", "seek", "exit", "puts")
        defines = [f"-D{name}=crt_{name}64" for name in names]
        with tempfile.TemporaryDirectory(prefix="crt-syscall64-") as temporary:
            executable = Path(temporary) / "crt_syscall64_host"
            subprocess.run(
                [os.environ.get("HOST_CC", "cc"), "-O2", "-std=c11", "-Wall",
                 "-Wextra", "-Werror", "-fno-builtin", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-no-pie",
                 *defines, "-Dstrlen=fixture_strlen64",
                 "-I", str(ROOT / "64bit" / "app64" / "crt" / "include"),
                 str(ROOT / "64bit" / "tests" / "crt_syscall64_host.c"),
                 str(ROOT / "64bit" / "app64" / "crt" / "syscall.c"),
                 "-o", str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"}, timeout=30,
            )


if __name__ == "__main__":
    unittest.main()
