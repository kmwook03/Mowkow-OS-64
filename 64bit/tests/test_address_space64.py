from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class AddressSpace64Test(unittest.TestCase):
    def test_x86_private_tables_and_lifecycle(self) -> None:
        self.run_native(())

    def test_aarch64_private_tables_and_lifecycle(self) -> None:
        self.run_native(("-DTEST_AS_AARCH64",))

    def run_native(self, defines: tuple[str, ...]) -> None:
        with tempfile.TemporaryDirectory(prefix="address-space64-") as temporary:
            executable = Path(temporary) / "address_space64_host"
            subprocess.run(
                [os.environ.get("HOST_CC", "cc"), "-O2", "-std=c11", "-Wall",
                 "-Wextra", "-Werror", "-fsanitize=undefined",
                 "-fno-sanitize-recover=all", *defines,
                 "-idirafter", str(ROOT / "64bit/src64/include"),
                 str(ROOT / "64bit/tests/address_space64_host.c"),
                 "-o", str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT, timeout=20)


if __name__ == "__main__":
    unittest.main()
