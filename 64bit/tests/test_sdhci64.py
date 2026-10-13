from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Sdhci64Test(unittest.TestCase):
    def test_native_capacity_pio_and_recovery_contract(self) -> None:
        compiler = os.environ.get("HOST_CC", "cc")
        with tempfile.TemporaryDirectory(prefix="sdhci64-") as temporary:
            executable = Path(temporary) / "sdhci64_host"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{ROOT / '64bit' / 'src64' / 'include'}",
                    str(ROOT / "64bit" / "tests" / "sdhci64_host.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
