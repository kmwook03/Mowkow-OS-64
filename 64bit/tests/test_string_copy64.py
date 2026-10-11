from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class StringCopy64Test(unittest.TestCase):
    def test_native_boundaries_and_callers(self) -> None:
        compiler = os.environ.get("HOST_CC", "cc")
        with tempfile.TemporaryDirectory(prefix="string-copy64-") as temporary:
            executable = Path(temporary) / "string_copy64_host"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=undefined", "-fno-sanitize-recover=all",
                 "-ffunction-sections", "-fdata-sections", "-idirafter",
                 str(ROOT / "64bit" / "src64" / "include"),
                 "-I", str(ROOT / "64bit" / "app64" / "crt" / "include"),
                 str(ROOT / "64bit" / "tests" / "string_copy64_host.c"),
                 "-Wl,--gc-sections", "-o", str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
