from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "64bit" / "src64"


class Input64Test(unittest.TestCase):
    def test_ps2_and_usb_decoders(self) -> None:
        self.run_native(False)

    def test_usb_and_aarch64_modifiers(self) -> None:
        self.run_native(True)

    def run_native(self, aarch64: bool) -> None:
        compiler = os.environ.get("HOST_CC", "cc")
        sources = [
            ROOT / "64bit" / "tests" / "input64_host.c",
            SOURCE / "lib" / "fifo64.c",
            SOURCE / "arch" / "aarch64" / "usbhid64.c",
        ]
        if aarch64:
            sources.append(SOURCE / "arch" / "aarch64" / "keyboard64.c")
        else:
            sources.extend([
                SOURCE / "drivers" / "keyboard64.c",
                SOURCE / "drivers" / "mouse64.c",
            ])
        with tempfile.TemporaryDirectory(prefix="input64-") as temporary:
            executable = Path(temporary) / "input64_host"
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=undefined", "-fno-sanitize-recover=all",
                 "-idirafter", str(SOURCE / "include")]
                + (["-DINPUT64_AARCH64"] if aarch64 else [])
                + [str(source) for source in sources]
                + ["-o", str(executable)],
                check=True, cwd=ROOT,
            )
            subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
