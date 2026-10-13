from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Rollback64Test(unittest.TestCase):
    def test_native_partial_initialization_rollback(self) -> None:
        compiler = os.environ.get("HOST_CC", "cc")
        with tempfile.TemporaryDirectory(prefix="rollback64-") as temporary:
            for architecture in ("x86_64", "aarch64"):
                with self.subTest(architecture=architecture):
                    executable = Path(temporary) / f"rollback64_{architecture}"
                    defines = ["-D__aarch64__"] if architecture == "aarch64" else []
                    subprocess.run(
                        [
                            compiler,
                            "-std=c11",
                            "-Wall",
                            "-Wextra",
                            "-Werror",
                            "-no-pie",
                            *defines,
                            f"-I{ROOT / '64bit' / 'src64' / 'include'}",
                            str(ROOT / "64bit" / "tests" / "rollback64_host.c"),
                            str(ROOT / "64bit" / "src64" / "drivers" / "ahci64.c"),
                            "-o",
                            str(executable),
                        ],
                        check=True,
                        cwd=ROOT,
                    )
                    subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
