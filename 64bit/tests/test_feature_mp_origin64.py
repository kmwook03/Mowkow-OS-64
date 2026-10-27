from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROBES = ROOT / "64bit/tools/p0"


class FeatureMpOrigin64Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.directory = tempfile.TemporaryDirectory(prefix="mowkow-origin-")
        cls.executable = Path(cls.directory.name) / "probe"
        subprocess.run([
            os.environ.get("HOST_CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra",
            "-Werror", "-Wformat=2", "-Wshadow", "-Wundef", "-Wstrict-prototypes",
            "-Wmissing-prototypes", "-Wcast-align", str(PROBES / "mp_origin_store64.c"),
            str(PROBES / "mp_origin_store_probe64.c"), "-o", str(cls.executable),
        ], check=True, capture_output=True, timeout=30)
        cls.addClassCleanup(cls.directory.cleanup)

    def test_all_heap_addresses_and_emergency_keep_full_width_values(self) -> None:
        result = subprocess.run([str(self.executable), "full"], check=True,
                                capture_output=True, text=True, timeout=30)
        self.assertIn("65536 live records + emergency, no eviction", result.stdout)
        self.assertIn("metadata=589889", result.stdout)

    def test_invalid_boundaries_preserve_configuration_and_output(self) -> None:
        result = subprocess.run([str(self.executable), "invalid"], check=True,
                                capture_output=True, text=True, timeout=30)
        self.assertIn("preserve store and output", result.stdout)


if __name__ == "__main__":
    unittest.main()
