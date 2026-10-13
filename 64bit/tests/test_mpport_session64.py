from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class MpportSession64Test(unittest.TestCase):
    def test_claim_cleanup_and_init_failures(self) -> None:
        with tempfile.TemporaryDirectory(prefix="mpport-session64-") as temporary:
            folder = Path(temporary)
            headers = folder / "py"
            headers.mkdir()
            runtime = folder / "shared" / "runtime"
            runtime.mkdir(parents=True)
            mock = ROOT / "64bit" / "tests" / "mpport_session64_mock.h"
            for name in ("builtin", "compile", "cstack", "gc", "lexer", "mpstate",
                         "parse", "runtime"):
                (headers / f"{name}.h").write_text(f'#include "{mock}"\n')
            for name in ("gchelper", "pyexec"):
                (runtime / f"{name}.h").write_text(f'#include "{mock}"\n')
            vendor = ROOT / "third_party" / "micropython"
            compiler = os.environ.get("HOST_CC", "cc")
            for architecture in ("x86_64", "aarch64"):
                for backend in ("setjmp", "native"):
                    with self.subTest(architecture=architecture, backend=backend):
                        (headers / "mpconfig.h").write_text(
                            "#define MP_NORETURN _Noreturn\n"
                            "#define MP_UNREACHABLE __builtin_unreachable();\n"
                            f"#define MICROPY_NLR_SETJMP {int(backend == 'setjmp')}\n"
                            "#define MICROPY_ENABLE_PYSTACK 0\n"
                            "#define MICROPY_ENABLE_VM_ABORT 0\n"
                            "#define MICROPY_STACK_CHECK_MARGIN 8192\n"
                        )
                        defines = ["-D__aarch64__"] if architecture == "aarch64" else []
                        sanitizer = "address,undefined" if backend == "setjmp" else "undefined"
                        flags = ["-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                 "-no-pie", f"-fsanitize={sanitizer}",
                                 "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
                                 *defines, "-I", str(folder), "-I", str(vendor),
                                 "-idirafter", str(ROOT / "64bit" / "src64" / "include")]
                        sources = [ROOT / "64bit" / "tests" / "mpport_session64_host.c",
                                   vendor / "py" / "nlr.c", vendor / "py" /
                                   ("nlrsetjmp.c" if backend == "setjmp" else "nlrx64.c")]
                        objects = []
                        for index, source in enumerate(sources):
                            obj = folder / f"{architecture}-{backend}-{index}.o"
                            extra = ["-Wno-unused-parameter"] if source.name == "nlrx64.c" else []
                            subprocess.run(
                                [compiler, *flags, *extra, "-c", str(source), "-o", str(obj)],
                                check=True, cwd=ROOT,
                            )
                            objects.append(str(obj))
                        executable = folder / f"{architecture}-{backend}"
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
