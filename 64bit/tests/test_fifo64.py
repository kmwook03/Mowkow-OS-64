from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Fifo64Test(unittest.TestCase):
    def test_native_boundary_and_irq_contract(self) -> None:
        compiler = os.environ.get("HOST_CC", "cc")
        with tempfile.TemporaryDirectory(prefix="fifo64-") as temporary:
            executable = Path(temporary) / "fifo64_host"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{ROOT / '64bit' / 'src64' / 'include'}",
                    str(ROOT / "64bit" / "tests" / "fifo64_host.c"),
                    str(ROOT / "64bit" / "src64" / "lib" / "fifo64.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
