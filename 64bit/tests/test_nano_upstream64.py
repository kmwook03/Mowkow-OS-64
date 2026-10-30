from __future__ import annotations

import importlib.util
import json
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "64bit/nano64"
WARNINGS = ["-Wall", "-Wextra", "-Werror", "-Wformat=2", "-Wshadow", "-Wundef",
            "-Wmissing-prototypes", "-Wstrict-prototypes", "-Wcast-align"]
SPEC = importlib.util.spec_from_file_location(
    "prepare_nano64", ROOT / "64bit/tools/prepare_nano64.py")
assert SPEC is not None and SPEC.loader is not None
PREPARER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREPARER)


class NanoUpstream64Test(unittest.TestCase):
    def test_real_editor_and_private_port_with_sanitizers(self) -> None:
        with tempfile.TemporaryDirectory(prefix="nano-upstream64-") as temporary:
            folder = Path(temporary)
            stage = folder / "vendor"
            PREPARER.prepare(PREPARER.VENDOR, stage, PREPARER.PATCH)
            cc = os.environ.get("HOST_CC", "cc")
            aliases = ("read", "write", "open", "close", "seek", "exit", "puts")
            definitions = [f"-D{name}=fixture_{name}64" for name in aliases]
            definitions += ["-Dmalloc=native_malloc64", "-Dfree=native_free64"]
            flags = ["-O1", "-g", "-std=gnu11", "-fno-builtin", "-fno-pie", "-U__linux__",
                     "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                     "-fno-omit-frame-pointer", *definitions,
                     "-I", str(PORT / "include"), "-I", str(PORT / "libc"), "-I", str(PORT),
                     "-I", str(ROOT / "64bit/app64/crt/include"),
                     "-I", str(ROOT / "64bit/src64/include"), "-I", str(stage / "src")]
            # Obtain the source list from make's shared production inventory.
            result = subprocess.run(["make", "-n", "-B", "nano64-apps"], cwd=ROOT,
                                    text=True, capture_output=True, check=True, timeout=30)
            recipes = result.stdout.replace("\\\n", " ").splitlines()
            commands = [shlex.split(line) for line in recipes
                        if "-c" in shlex.split(line) and "nano-app/x86_64/" in line]
            sources = [ROOT / line[line.index("-c") + 1] for line in commands]
            sources = [stage / "src" / path.name if "nano-upstream" in path.parts else path
                       for path in sources]
            sources += [PORT / "file64.c", PORT / "utf64.c", ROOT / "64bit/src64/lib/utf864.c",
                        ROOT / "64bit/app64/crt/syscall.c",
                        ROOT / "64bit/tests/nano_upstream64_host.c"]
            objects = []
            for index, source in enumerate(sources):
                obj = folder / f"{index}.o"
                vendor = source.is_relative_to(stage)
                warnings = ["-Wall", "-Wextra"] if vendor else WARNINGS
                extra = ["-DHAVE_CONFIG_H"] if vendor else []
                if source.name == "nano.c":
                    extra.append("-Dmain=nano_entry64")
                compiled = subprocess.run(
                    [cc, *flags, *warnings, *extra, "-c", str(source), "-o", str(obj)],
                    cwd=ROOT, capture_output=True, text=True, timeout=30)
                self.assertEqual(compiled.returncode, 0, compiled.stderr[-6000:])
                objects.append(obj)
            executable = folder / "nano-host"
            subprocess.run([cc, "-no-pie", "-fsanitize=address,undefined", *map(str, objects),
                            "-Wl,--wrap=nano_fclose64", "-o", str(executable)],
                           check=True, cwd=ROOT, capture_output=True, text=True, timeout=30)
            subprocess.run([str(executable), "curses"], check=True, cwd=ROOT, timeout=30,
                           env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
            subprocess.run([str(executable)], check=True, cwd=ROOT, timeout=30,
                           env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})

    def test_source_hash_failure_preserves_previous_stage(self) -> None:
        with tempfile.TemporaryDirectory(prefix="nano-pin64-") as temporary:
            folder = Path(temporary)
            vendor, output = folder / "vendor", folder / "output"
            vendor.mkdir()
            output.mkdir()
            (output / "keep").write_text("previous")
            (vendor / "changed.c").write_text("changed")
            (vendor / "source.json").write_text(json.dumps({"files": {"changed.c": "0" * 64}}))
            with self.assertRaisesRegex(ValueError, "hash mismatch"):
                PREPARER.prepare(vendor, output, PREPARER.PATCH)
            self.assertEqual((output / "keep").read_text(), "previous")

    def test_warning_lanes_and_direct_header_audit(self) -> None:
        audit = (PORT / "headers64.c").read_text()
        for path in sorted((PORT / "libc").rglob("*.h")):
            self.assertIn(f'#include "{path.relative_to(PORT)}"', audit)
        result = subprocess.run(["make", "-n", "-B", "nano64-apps"], cwd=ROOT,
                                text=True, capture_output=True, check=True, timeout=30)
        recipes = result.stdout.replace("\\\n", " ").splitlines()
        commands = [shlex.split(line) for line in recipes
                    if "-c" in shlex.split(line) and "nano-app/" in line]
        for architecture in ("x86_64", "aarch64"):
            selected = [line for line in commands
                        if any(f"nano-app/{architecture}/" in part for part in line)]
            self.assertEqual(len(selected), 16)
            for line in selected:
                if "nano-upstream" in line[line.index("-c") + 1]:
                    self.assertNotIn("-Werror", line)
                    self.assertNotIn("-Wno-error", line)
                else:
                    for warning in WARNINGS:
                        self.assertIn(warning, line)
                self.assertIn("-nostdlib", line)
                self.assertIn("-ffreestanding", line)
                self.assertIn("-MMD", line)
                self.assertIn("-MP", line)


if __name__ == "__main__":
    unittest.main()
