from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Aarch64Platform64Test(unittest.TestCase):
    def test_boot_keyboard_and_explicit_storage_probes(self) -> None:
        self.run_native(
            "aarch64_boot_smoke64_host.c",
            ("arch/aarch64/boot_smoke64.c", "arch/aarch64/usbhid64.c", "lib/fifo64.c"),
            ("-DMOWKOW64_BOOT_TESTS=1",),
        )

    def test_deadline_arithmetic_and_diagnostic_retention(self) -> None:
        self.run_native("aarch64_poll64_host.c")

    def test_mailbox_validation_timeout_and_dma_ownership(self) -> None:
        self.run_native("aarch64_mailbox64_host.c")

    def test_pcie_bar_dma_validation_and_register_rollback(self) -> None:
        self.run_native("aarch64_pcie64_host.c")

    def test_mmu_mapping_failure_and_cache_boundaries(self) -> None:
        self.run_native("aarch64_mmu64_host.c")

    def run_native(self, fixture: str, sources: tuple[str, ...] = (),
                   defines: tuple[str, ...] = ()) -> None:
        with tempfile.TemporaryDirectory(prefix="aarch64-platform64-") as temporary:
            executable = Path(temporary) / "platform64_host"
            subprocess.run(
                [os.environ.get("HOST_CC", "cc"), "-O2", "-std=c11", "-Wall",
                 "-Wextra", "-Werror", "-fsanitize=undefined",
                 "-fno-sanitize-recover=all", "-ffunction-sections", "-fdata-sections",
                 *defines, "-idirafter", str(ROOT / "64bit/src64/include"),
                 str(ROOT / "64bit/tests" / fixture),
                 *(str(ROOT / "64bit/src64" / source) for source in sources),
                 "-Wl,--gc-sections", "-o", str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT, timeout=10)


if __name__ == "__main__":
    unittest.main()
