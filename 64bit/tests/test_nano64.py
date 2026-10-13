from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Nano64Test(unittest.TestCase):
    def test_editor_bounds_utf8_and_file_failures(self) -> None:
        with tempfile.TemporaryDirectory(prefix="nano64-") as temporary:
            executable = Path(temporary) / "nano64_host"
            subprocess.run(
                [os.environ.get("HOST_CC", "cc"), "-O2", "-std=c11", "-Wall",
                 "-Wextra", "-Werror", "-fno-builtin", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-no-pie",
                 "-I", str(ROOT / "64bit" / "app64" / "crt" / "include"),
                 "-idirafter", str(ROOT / "64bit" / "src64" / "include"),
                 str(ROOT / "64bit" / "tests" / "nano64_host.c"),
                 str(ROOT / "64bit" / "src64" / "lib" / "utf864.c"),
                 "-o", str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run(
                [str(executable)], check=True, cwd=ROOT,
                env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"}, timeout=30,
            )


if __name__ == "__main__":
    unittest.main()
