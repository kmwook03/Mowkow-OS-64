from __future__ import annotations

import contextlib
import errno
import importlib.util
import io
import shutil
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "stage_aarch64_boot", ROOT / "64bit/tools/stage_aarch64_boot.py"
)
assert SPEC is not None and SPEC.loader is not None
STAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STAGER)


class BootStagingTest(unittest.TestCase):
    def test_new_and_existing_tree_publication(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            output = base / "boot"
            source = base / "source"
            source.write_bytes(b"kernel payload")
            untouched = base / "boot.tmp"
            untouched.mkdir()
            (untouched / "keep").write_bytes(b"not ours")
            specs = [f"kernel_2712.img={source}", f"한글.py={source}"]
            for exists in (False, True):
                if exists:
                    (output / "stale").mkdir()
                    (output / "stale/old").write_bytes(b"old payload")
                with contextlib.redirect_stdout(io.StringIO()):
                    STAGER.stage_boot(output, specs)
                self.assertEqual({p.name for p in output.iterdir()},
                                 {"kernel_2712.img", "한글.py"})
                for name in ("kernel_2712.img", "한글.py"):
                    self.assertEqual((output / name).read_bytes(), b"kernel payload")
                self.assertEqual((untouched / "keep").read_bytes(), b"not ours")
                self.assertEqual(set(base.iterdir()), {output, source, untouched})

    def test_copy_validation_and_exchange_failures_preserve_tree(self) -> None:
        copy = shutil.copy2
        phases = ("create", "copy", "interrupt", "corrupt", "truncate", "extra",
                  "missing", "subdir", "fsync", "exchange")
        for phase in phases:
            for exists in (False, True):
                with self.subTest(phase=phase, exists=exists), \
                        tempfile.TemporaryDirectory() as directory:
                    base = Path(directory)
                    output = base / "boot"
                    source = base / "source"
                    source.write_bytes(b"new payload")
                    if exists:
                        output.mkdir()
                        (output / "old").write_bytes(b"original")

                    def copy_payload(src, dst):
                        self.assertEqual(dst.parent.parent.parent, base)
                        if exists:
                            self.assertEqual((output / "old").read_bytes(), b"original")
                        else:
                            self.assertFalse(output.exists())
                        copy(src, dst)
                        if phase == "copy":
                            dst.write_bytes(b"partial")
                            raise OSError(errno.ENOSPC, "copy")
                        if phase == "interrupt":
                            raise KeyboardInterrupt()
                        if phase == "corrupt":
                            dst.write_bytes(b"bad payload")  # same size
                        if phase == "truncate":
                            dst.write_bytes(b"new")
                        if phase == "extra":
                            (dst.parent / "extra").write_bytes(b"unlisted")
                        if phase == "missing":
                            dst.unlink()
                        if phase == "subdir":
                            (dst.parent / "extra").mkdir()

                    with contextlib.ExitStack() as stack:
                        stack.enter_context(patch.object(
                            STAGER.shutil, "copy2", side_effect=copy_payload
                        ))
                        targets = {
                            "create": (STAGER.tempfile, "mkdtemp"),
                            "fsync": (STAGER.os, "fsync"),
                            "exchange": (STAGER, "exchange_directories") if exists else
                                        (STAGER.os, "replace"),
                        }
                        if phase in targets:
                            owner, attribute = targets[phase]
                            stack.enter_context(patch.object(
                                owner, attribute, side_effect=OSError(errno.EIO, phase)
                            ))
                        error = (KeyboardInterrupt if phase == "interrupt" else
                                 ValueError if phase in (
                                     "corrupt", "truncate", "extra", "missing", "subdir"
                                 ) else OSError)
                        with self.assertRaises(error):
                            STAGER.stage_boot(output, [f"kernel_2712.img={source}"])
                    if exists:
                        self.assertEqual({p.name for p in output.iterdir()}, {"old"})
                        self.assertEqual((output / "old").read_bytes(), b"original")
                    else:
                        self.assertFalse(output.exists())
                    self.assertEqual(set(base.iterdir()), {source, output} if exists else {source})

    def test_unsupported_and_failed_exchange_keep_existing_directory(self) -> None:
        def failed_exchange(*args):
            STAGER.ctypes.set_errno(errno.EACCES)
            return -1

        for library, code in ((SimpleNamespace(), errno.ENOSYS),
                              (SimpleNamespace(renameat2=failed_exchange), errno.EACCES)):
            with self.subTest(errno=code), tempfile.TemporaryDirectory() as directory:
                base = Path(directory)
                output = base / "boot"
                output.mkdir()
                (output / "old").write_bytes(b"original")
                source = base / "source"
                source.write_bytes(b"new payload")
                with patch.object(STAGER.ctypes, "CDLL", return_value=library), \
                        self.assertRaises(OSError) as result:
                    STAGER.stage_boot(output, [f"kernel_2712.img={source}"])
                self.assertEqual(result.exception.errno, code)
                self.assertEqual((output / "old").read_bytes(), b"original")
                self.assertEqual(set(base.iterdir()), {source, output})

    def test_invalid_inputs_preserve_existing_files_and_links(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "source"
            source.write_bytes(b"original")
            output = base / "boot"
            for target in (source, output):
                if target == output:
                    output.symlink_to(source)
                with self.assertRaisesRegex(ValueError, "output must be a directory"):
                    STAGER.stage_boot(target, [f"kernel={source}"])
                self.assertEqual(source.read_bytes(), b"original")
            output.unlink()
            output.mkdir()
            (output / "old").write_bytes(b"original")
            for specs in (["bad-spec"], [f"../bad={source}"], ["kernel=missing"],
                          [f"kernel={source}", f"kernel={source}"]):
                with self.subTest(specs=specs), self.assertRaises(ValueError):
                    STAGER.stage_boot(output, specs)
                self.assertEqual((output / "old").read_bytes(), b"original")
                self.assertEqual(set(base.iterdir()), {source, output})

    def test_cleanup_failure_reports_retained_old_tree_after_commit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            output = base / "boot"
            output.mkdir()
            (output / "old").write_bytes(b"original")
            source = base / "source"
            source.write_bytes(b"new payload")
            errors = io.StringIO()
            with patch.object(STAGER.shutil, "rmtree",
                              side_effect=OSError(errno.EACCES, "cleanup")), \
                    contextlib.redirect_stderr(errors), contextlib.redirect_stdout(io.StringIO()):
                STAGER.stage_boot(output, [f"kernel={source}"])
            self.assertIn("temporary cleanup failed", errors.getvalue())
            self.assertEqual((output / "kernel").read_bytes(), b"new payload")
            retained = next(path for path in base.iterdir() if path.name.startswith(".boot."))
            self.assertIn(str(retained), errors.getvalue())
            self.assertEqual((retained / "tree/old").read_bytes(), b"original")


if __name__ == "__main__":
    unittest.main()
