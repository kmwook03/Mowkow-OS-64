from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Scheduler64Test(unittest.TestCase):
    def test_native_state_and_queue_contract(self) -> None:
        compiler = os.environ.get("HOST_CC", "cc")
        with tempfile.TemporaryDirectory(prefix="scheduler64-") as temporary:
            executable = Path(temporary) / "scheduler64_host"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DMOWKOW64_TASK_STACK_DEBUG=1",
                    f"-I{ROOT / '64bit' / 'src64' / 'include'}",
                    str(ROOT / "64bit" / "tests" / "scheduler64_host.c"),
                    str(ROOT / "64bit" / "src64" / "kernel" / "memory64.c"),
                    str(ROOT / "64bit" / "src64" / "kernel" / "mtask64.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
