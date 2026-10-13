from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class MpportFile64Test(unittest.TestCase):
    def test_exact_reads_and_nlr_ownership(self) -> None:
        # Use upstream reader/NLR code without generated headers or a built VM.
        with tempfile.TemporaryDirectory(prefix="mpport-file64-") as temporary:
            folder = Path(temporary)
            headers = folder / "py"
            headers.mkdir()
            mock = ROOT / "64bit" / "tests" / "mpport_file64_mock.h"
            for name in ("runtime", "mpstate", "obj", "lexer", "mperrno", "mpthread"):
                (headers / f"{name}.h").write_text(f'#include "{mock}"\n')
            for backend in ("setjmp", "native"):
                with self.subTest(backend=backend):
                    (headers / "mpconfig.h").write_text(
                        "#define MP_NORETURN _Noreturn\n"
                        "#define MP_UNREACHABLE __builtin_unreachable();\n"
                        f"#define MICROPY_NLR_SETJMP {int(backend == 'setjmp')}\n"
                        "#define MICROPY_ENABLE_PYSTACK 0\n"
                        "#define MICROPY_ENABLE_VM_ABORT 0\n"
                        "#define MICROPY_READER_POSIX 0\n"
                        "#define MICROPY_READER_VFS 0\n"
                    )
                    sanitizers = "address,undefined" if backend == "setjmp" else "undefined"
                    executable = folder / backend
                    vendor = ROOT / "third_party" / "micropython"
                    compiler = os.environ.get("HOST_CC", "cc")
                    flags = ["-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
                             "-fno-inline", "-no-pie", f"-fsanitize={sanitizers}",
                             "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
                             "-I", str(folder), "-I", str(vendor), "-idirafter",
                             str(ROOT / "64bit" / "src64" / "include")]
                    sources = [
                        ROOT / "64bit" / "tests" / "mpport_file64_host.c",
                        ROOT / "64bit" / "src64" / "mpport" / "mpport_file64.c",
                        ROOT / "64bit" / "src64" / "lib" / "utf864.c",
                        vendor / "py" / "reader.c", vendor / "py" / "nlr.c",
                        vendor / "py" / ("nlrsetjmp.c" if backend == "setjmp"
                                         else "nlrx64.c"),
                    ]
                    objects = []
                    for index, source in enumerate(sources):
                        obj = folder / f"{backend}-{index}.o"
                        # The upstream naked gate consumes its argument in asm.
                        extra = ["-Wno-unused-parameter"] if source.name == "nlrx64.c" else []
                        subprocess.run(
                            [compiler, *flags, *extra, "-c", str(source), "-o", str(obj)],
                            check=True, cwd=ROOT,
                        )
                        objects.append(str(obj))
                    subprocess.run(
                        [compiler, *flags, *objects, "-o", str(executable)],
                        check=True, cwd=ROOT,
                    )
                    subprocess.run(
                        [str(executable)], check=True, cwd=ROOT, timeout=30,
                        env={**os.environ, "ASAN_OPTIONS":
                             "detect_leaks=0:detect_stack_use_after_return=0"},
                    )


if __name__ == "__main__":
    unittest.main()
