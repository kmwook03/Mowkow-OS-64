from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Utf864Test(unittest.TestCase):
    def test_native_scalars_malformed_and_display_bounds(self) -> None:
        compiler = os.environ.get("HOST_CC", "cc")
        with tempfile.TemporaryDirectory(prefix="utf864-") as temporary:
            executable = Path(temporary) / "utf864_host"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=undefined",
                    "-fno-sanitize-recover=all",
                    "-ffunction-sections",
                    "-fdata-sections",
                    "-idirafter",
                    str(ROOT / "64bit" / "src64" / "include"),
                    str(ROOT / "64bit" / "tests" / "utf864_host.c"),
                    str(ROOT / "64bit" / "src64" / "lib" / "utf864.c"),
                    str(ROOT / "64bit" / "src64" / "lib" / "draw64.c"),
                    str(ROOT / "64bit" / "src64" / "kernel" / "window64.c"),
                    "-Wl,--gc-sections",
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
