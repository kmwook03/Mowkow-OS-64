from __future__ import annotations

import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class Syscall64Test(unittest.TestCase):
    def test_native_dispatch_and_input_validation(self) -> None:
        kernel = ROOT / "64bit/src64/include/syscall64.h"
        crt = ROOT / "64bit/app64/crt/include/syscall.h"
        pattern = r"^#define (TTY_\w+)\s+(\d+)\b"
        kernel_ops = dict(re.findall(pattern, kernel.read_text(), re.MULTILINE))
        crt_ops = dict(re.findall(pattern, crt.read_text(), re.MULTILINE))
        for name, value in crt_ops.items():
            self.assertEqual(kernel_ops[name], value, name)
        compiler = os.environ.get("HOST_CC", "cc")
        with tempfile.TemporaryDirectory(prefix="syscall64-") as temporary:
            for architecture in ("x86_64", "aarch64"):
                with self.subTest(architecture=architecture):
                    executable = Path(temporary) / f"syscall64_{architecture}"
                    defines = ["-D__aarch64__"] if architecture == "aarch64" else []
                    subprocess.run(
                        [
                            compiler,
                            "-std=c11",
                            "-Wall",
                            "-Wextra",
                            "-Werror",
                            *defines,
                            f"-I{ROOT / '64bit' / 'src64' / 'include'}",
                            str(ROOT / "64bit" / "tests" / "syscall64_host.c"),
                            str(ROOT / "64bit" / "src64" / "kernel" / "syscall64.c"),
                            str(ROOT / "64bit" / "src64" / "lib" / "utf864.c"),
                            "-o",
                            str(executable),
                        ],
                        check=True,
                        cwd=ROOT,
                    )
                    subprocess.run([str(executable)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
