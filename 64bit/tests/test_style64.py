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
        violations = self.check_text(".c", "void f(void) { strcpy(a, b); }\n")
        self.assertIn(
            "unbounded string API is forbidden",
            [message for _, message in violations],
        )


if __name__ == "__main__":
    unittest.main()
