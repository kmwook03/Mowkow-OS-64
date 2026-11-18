from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Shutdown64Test(unittest.TestCase):
    def test_ordering_failures_retry_and_qemu_power_registers(self) -> None:
        with tempfile.TemporaryDirectory(prefix="shutdown64-") as temporary:
            executable = Path(temporary) / "shutdown64_host"
            subprocess.run(
                [os.environ.get("HOST_CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                 "-Werror", "-fsanitize=undefined", "-fno-sanitize-recover=all",
                 f"-I{ROOT / '64bit/src64/include'}",
                 str(ROOT / "64bit/tests/shutdown64_host.c"), "-o", str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT, timeout=10)


if __name__ == "__main__":
    unittest.main()
