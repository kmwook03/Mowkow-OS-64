from __future__ import annotations

import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "64bit" / "nano64"
WARNINGS = ["-Wall", "-Wextra", "-Werror", "-Wformat=2", "-Wshadow", "-Wundef",
            "-Wmissing-prototypes", "-Wstrict-prototypes", "-Wcast-align"]


class NanoCompat64Test(unittest.TestCase):
    def test_file_ownership_io_faults_and_utf_boundaries(self) -> None:
        # Native syscall gates are replaced; CRT and port code are unmodified.
        names = ("read", "write", "open", "close", "seek", "exit", "puts")
        defines = [f"-D{name}=fixture_{name}64" for name in names]
        with tempfile.TemporaryDirectory(prefix="nano-compat64-") as temporary:
            executable = Path(temporary) / "nano_compat64_host"
            subprocess.run(
                [os.environ.get("HOST_CC", "cc"), "-O2", "-std=c11", *WARNINGS,
                 "-fno-builtin", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-no-pie",
                 *defines, "-I", str(ROOT / "64bit" / "app64" / "crt" / "include"),
                 "-idirafter", str(ROOT / "64bit" / "src64" / "include"),
                 str(ROOT / "64bit" / "tests" / "nano_compat64_host.c"),
                 str(PORT / "file64.c"), str(PORT / "utf64.c"),
                 str(ROOT / "64bit" / "src64" / "lib" / "utf864.c"),
                 str(ROOT / "64bit" / "app64" / "crt" / "syscall.c"),
                 "-o", str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT, timeout=30,
                           env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})

    def test_headers_and_architecture_recipes(self) -> None:
        audit = (PORT / "headers64.c").read_text()
        for header in (PORT / "include").glob("*.h"):
            self.assertIn(f'#include "include/{header.name}"', audit)
        result = subprocess.run(["make", "-n", "-B", "nano64-compat"], cwd=ROOT,
                                text=True, capture_output=True, check=True, timeout=30)
        # Join make's continued recipe lines before examining compiler flags.
        recipes = result.stdout.replace("\\\n", " ").splitlines()
        commands = [shlex.split(line) for line in recipes if " -c " in line]
        self.assertEqual(len(commands), 8)
        for architecture in ("x86_64", "aarch64"):
            selected = [line for line in commands
                        if any(f"nano-compat/{architecture}/" in arg for arg in line)]
            self.assertEqual(len(selected), 4)
            self.assertEqual({line[line.index("-c") + 1] for line in selected},
                             {"64bit/nano64/file64.c", "64bit/nano64/utf64.c",
                              "64bit/nano64/headers64.c", "64bit/src64/lib/utf864.c"})
            for line in selected:
                for flag in (*WARNINGS, "-ffreestanding", "-nostdlib", "-MMD", "-MP"):
                    self.assertIn(flag, line)
                self.assertFalse(any(flag.startswith("-Wno-") for flag in line))


if __name__ == "__main__":
    unittest.main()
