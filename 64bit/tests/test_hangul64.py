from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Hangul64Test(unittest.TestCase):
    def test_native_automaton_and_console_modes(self) -> None:
        self.run_native("hangul64_host.c", "keymap64.c", "lib")

    def test_native_state_font_and_clip_bounds(self) -> None:
        self.run_native("hangul64_bounds_host.c", "window64.c", "kernel")

    def run_native(self, fixture: str, source: str, directory: str) -> None:
        compiler = os.environ.get("HOST_CC", "cc")
        with tempfile.TemporaryDirectory(prefix="hangul64-") as temporary:
            executable = Path(temporary) / "hangul64_host"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=undefined",
                    "-fno-sanitize-recover=all",
                    "-ffunction-sections",
                    "-fdata-sections",
                    "-idirafter",
                    str(ROOT / "64bit" / "src64" / "include"),
                    str(ROOT / "64bit" / "tests" / fixture),
                    str(ROOT / "64bit" / "src64" / "lib" / "hangul64.c"),
                    str(ROOT / "64bit" / "src64" / "lib" / "draw64.c"),
                    str(ROOT / "64bit" / "src64" / "lib" / "utf864.c"),
                    str(ROOT / "64bit" / "src64" / directory / source),
                    "-Wl,--gc-sections",
                    "-o",
                    str(executable),
                ],
                check=True,
                cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
