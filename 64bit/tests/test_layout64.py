from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BUILDER_PATH = ROOT / "64bit" / "tools" / "mkfat32_64.py"
SPEC = importlib.util.spec_from_file_location("mkfat32_64", BUILDER_PATH)
assert SPEC is not None and SPEC.loader is not None
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


class Fat32LayoutTest(unittest.TestCase):
    def test_accepts_repository_layout(self) -> None:
        BUILDER.validate_layout()

    def test_rejects_stage2_kernel_overlap(self) -> None:
        with self.assertRaisesRegex(ValueError, "stage 2 overlaps the kernel"):
            BUILDER.validate_layout(kernel_lba=BUILDER.STAGE2_LBA + 1)

    def test_rejects_kernel_outside_reserved_area(self) -> None:
        with self.assertRaisesRegex(ValueError, "kernel lies outside"):
            BUILDER.validate_layout(kernel_lba=BUILDER.RESERVED_SECTORS)

    def test_rejects_undersized_fat(self) -> None:
        with self.assertRaisesRegex(ValueError, "FAT is too small"):
            BUILDER.validate_layout(sectors_per_fat=1)


if __name__ == "__main__":
    unittest.main()
