from __future__ import annotations

import importlib.util
import os
import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import patch


TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location("feature_mp_audit64",
                                             TOOLS / "feature_mp_audit64.py")
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)
PROBE_SPEC = importlib.util.spec_from_file_location("feature_probe64",
                                                   TOOLS / "feature_probe64.py")
PROBE = importlib.util.module_from_spec(PROBE_SPEC)
PROBE_SPEC.loader.exec_module(PROBE)


class FeatureMpAudit64Test(unittest.TestCase):
    def test_configured_branches_and_concatenation(self) -> None:
        source = '''
#define MP_ERROR_TEXT(x) P0_ERROR_TEXT64(x)
#line 40 "configured.c"
#if ENABLED
const char *a = MP_ERROR_TEXT("active %" WIDTH " bytes");
#else
const char *b = MP_ERROR_TEXT("inactive");
#endif
const char *data = "P0_ERROR_TEXT64(fake)";
char quote = '"';
const char *after_char = MP_ERROR_TEXT("after char");
'''
        for enabled, expected, line in ((1, "active %lu bytes", 41), (0, "inactive", 43)):
            with self.subTest(enabled=enabled):
                result = subprocess.run(
                    [os.environ.get("HOST_CC", "cc"), "-E", "-x", "c", "-",
                     f"-DENABLED={enabled}", '-DWIDTH="lu"'], input=source,
                    text=True, capture_output=True, check=True, timeout=30,
                )
                self.assertEqual(AUDIT.active_records(result.stdout), [
                    {"path": "configured.c", "line": line, "text": expected},
                    {"path": "configured.c", "line": 47, "text": "after char"},
                ])

    def test_multiline_system_markers_and_escaped_data(self) -> None:
        source = r'''
# 10 "py/runtime.c"
raise_error(P0_ERROR_TEXT64("size %"
# 12 "py/runtime.c" 3 4
"lu" " bytes\n"));
# 20 "py/lexer.c"
P0_ERROR_TEXT64("bad (token), \"quoted\""); P0_ERROR_TEXT64("second");
'''
        records = AUDIT.active_records(source)
        self.assertEqual(records, [
            {"path": "py/runtime.c", "line": 10, "text": "size %lu bytes\n"},
            {"path": "py/lexer.c", "line": 20, "text": 'bad (token), "quoted"'},
            {"path": "py/lexer.c", "line": 20, "text": "second"},
        ])

    def test_unparsed_text_fails_instead_of_disappearing(self) -> None:
        with self.assertRaisesRegex(ValueError, "unparsed error text at py/runtime.c:9"):
            AUDIT.active_records('# 9 "py/runtime.c"\nf(P0_ERROR_TEXT64(dynamic));\n')

    def test_boundary_symbols_separate_imports_and_definitions(self) -> None:
        result = subprocess.CompletedProcess([], 0, stdout=(
            "                 U mp_raise_OSError\n"
            "0000000000000110 T mp_raise_msg\n"
            "0000000000000030 t local_helper\n"
            "                 U user_print\n"
            "                 U mp_obj_exception_make_new\n"
        ))
        with patch.object(AUDIT.subprocess, "run", return_value=result) as run:
            self.assertEqual(AUDIT.symbol_boundaries(Path("runtime.o"), ["nm"]), {
                "imports": ["mp_obj_exception_make_new", "mp_raise_OSError"],
                "definitions": ["mp_raise_msg"],
            })
            self.assertEqual(run.call_args.args[0], ["nm", "runtime.o"])


class FeatureMpCoverage64Test(unittest.TestCase):
    def test_pending_ids_are_explicit_and_duplicates_are_not_coverage(self) -> None:
        result = PROBE.message_coverage({"messages": [
            {"id": "first", "text": "first format"},
            {"id": "second", "text": "second format"},
        ]}, "command line\nFORMAT64 first format\nFORMAT64 first format\n")
        self.assertEqual(result["active_formats"], 2)
        self.assertEqual(result["prototype_formats"], ["first format"])
        self.assertEqual(result["pending_ids"], ["second"])
        self.assertFalse(result["complete"])

    def test_unknown_or_empty_format_inventory_fails(self) -> None:
        for output in ("command only\n", "FORMAT64 unexpected\n"):
            with self.subTest(output=output), self.assertRaises(ValueError):
                PROBE.message_coverage({"messages": [{"id": "one", "text": "known"}]}, output)

    def test_full_format_set_does_not_imply_constructor_acceptance(self) -> None:
        result = PROBE.message_coverage({"messages": [{"id": "one", "text": "known"}]},
                                       "FORMAT64 known\n")
        self.assertEqual(result["pending_ids"], [])
        self.assertFalse(result["complete"])


if __name__ == "__main__":
    unittest.main()
