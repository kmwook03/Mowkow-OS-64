from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import feature_mp_boundary64 as BOUNDARY


class FeatureMpBoundary64Test(unittest.TestCase):
    def test_real_alias_preserves_internal_calls_and_pointer_identity(self) -> None:
        with tempfile.TemporaryDirectory(prefix="mowkow-boundary-") as directory:
            folder = Path(directory)
            original = folder / "original.c"
            wrapper = folder / "wrapper.c"
            raw = folder / "original.o"
            aliased = folder / "aliased.o"
            executable = folder / "probe"
            original.write_text('''
int native64(int value) { return value + 7; }
int (*const slot64)(int) = native64;
int internal64(int value) { return native64(value); }
''')
            wrapper.write_text('''
#include <assert.h>
extern int __real_native64(int);
extern int internal64(int);
extern int (*const slot64)(int);
static unsigned calls64;
int native64(int value) { calls64++; return __real_native64(value) + 100; }
int main(void) {
    assert(slot64 == native64);
    assert(slot64 != __real_native64);
    assert(slot64(3) == 110);
    assert(internal64(5) == 112);
    assert(calls64 == 2);
    return 0;
}
''')
            cc = os.environ.get("HOST_CC", "cc")
            subprocess.run([cc, "-O2", *BOUNDARY.FLAGS64, "-ffunction-sections", "-fno-pie",
                            "-c", str(original),
                            "-o", str(raw)], check=True, capture_output=True, timeout=30)
            before = raw.read_bytes()
            report = BOUNDARY.alias_object(raw, aliased, ["native64"], ["objcopy"],
                                           ["objdump"], folder / "alias.log")
            self.assertEqual(raw.read_bytes(), before)
            self.assertTrue(report["instructions_and_relocations_unchanged"])
            self.assertEqual(report["checked_code_sections"],
                             [".text.native64", ".text.internal64"])
            subprocess.run([cc, "-no-pie", str(wrapper), str(aliased), "-o", str(executable)],
                           check=True, capture_output=True, timeout=30)
            result = subprocess.run([str(executable)], capture_output=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr.decode())

    def test_missing_undefined_duplicate_and_wrong_binding_rejected(self) -> None:
        for table in ("", "0 *UND* 0 target", "0 w F .text 10 target",
                      "0 g F .data 10 target",
                      "0 g F .text 10 target\n10 g F .text 10 target"):
            with self.subTest(table=table), self.assertRaises(ValueError):
                BOUNDARY.functions(table, ["target"], "g")

    def test_in_place_alias_rejected_before_any_tool(self) -> None:
        with self.assertRaisesRegex(ValueError, "separate output copy"):
            BOUNDARY.alias_object(Path("same.o"), Path("same.o"), ["target"],
                                  ["unavailable-objcopy"], ["unavailable-objdump"],
                                  Path("unused.log"))

    def test_empty_text_and_nonempty_function_sections(self) -> None:
        self.assertEqual(BOUNDARY.code_sections('''
  0 .text 00000000 00000000 00000000 00000040 2**0
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  1 .text.first 00000004 00000000 00000000 00000040 2**2
                  CONTENTS, ALLOC, LOAD, RELOC, READONLY, CODE
  2 .data 00000008 00000000 00000000 00000044 2**2
                  CONTENTS, ALLOC, LOAD, DATA
  3 .text.second 00000008 00000000 00000000 0000004c 2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
'''), [".text.first", ".text.second"])
        with self.assertRaises(ValueError):
            BOUNDARY.code_sections("  0 .text 00000000 0 0 40 2**0\n CODE\n")


if __name__ == "__main__":
    unittest.main()
