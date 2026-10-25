from __future__ import annotations

import contextlib
import errno
import importlib.util
import io
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


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
        names = ["", ".", "..", "tail.", "tail "]
        names += [f"bad{chr(c)}name" for c in range(0x20)]
        names += [f"bad{c}name" for c in '"*/:<>?\\|']
        for name in names:
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
        for second in ("Report.txt", "report.TXT"):
            with self.subTest(second=second), self.assertRaisesRegex(SystemExit, "대소문자"):
                BUILDER.validate_files([("Report.txt", b""), (second, b"")])
        pairs = [
            ("longfilename-one.txt", "longfilename-two.txt"),
            ("longfilename.txt", "LONGFI~1.TXT"),
            # The second LFN has a different alias but names the first alias.
            ("MiX.txt", "mix   ~1.txt"),
        ]
        for pair in pairs:
            for names in (pair, pair[::-1]):
                with self.subTest(names=names), self.assertRaisesRegex(SystemExit, "8.3 별칭"):
                    BUILDER.validate_files([(name, b"") for name in names])

    def test_keeps_kernel_ascii_case_policy(self) -> None:
        # Unicode casefold would incorrectly merge these distinct kernel names.
        self.assertEqual(BUILDER.validate_files([("İ.TXT", b""), ("i\u0307.TXT", b"")]), 1)

    def test_counts_root_entries_including_lfn_and_label(self) -> None:
        short_files = [(f"F{i:02}.TXT", b"") for i in range(16)]
        self.assertEqual(BUILDER.validate_files(short_files[:15]), 1)
        self.assertEqual(BUILDER.validate_files(short_files), 2)
        # Each 52-unit name needs four LFN entries and one short entry.
        long_files = [(f"N{i:05}" + "a" * 46, b"") for i in range(4)]
        self.assertEqual(BUILDER.validate_files(long_files[:3]), 1)
        self.assertEqual(BUILDER.validate_files(long_files), 2)
        with patch.object(BUILDER, "CLUSTER_COUNT", 1):
            with self.assertRaisesRegex(SystemExit, "root directory exceeds"):
                BUILDER.validate_files(long_files)

    def test_preflights_total_capacity_and_cluster_rounding(self) -> None:
        with patch.object(BUILDER, "CLUSTER_COUNT", 4):
            # One root + one empty-file + two data clusters exactly fills it.
            for size in (513, 1024):
                self.assertEqual(
                    BUILDER.validate_files([("EMPTY", b""), ("DATA", b"x" * size)]), 1
                )
            with self.assertRaisesRegex(SystemExit, "files and root directory exceed"):
                BUILDER.validate_files([("EMPTY", b""), ("DATA", b"x" * 1025)])
        with patch.object(BUILDER, "CLUSTER_COUNT", 18):
            files = [(f"F{i:02}", b"") for i in range(17)]
            self.assertEqual(BUILDER.validate_files(files[:16]), 2)
            with self.assertRaisesRegex(SystemExit, "files and root directory exceed"):
                BUILDER.validate_files(files)

    def test_build_rejects_before_image_allocation_or_output_changes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            payload = base / "payload"
            payload.write_bytes(b"x" * 513)
            cases = [
                ([f"H04.FNT={payload}"], BUILDER.CLUSTER_COUNT, "대소문자"),
                ([f"readme.txt={payload}"], BUILDER.CLUSTER_COUNT, "대소문자"),
                ([f"bad/name={payload}"], BUILDER.CLUSTER_COUNT, "VFAT"),
                ([f"bad\udcff={payload}"], BUILDER.CLUSTER_COUNT, "UTF-8/Unicode"),
                ([f"longfilename-one.txt={payload}",
                  f"longfilename-two.txt={payload}"], BUILDER.CLUSTER_COUNT, "8.3 별칭"),
                ([f"DATA={payload}"], 4, "files and root directory exceed"),
                ([f"F{i:02}={payload}" for i in range(14)], 1, "root directory exceeds"),
                (["bad-spec"], BUILDER.CLUSTER_COUNT, "bad app spec"),
            ]
            for specs, clusters, message in cases:
                for exists in (False, True):
                    with self.subTest(specs=repr(specs), exists=exists):
                        output = base / "image"
                        if exists:
                            output.write_bytes(b"original image")
                        with patch.object(BUILDER, "CLUSTER_COUNT", clusters), \
                                patch.object(BUILDER, "Volume") as volume, \
                                self.assertRaisesRegex(SystemExit, message):
                            BUILDER.build(output, bytes(512), b"", b"", b"", specs)
                        volume.assert_not_called()
                        if exists:
                            self.assertEqual(output.read_bytes(), b"original image")
                            output.unlink()
                        else:
                            self.assertFalse(output.exists())

    @unittest.skipUnless(os.name == "posix", "invalid UTF-8 argv uses POSIX bytes")
    def test_cli_rejects_invalid_utf8_bytes_without_touching_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            boot = base / "boot"
            payload = base / "payload"
            output = base / "image"
            boot.write_bytes(bytes(512))
            payload.write_bytes(b"")
            output.write_bytes(b"original image")
            result = subprocess.run(
                [os.fsencode(sys.executable), os.fsencode(BUILDER_PATH),
                 os.fsencode(output), os.fsencode(boot),
                 *[os.fsencode(payload)] * 3, b"bad\xff=" + os.fsencode(payload)],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
                timeout=10, env={**os.environ, "PYTHONUTF8": "1"},
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("UTF-8/Unicode".encode(), result.stderr)
            self.assertNotIn(b"Traceback", result.stderr)
            self.assertEqual(output.read_bytes(), b"original image")

    def test_build_writes_planned_root_and_file_chains(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            payload = base / "payload"
            output = base / "image"
            payload.write_bytes(b"x" * 513)
            # 16 short entries plus the label occupy two root clusters.
            specs = [f"F{i:02}.TXT={payload}" for i in range(14)]
            BUILDER.build(output, bytes(512), b"loader", b"kernel", b"", specs)
            image = output.read_bytes()

        self.assertEqual(len(image), 64 * 1024 * 1024)
        fat = image[1024 * 512:2025 * 512]
        self.assertEqual(fat, image[2025 * 512:3026 * 512])
        # Root = 2->3, empty H04.FNT = 4, README.TXT = 5, F00.TXT = 6->7.
        self.assertEqual(struct.unpack_from("<6I", fat, 2 * 4),
                         (3, 0x0FFFFFFF, 0x0FFFFFFF, 0x0FFFFFFF, 7, 0x0FFFFFFF))
        root = image[3026 * 512:3028 * 512]
        self.assertEqual(root[:11], b"MOWKOW64   ")
        for index, name, cluster, size in (
                (1, b"H04     FNT", 4, 0),
                (3, b"F00     TXT", 6, 513),
                (16, b"F13     TXT", 32, 513)):
            entry = root[index * 32:(index + 1) * 32]
            self.assertEqual(entry[:11], name)
            self.assertEqual(struct.unpack_from("<H", entry, 26)[0], cluster)
            self.assertEqual(struct.unpack_from("<I", entry, 28)[0], size)
        self.assertEqual(root[17 * 32:], bytes(1024 - 17 * 32))
        self.assertEqual(image[3030 * 512:3030 * 512 + 513], b"x" * 513)

    def test_rejects_file_larger_than_fat_entry(self) -> None:
        BUILDER.validate_file_size(BUILDER.FAT_FILE_SIZE_MAX)
        for size in (-1, BUILDER.FAT_FILE_SIZE_MAX + 1):
            with self.subTest(size=size), self.assertRaisesRegex(SystemExit, "4 GiB"):
                BUILDER.validate_file_size(size)

    def test_image_publication_preserves_old_file_until_commit(self) -> None:
        replace = os.replace
        image = bytearray(b"x" * 512)
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            output = base / "image.img"
            for exists in (False, True):
                if exists:
                    output.write_bytes(b"old image")
                    output.chmod(0o640)

                def commit(source: Path, target: Path) -> None:
                    self.assertEqual(source.parent.parent, base)
                    self.assertEqual(source.read_bytes(), image)
                    if exists:
                        self.assertEqual(output.read_bytes(), b"old image")
                    else:
                        self.assertFalse(output.exists())
                    replace(source, target)

                with patch.object(BUILDER, "TOTAL_SECTORS", 1), \
                        patch.object(BUILDER.os, "replace", side_effect=commit) as publish:
                    BUILDER.publish_image(output, image)
                publish.assert_called_once()
                self.assertEqual(output.read_bytes(), image)
                self.assertEqual(set(base.iterdir()), {output})
                if exists:
                    self.assertEqual(output.stat().st_mode & 0o777, 0o640)
                output.unlink()

    def test_image_io_failures_preserve_output_and_remove_temporaries(self) -> None:
        original_open = Path.open

        class FaultStream:
            def __init__(self, stream, phase: str):
                self.stream = stream
                self.phase = phase

            def __enter__(self):
                return self

            def __exit__(self, *args):
                self.stream.__exit__(*args)
                if self.phase == "close":
                    raise OSError(errno.EIO, "injected close")

            def write(self, data):
                if self.phase in ("write", "short", "interrupt"):
                    self.stream.write(data[:17])
                    if self.phase == "short":
                        return 17
                    if self.phase == "interrupt":
                        raise KeyboardInterrupt()
                    raise OSError(errno.ENOSPC, "injected partial write")
                return self.stream.write(data)

            def flush(self):
                self.stream.flush()
                if self.phase == "flush":
                    raise OSError(errno.EIO, "injected flush")

            def fileno(self):
                return self.stream.fileno()

        phases = ("create", "open", "write", "short", "flush", "fsync", "close",
                  "validate", "chmod", "replace", "interrupt")
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            output = base / "image.img"
            for phase in phases:
                for exists in (False, True):
                    if phase == "chmod" and not exists:
                        continue
                    with self.subTest(phase=phase, exists=exists):
                        if exists:
                            output.write_bytes(b"old image")

                        def open_stream(path, mode="r", *args, **kwargs):
                            if mode == "xb":
                                if phase == "open":
                                    raise OSError(errno.EIO, "injected open")
                                return FaultStream(original_open(path, mode), phase)
                            return original_open(path, mode, *args, **kwargs)

                        with contextlib.ExitStack() as stack:
                            stack.enter_context(patch.object(BUILDER, "TOTAL_SECTORS", 1))
                            stack.enter_context(patch.object(Path, "open", open_stream))
                            targets = {
                                "create": (BUILDER.tempfile, "mkdtemp"),
                                "fsync": (BUILDER.os, "fsync"),
                                "validate": (BUILDER, "validate_image"),
                                "chmod": (Path, "chmod"),
                                "replace": (BUILDER.os, "replace"),
                            }
                            if phase in targets:
                                owner, attribute = targets[phase]
                                stack.enter_context(patch.object(
                                    owner, attribute, side_effect=OSError(errno.EIO, phase)
                                ))
                            expected = KeyboardInterrupt if phase == "interrupt" else OSError
                            with self.assertRaises(expected):
                                BUILDER.publish_image(output, bytearray(b"x" * 512))
                        if exists:
                            self.assertEqual(output.read_bytes(), b"old image")
                            output.unlink()
                        else:
                            self.assertFalse(output.exists())
                        self.assertEqual(list(base.iterdir()), [])

    def test_image_readback_detects_corruption_before_commit(self) -> None:
        validate = BUILDER.validate_image
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            output = base / "image.img"
            output.write_bytes(b"old image")
            for damaged in (b"x" * 511, b"x" * 513, b"y" + b"x" * 511):
                def corrupt(path, expected):
                    path.write_bytes(damaged)
                    validate(path, expected)

                with self.subTest(size=len(damaged)), \
                        patch.object(BUILDER, "TOTAL_SECTORS", 1), \
                        patch.object(BUILDER, "validate_image", side_effect=corrupt), \
                        self.assertRaisesRegex(ValueError, "image .* mismatch"):
                    BUILDER.publish_image(output, bytearray(b"x" * 512))
                self.assertEqual(output.read_bytes(), b"old image")
                self.assertEqual(set(base.iterdir()), {output})

    def test_image_cleanup_error_does_not_mask_commit_or_original_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            output = base / "image.img"
            for commit in (False, True):
                output.write_bytes(b"old image")
                errors = io.StringIO()
                with contextlib.ExitStack() as stack:
                    stack.enter_context(patch.object(BUILDER, "TOTAL_SECTORS", 1))
                    stack.enter_context(contextlib.redirect_stderr(errors))
                    stack.enter_context(patch.object(
                        BUILDER.shutil, "rmtree", side_effect=OSError(errno.EACCES, "cleanup")
                    ))
                    if not commit:
                        stack.enter_context(patch.object(
                            BUILDER.os, "replace", side_effect=OSError(errno.ENOSPC, "commit")
                        ))
                        with self.assertRaises(OSError) as result:
                            BUILDER.publish_image(output, bytearray(b"x" * 512))
                        self.assertEqual(result.exception.errno, errno.ENOSPC)
                    else:
                        BUILDER.publish_image(output, bytearray(b"x" * 512))
                self.assertIn("temporary cleanup failed", errors.getvalue())
                self.assertEqual(output.read_bytes(), b"x" * 512 if commit else b"old image")


if __name__ == "__main__":
    unittest.main()
