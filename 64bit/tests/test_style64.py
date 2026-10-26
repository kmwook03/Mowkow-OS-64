from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER_PATH = ROOT / "64bit" / "tools" / "check_style64.py"
SPEC = importlib.util.spec_from_file_location("check_style64", CHECKER_PATH)
assert SPEC is not None and SPEC.loader is not None
CHECKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECKER)


class Style64Test(unittest.TestCase):
    def check_text(self, suffix: str, text: str) -> list[tuple[int, str]]:
        with tempfile.NamedTemporaryFile(
            mode="w", suffix=suffix, encoding="utf-8", dir=ROOT, delete=False
        ) as output:
            output.write(text)
            path = Path(output.name)
        try:
            return CHECKER.check_path(path)
        finally:
            path.unlink()

    def test_accepts_guarded_header(self) -> None:
        violations = self.check_text(
            ".h", "#ifndef TEST_H\n#define TEST_H\n\nint value;\n\n#endif\n"
        )
        self.assertEqual([], violations)

    def test_rejects_trailing_whitespace_and_space_indent(self) -> None:
        violations = self.check_text(".c", "int f(void)\n{\n    return 0; \n}\n")
        messages = [message for _, message in violations]
        self.assertIn("trailing whitespace", messages)
        self.assertIn("C indentation uses spaces instead of a tab", messages)

    def test_rejects_unbounded_string_api(self) -> None:
        violations = self.check_text(
            ".c", "void f(void) { str" "cpy(a, b); }\n"
        )
        self.assertIn(
            "unbounded string API is forbidden",
            [message for _, message in violations],
        )

    def test_libc_exceptions_only_allow_exact_signatures(self) -> None:
        cases = (
            ("64bit/src64/lib/kstring64.c", "char *str" "cpy(char *dst, const char *src)"),
            ("64bit/src64/include/kstring64.h", "char *str" "cpy(char *dst, const char *src);"),
            ("64bit/src64/mpport/libc/stdio.h", "int spr" "intf(char *str, const char *fmt, ...);"),
            ("64bit/nano64/include/nano_posix64.h", "char *str" "cpy(char *dst, const char *src);"),
            ("64bit/nano64/include/nano_posix64.h", "char *str" "cat(char *dst, const char *src);"),
            ("64bit/nano64/include/nano_posix64.h",
             "int spr" "intf(char *dst, const char *format, ...);"),
            ("64bit/nano64/libc64.c", "char *str" "cpy(char *dst, const char *src)"),
            ("64bit/nano64/libc64.c", "char *str" "cat(char *dst, const char *src)"),
            ("64bit/nano64/format64.c", "int spr" "intf(char *dst, const char *format, ...)"),
        )
        for path, declaration in cases:
            with self.subTest(path=path):
                self.assertEqual([], CHECKER.check_string_apis(Path(path), declaration))
                violations = CHECKER.check_string_apis(
                    Path(path), declaration + "\nvoid f(void) { str" "cpy(a, b); }\n"
                )
                self.assertEqual([(2, "unbounded string API is forbidden")], violations)

    def test_rejects_multiline_calls_and_function_aliases(self) -> None:
        source = "void f(void) { str" "cpy\n(a, b); }\nvoid *p = str" "cpy;\n"
        self.assertEqual(
            [(1, "unbounded string API is forbidden"),
             (3, "unbounded string API is forbidden")],
            CHECKER.check_string_apis(Path("64bit/src64/lib/example.c"), source),
        )

    def test_ignores_comments_and_string_literals(self) -> None:
        source = '/* str' 'cpy(a, b) */\n// spr' 'intf(b, f)\nchar *s = "str' 'cpy(a,b)";\n'
        self.assertEqual([], CHECKER.check_string_apis(Path("example.c"), source))


if __name__ == "__main__":
    unittest.main()
