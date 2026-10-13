from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class CrtAllocator64Test(unittest.TestCase):
    def test_overflow_corruption_and_reuse(self) -> None:
        with tempfile.TemporaryDirectory(prefix="crt-allocator64-") as temporary:
            executable = Path(temporary) / "crt_allocator64_host"
            subprocess.run(
                [os.environ.get("HOST_CC", "cc"), "-O2", "-std=c11", "-Wall",
                 "-Wextra", "-Werror", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-no-pie",
                 "-idirafter", str(ROOT / "64bit" / "src64" / "include"),
                 "-I", str(ROOT / "64bit" / "app64" / "crt" / "include"),
                 str(ROOT / "64bit" / "tests" / "crt_allocator64_host.c"),
                 "-o", str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"}, timeout=30,
            )


if __name__ == "__main__":
    unittest.main()
