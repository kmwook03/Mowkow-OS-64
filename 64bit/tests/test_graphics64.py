from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Graphics64Test(unittest.TestCase):
    def test_native_geometry_clipping_and_composition(self) -> None:
        compiler = os.environ.get("HOST_CC", "cc")
        with tempfile.TemporaryDirectory(prefix="graphics64-") as temporary:
            executable = Path(temporary) / "graphics64_host"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=undefined", "-fno-sanitize-recover=all",
                 "-ffunction-sections", "-fdata-sections", "-idirafter",
                 str(ROOT / "64bit" / "src64" / "include"),
                 str(ROOT / "64bit" / "tests" / "graphics64_host.c"),
                 str(ROOT / "64bit" / "src64" / "lib" / "draw64.c"),
                 str(ROOT / "64bit" / "src64" / "lib" / "hangul64.c"),
                 str(ROOT / "64bit" / "src64" / "lib" / "utf864.c"),
                 str(ROOT / "64bit" / "src64" / "kernel" / "window64.c"),
                 str(ROOT / "64bit" / "src64" / "kernel" / "framebuffer64.c"),
                 "-Wl,--gc-sections", "-o", str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
