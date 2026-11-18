"""Prepare real private ELF/process spaces with both page-table backends."""
from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ProcessMemory64Test(unittest.TestCase):
    def test_private_process_memory(self) -> None:
        with tempfile.TemporaryDirectory(prefix="process-memory64-") as temporary:
            for architecture in ("x86_64", "aarch64"):
                with self.subTest(architecture=architecture):
                    executable = Path(temporary) / architecture
                    defines = (["-DTEST_AS_AARCH64", "-D__aarch64__"]
                               if architecture == "aarch64" else [])
                    subprocess.run(
                        [os.environ.get("HOST_CC", "cc"), "-O2", "-std=c11",
                         "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                         "-fno-sanitize-recover=all", "-ffunction-sections",
                         "-fdata-sections", "-Wl,--gc-sections", *defines,
                         "-idirafter", str(ROOT / "64bit/src64/include"),
                         str(ROOT / "64bit/tests/process_memory64_host.c"),
                         "-o", str(executable)], check=True, cwd=ROOT,
                    )
                    subprocess.run([str(executable)], check=True, cwd=ROOT, timeout=30)


if __name__ == "__main__":
    unittest.main()
