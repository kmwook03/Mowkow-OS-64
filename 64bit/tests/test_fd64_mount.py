from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Fd64MountTest(unittest.TestCase):
    def test_native_mount_geometry_validation(self) -> None:
        compiler = os.environ.get("HOST_CC", "cc")
        with tempfile.TemporaryDirectory(prefix="fd64-mount-") as temporary:
            executable = Path(temporary) / "fd64_mount_host"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-ffunction-sections",
                    "-fdata-sections",
                    f"-I{ROOT / '64bit' / 'src64' / 'include'}",
                    str(ROOT / "64bit" / "tests" / "fd64_mount_host.c"),
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
