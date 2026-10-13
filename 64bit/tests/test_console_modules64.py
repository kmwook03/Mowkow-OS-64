from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ConsoleModules64Test(unittest.TestCase):
    def test_separate_modules_and_public_flows(self) -> None:
        compiler = os.environ.get("HOST_CC", "cc")
        kernel = ROOT / "64bit" / "src64" / "kernel"
        library = ROOT / "64bit" / "src64" / "lib"
        sources = [kernel / name for name in (
            "console64.c", "console_render64.c", "console_editor64.c",
            "console_command64.c", "console_queue64.c",
        )]
        sources += [library / name for name in (
            "fifo64.c", "draw64.c", "hangul64.c", "utf864.c", "keymap64.c",
        )]
        with tempfile.TemporaryDirectory(prefix="console-modules64-") as temporary:
            for architecture in ("x86_64", "aarch64"):
                with self.subTest(architecture=architecture):
                    executable = Path(temporary) / architecture
                    defines = ["-D__aarch64__"] if architecture == "aarch64" else []
                    subprocess.run(
                        [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                         "-fsanitize=undefined", "-fno-sanitize-recover=all",
                         *defines, "-idirafter",
                         str(ROOT / "64bit" / "src64" / "include"),
                         str(ROOT / "64bit" / "tests" / "console_modules64_host.c"),
                         *map(str, sources), "-o", str(executable)],
                        check=True, cwd=ROOT,
                    )
                    subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
