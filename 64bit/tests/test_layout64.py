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

    def test_enforces_kernel_name_limits(self) -> None:
        BUILDER.validate_file_name("a" * BUILDER.FD64_LFN_MAX_UNITS)
        with self.assertRaisesRegex(SystemExit, "UTF-16"):
            BUILDER.validate_file_name(
                "a" * (BUILDER.FD64_LFN_MAX_UNITS + 1)
            )
        with self.assertRaisesRegex(SystemExit, "UTF-8"):
            BUILDER.validate_file_name("가" * 54)

    def test_rejects_vfat_forbidden_names(self) -> None:
        for name in (".", "..", "tail.", "tail ", "bad/name", "bad\x01name"):
            with self.subTest(name=name), self.assertRaises(SystemExit):
                BUILDER.validate_file_name(name)

    def test_preserves_surrogate_pairs_in_lfn_entries(self) -> None:
        # High/low units straddle the first and second 13-unit LFN entries.
        name = "a" * 12 + "😀" + ".txt"
        alias = b"PAIR    TXT"
        entries = BUILDER.lfn_entries(name, alias)
        units = []
        for entry in reversed(entries):
            # Golden checksum for PAIR    TXT, independent of the writer helper.
            self.assertEqual(entry[13], 0xBD)
            for i in range(BUILDER.LFN_UNITS_PER_ENTRY):
                offset = (1 + i * 2 if i < 5 else
                          14 + (i - 5) * 2 if i < 11 else
                          28 + (i - 11) * 2)
                units.append(int.from_bytes(entry[offset:offset + 2], "little"))
        self.assertEqual(units[12:14], [0xD83D, 0xDE00])
        payload = b"".join(unit.to_bytes(2, "little")
                           for unit in units[:units.index(0)])
        self.assertEqual(payload.decode("utf-16-le"), name)

    def test_enforces_non_bmp_unit_limits_and_rejects_surrogates(self) -> None:
        BUILDER.validate_file_name("😀" * 26)
        self.assertEqual(len(BUILDER.lfn_entries("😀" * 26, b"PAIR       ")), 4)
        for name in ("😀" * 27, "a" * 51 + "😀"):
            with self.subTest(name=name), self.assertRaisesRegex(SystemExit, "UTF-16"):
                BUILDER.validate_file_name(name)
        for name in ("\ud800", "\udfff", "\ud800\udc00"):
            with self.subTest(name=repr(name)), self.assertRaisesRegex(SystemExit, "Unicode"):
                BUILDER.validate_file_name(name)
            with self.assertRaises(SystemExit):
                BUILDER.lfn_entries(name, b"PAIR       ")

    def test_rejects_name_and_alias_collisions(self) -> None:
        with self.assertRaisesRegex(SystemExit, "대소문자"):
            BUILDER.validate_files([("Report.txt", b""), ("report.TXT", b"")])
        with self.assertRaisesRegex(SystemExit, "8.3 별칭"):
            BUILDER.validate_files(
                [("longfilename-one.txt", b""),
                 ("longfilename-two.txt", b"")]
            )

    def test_rejects_file_larger_than_fat_entry(self) -> None:
        BUILDER.validate_file_size(BUILDER.FAT_FILE_SIZE_MAX)
        with self.assertRaisesRegex(SystemExit, "4 GiB"):
            BUILDER.validate_file_size(BUILDER.FAT_FILE_SIZE_MAX + 1)


if __name__ == "__main__":
    unittest.main()
