"""Exercise independent damaged images through the real storage/ELF stack."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from storage_fixture64 import LONG_NAME, SHORT_NAME, elf_fixture, volume_fixture


ROOT = Path(__file__).resolve().parents[2]


class StorageFault64Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory(prefix="storage-fault64-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        cls.executable = cls.directory / "storage_fault64_host"
        subprocess.run(
            [os.environ.get("HOST_CC", "cc"), "-std=c11", "-Wall", "-Wextra",
             "-Werror", "-idirafter", str(ROOT / "64bit/src64/include"),
             str(ROOT / "64bit/tests/storage_fault64_host.c"),
             str(ROOT / "64bit/src64/kernel/memory64.c"),
             str(ROOT / "64bit/src64/lib/utf864.c"), "-o", str(cls.executable)],
            check=True, cwd=ROOT, timeout=30,
        )

    def run_image(self, mode: str, argument: str = "", **mutations: str | bytes) -> None:
        image = self.directory / "fixture.img"
        volume_fixture(image, **mutations)
        result = subprocess.run(
            [str(self.executable), str(image), mode, argument], cwd=ROOT,
            capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_valid_volume_and_name(self) -> None:
        self.run_image("read")
        self.run_image("name", LONG_NAME)
        self.run_image("elf-valid", payload=elf_fixture())

    def test_bpb_corruption(self) -> None:
        for corruption in ("signature", "sector-size", "cluster-size", "reserved",
                           "fat-capacity", "geometry-overflow", "device-size", "root-range"):
            with self.subTest(corruption=corruption):
                self.run_image("mount-reject", bpb=corruption)

    def test_fat_corruption(self) -> None:
        for owner in ("root", "file"):
            for corruption in ("free", "reserved", "bad", "range", "cycle"):
                with self.subTest(owner=owner, corruption=corruption):
                    mode = "mount-reject" if owner == "root" else "open-reject"
                    self.run_image(mode, fat=f"{owner}-{corruption}")
        self.run_image("open-reject", fat="file-short")

    def test_malformed_vfat_falls_back_to_alias(self) -> None:
        for corruption in ("checksum", "ordinal", "missing-last", "oversized",
                           "zero-ordinal", "surrogate", "deleted"):
            with self.subTest(corruption=corruption):
                self.run_image("name", SHORT_NAME, vfat=corruption)

    def test_failed_partial_reads_retry_without_exposing_prefix(self) -> None:
        self.run_image("mount-read-fail", "0")
        self.run_image("mount-read-fail", "32")
        self.run_image("directory-read-fail")
        self.run_image("partial-read")

    def test_writeback_failure_and_ordered_retry(self) -> None:
        for target in (1096, 1104, 1112, 32, 560, 1088):
            for persistent in (False, True):
                with self.subTest(target=target, persistent=persistent):
                    mode = "sync-persistent" if persistent else "sync-once"
                    self.run_image(mode, str(target))

    def test_malformed_elf_releases_memory_and_owner(self) -> None:
        failures = {
            -4: ("magic", "machine", "phoff-overflow", "truncated-header", "truncated-table"),
            -5: ("offset-overflow", "address-overflow", "filesz", "overlap", "alignment", "flags"),
            -6: ("entry", "no-load"),
        }
        for expected, corruptions in failures.items():
            for corruption in corruptions:
                with self.subTest(corruption=corruption):
                    self.run_image("elf", str(expected), payload=elf_fixture(corruption))

    def test_elf_short_io_releases_memory_and_owner(self) -> None:
        self.run_image("elf-read-fail", payload=elf_fixture())


if __name__ == "__main__":
    unittest.main()
