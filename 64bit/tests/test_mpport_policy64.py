from __future__ import annotations

import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "64bit" / "src64" / "mpport"
AUDIT = PORT / "mpport_headers64.c"
REQUIRED_WARNINGS = {
    "-Wall", "-Wextra", "-Wformat=2", "-Wshadow", "-Wundef", "-Wstrict-prototypes",
    "-Wmissing-prototypes", "-Wcast-align", "-Werror",
}


def make_policy() -> dict[str, list[str]]:
    names = ("WARN64_CFLAGS", "MPY_PORT_CFLAGS", "A64_MPY_PORT_CFLAGS",
             "MPY_VENDOR_CFLAGS", "A64_MPY_VENDOR_CFLAGS", "MPY_PORT_SRCS",
             "A64_MPY_PORT_SRCS", "MPY_PORT_OBJS", "A64_MPY_PORT_OBJS")
    source = "include Makefile\n"
    source += "\n".join(f"$(info AUDIT64_{name}=$({name}))" for name in names)
    source += "\n.PHONY: audit64-policy\naudit64-policy:\n\t@:\n"
    result = subprocess.run(
        ["make", "--no-print-directory", "-s", "-f", "-", "audit64-policy"],
        input=source, text=True, capture_output=True, check=True, cwd=ROOT, timeout=30,
    )
    return {line.split("=", 1)[0].removeprefix("AUDIT64_"): shlex.split(value)
            for line in result.stdout.splitlines() if line.startswith("AUDIT64_")
            for value in [line.split("=", 1)[1]]}


class MpportPolicy64Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.policy = make_policy()

    def test_warning_lanes_and_source_coverage(self) -> None:
        for name in ("MPY_PORT_CFLAGS", "A64_MPY_PORT_CFLAGS"):
            with self.subTest(name=name):
                flags = self.policy[name]
                self.assertTrue(REQUIRED_WARNINGS <= set(flags))
                self.assertFalse(any(flag.startswith("-Wno-") for flag in flags))
                for index, flag in enumerate(flags[:-1]):
                    if flag == "-isystem":
                        self.assertNotIn("src64", flags[index + 1])
        for name in ("MPY_VENDOR_CFLAGS", "A64_MPY_VENDOR_CFLAGS"):
            self.assertNotIn("-Werror", self.policy[name])
            self.assertNotIn("-Wstrict-prototypes", self.policy[name])
        sources = {str(path.relative_to(ROOT)) for path in PORT.rglob("*.c")}
        self.assertEqual(sources, set(self.policy["MPY_PORT_SRCS"]))
        self.assertEqual(sources - {"64bit/src64/mpport/libc/math.c"},
                         set(self.policy["A64_MPY_PORT_SRCS"]))
        for source_name, object_name in (("MPY_PORT_SRCS", "MPY_PORT_OBJS"),
                                         ("A64_MPY_PORT_SRCS", "A64_MPY_PORT_OBJS")):
            self.assertEqual(len(self.policy[source_name]), len(self.policy[object_name]))
            self.assertTrue(any(path.endswith("/mpport_headers64.o")
                                for path in self.policy[object_name]))

    def test_recipes_use_the_port_and_vendor_flags(self) -> None:
        targets = self.policy["MPY_PORT_OBJS"] + self.policy["A64_MPY_PORT_OBJS"]
        targets += ["build64/upy/obj.o", "build64/aarch64-upy/obj.o",
                    "build64/upy/shared/runtime/pyexec.o",
                    "build64/aarch64-upy/shared/runtime/pyexec.o"]
        result = subprocess.run(
            ["make", "--no-print-directory", "-n", "-B", *targets],
            text=True, capture_output=True, check=True, cwd=ROOT, timeout=30,
        )
        seen = set()
        for line in result.stdout.replace("\\\n", " ").splitlines():
            tokens = shlex.split(line)
            if "-c" not in tokens or "-o" not in tokens:
                continue
            output = tokens[tokens.index("-o") + 1]
            if output not in targets:
                continue
            source = tokens[tokens.index("-c") + 1]
            if source.startswith("64bit/src64/mpport/"):
                self.assertTrue(REQUIRED_WARNINGS <= set(tokens), line)
                self.assertFalse(any(flag.startswith("-Wno-") for flag in tokens), line)
            else:
                self.assertTrue(source.startswith("third_party/micropython/"), line)
                self.assertNotIn("-Werror", tokens, line)
                self.assertNotIn("-Wstrict-prototypes", tokens, line)
            seen.add(output)
        self.assertEqual(set(targets), seen)

    def test_direct_header_and_style_coverage(self) -> None:
        headers = {path.relative_to(PORT).as_posix() for path in PORT.rglob("*.h")}
        source = AUDIT.read_text()
        includes = set(re.findall(r'^#include "([^"]+)"', source, re.MULTILINE))
        self.assertEqual(headers, includes - {"py/mpconfig.h"})
        seed = source.index('#include "py/mpconfig.h"')
        for header in headers:
            if header.startswith("libc/") or header == "mpconfigport.h":
                self.assertLess(source.index(f'#include "{header}"'), seed)
        # The style gate scans port/config/libc files, never the vendor submodule.
        from test_style64 import CHECKER
        files = set(CHECKER.source_files())
        self.assertTrue(set(PORT.rglob("*.h")) <= files)
        self.assertTrue(set(PORT.rglob("*.c")) <= files)
        self.assertFalse(any("third_party" in path.parts for path in files))

    def test_header_diagnostics_are_visible_and_vendor_stays_isolated(self) -> None:
        headers = sorted(path.relative_to(PORT) for path in PORT.rglob("*.h"))
        with tempfile.TemporaryDirectory(prefix="mpport-headers64-") as temporary:
            folder = Path(temporary)
            port = folder / "port"
            vendor = folder / "vendor"
            (vendor / "py").mkdir(parents=True)
            shutil.copytree(PORT, port)
            # Context only: real cross-builds use upstream types/generated headers.
            (vendor / "py" / "mpconfig.h").write_text(
                "#include <stddef.h>\n#include <stdint.h>\n"
                "typedef uintptr_t mp_uint_t;\n"
            )
            (vendor / "py" / "lexer.h").write_text(
                "typedef struct _mp_lexer_t mp_lexer_t;\n"
            )
            (vendor / "py" / "runtime.h").write_text("typedef void *mp_obj_t;\n")
            runtime = vendor / "shared" / "runtime"
            runtime.mkdir(parents=True)
            (runtime / "interrupt_char.h").write_text(
                "void mp_hal_set_interrupt_char(int c);\n"
            )
            flags = [os.environ.get("HOST_CC", "cc"), "-std=c11", "-ffreestanding",
                     "-fsyntax-only", *self.policy["WARN64_CFLAGS"], "-I", str(port),
                     "-I", str(port / "libc"), "-isystem", str(vendor), "-idirafter",
                     str(ROOT / "64bit" / "src64" / "include")]
            gate = port / AUDIT.name
            for architecture in ("x86_64", "aarch64"):
                defines = ["-D__aarch64__"] if architecture == "aarch64" else []
                result = subprocess.run([*flags, *defines, str(gate)],
                                        capture_output=True, text=True, cwd=ROOT)
                self.assertEqual(0, result.returncode, result.stderr)
            for header in headers:
                with self.subTest(header=header.as_posix()):
                    path = port / header
                    original = path.read_text()
                    end = original.rindex("#endif")
                    # Mutate inside the guard, so an already-hidden header stays hidden.
                    path.write_text(original[:end] + "int prototype_warning64();\n" +
                                    original[end:])
                    transit = vendor / "transit.h"
                    transit.write_text(f'#include "py/mpconfig.h"\n#include "{header}"\n')
                    baseline = folder / "baseline.c"
                    baseline.write_text('#include <transit.h>\n')
                    inherited = subprocess.run([*flags, str(baseline)],
                                               capture_output=True, text=True, cwd=ROOT)
                    self.assertEqual(0, inherited.returncode, inherited.stderr)
                    direct = subprocess.run([*flags, str(gate)],
                                            capture_output=True, text=True, cwd=ROOT)
                    self.assertNotEqual(0, direct.returncode)
                    self.assertIn("strict-prototypes", direct.stderr)
                    self.assertIn(str(path), direct.stderr)
                    path.write_text(original)
            # Vendor-only defects still stay suppressed in the same strict TU.
            with (vendor / "py" / "mpconfig.h").open("a") as output:
                output.write("int vendor_prototype_warning();\n")
            isolated = subprocess.run([*flags, str(gate)],
                                      capture_output=True, text=True, cwd=ROOT)
            self.assertEqual(0, isolated.returncode, isolated.stderr)


if __name__ == "__main__":
    unittest.main()
