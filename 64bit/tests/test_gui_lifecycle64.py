from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def run_fixture64(fixture_name: str, extra_sources: tuple[str, ...] = ()) -> None:
    compiler = os.environ.get("HOST_CC", "cc")
    source = ROOT / "64bit" / "src64"
    flags = ["-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=undefined", "-fno-sanitize-recover=all",
             "-ffunction-sections", "-fdata-sections",
             "-idirafter", str(source / "include")]
    with tempfile.TemporaryDirectory(prefix="gui-lifecycle64-") as temporary:
        sheet = Path(temporary) / "sheet.o"
        fifo = Path(temporary) / "fifo.o"
        executable = Path(temporary) / "gui_lifecycle64"
        subprocess.run(
            [compiler, *flags, "-Dsheet64_alloc=fixture_sheet_alloc64",
             "-Dsheet64_setbuf=fixture_sheet_setbuf64", "-c",
             str(source / "kernel" / "sheet64.c"), "-o", str(sheet)],
            check=True, cwd=ROOT,
        )
        subprocess.run(
            [compiler, *flags, "-Dfifo64_init=fixture_fifo_init64", "-c",
             str(source / "lib" / "fifo64.c"), "-o", str(fifo)],
            check=True, cwd=ROOT,
        )
        subprocess.run(
            [compiler, *flags, str(ROOT / "64bit" / "tests" / fixture_name),
             str(source / "kernel" / "console_render64.c"),
             str(source / "kernel" / "console_queue64.c"),
             str(source / "kernel" / "window64.c"),
             str(source / "kernel" / "framebuffer64.c"),
             str(source / "lib" / "draw64.c"), str(source / "lib" / "hangul64.c"),
             str(source / "lib" / "utf864.c"),
             *[str(source / name) for name in extra_sources], str(sheet), str(fifo),
             "-Wl,--gc-sections", "-o", str(executable)],
            check=True, cwd=ROOT,
        )
        subprocess.run([str(executable)], check=True, cwd=ROOT)


class GuiLifecycle64Test(unittest.TestCase):
    def test_fault_injection_and_ownership(self) -> None:
        run_fixture64("gui_lifecycle64_host.c")


if __name__ == "__main__":
    unittest.main()
