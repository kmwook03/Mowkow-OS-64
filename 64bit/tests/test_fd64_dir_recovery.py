from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Fd64DirectoryRecoveryTest(unittest.TestCase):
    def test_native_directory_growth_recovery(self) -> None:
        compiler = os.environ.get("HOST_CC", "cc")
        with tempfile.TemporaryDirectory(prefix="fd64-dir-") as temporary:
            executable = Path(temporary) / "fd64_dir_recovery_host"
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
                    str(
                        ROOT
                        / "64bit"
                        / "tests"
                        / "fd64_dir_recovery_host.c"
                    ),
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
