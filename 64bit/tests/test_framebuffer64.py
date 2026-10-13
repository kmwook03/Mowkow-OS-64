from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Framebuffer64Test(unittest.TestCase):
    def test_backends_flush_ranges_and_firmware_response(self) -> None:
        source = ROOT / "64bit" / "src64"
        with tempfile.TemporaryDirectory(prefix="framebuffer64-") as temporary:
            executable = Path(temporary) / "framebuffer64_host"
            subprocess.run(
                [os.environ.get("HOST_CC", "cc"), "-O2", "-std=c11", "-Wall",
                 "-Wextra", "-Werror", "-fsanitize=undefined",
                 "-fno-sanitize-recover=all", "-ffunction-sections", "-fdata-sections",
                 "-idirafter", str(source / "include"),
                 str(ROOT / "64bit" / "tests" / "framebuffer64_host.c"),
                 str(source / "kernel" / "framebuffer64.c"),
                 str(source / "lib" / "draw64.c"),
                 str(source / "lib" / "hangul64.c"),
                 str(source / "lib" / "utf864.c"),
                 "-Wl,--gc-sections", "-o", str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
