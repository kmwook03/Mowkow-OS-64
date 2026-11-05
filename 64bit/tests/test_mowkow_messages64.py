from __future__ import annotations

import ast
import contextlib
import importlib
import io
import sys
import types
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "64bit/py64/머꼬"


class MowkowMessages64Test(unittest.TestCase):
    def setUp(self) -> None:
        self.mode = 1
        self.lookups = 0
        self.mowio = types.ModuleType("mowio")
        self.mowio.input_mode = self.input_mode
        self.mowio.readfile = lambda path: (PORT / path).read_text()
        self.mowio.argv = lambda: None
        self.mowio.readline = lambda prompt: ""
        self.modules = patch.dict(sys.modules)
        self.modules.start()
        for name in ("_compat", "_error", "_data", "_parse", "_eval", "mowkow"):
            sys.modules.pop(name, None)
        sys.modules["mowio"] = self.mowio
        self.path = patch.object(sys, "path", [str(PORT), *sys.path])
        self.path.start()
        self.errors = importlib.import_module("_error")

    def tearDown(self) -> None:
        self.path.stop()
        self.modules.stop()

    def input_mode(self) -> int:
        self.lookups += 1
        return self.mode

    def output(self, function, *args) -> str:
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            function(*args)
        return output.getvalue()

    def test_structured_errors_preserve_data_and_query_once(self) -> None:
        e = self.errors
        cases = (
            (e.ErrSyntax(), "구문 오류", "syntax error"),
            (e.ErrUnbound("invalid syntax 한글 %s"),
             "이름 'invalid syntax 한글 %s'을(를) 찾을 수 없습니다.",
             "name 'invalid syntax 한글 %s' is not defined"),
            (e.ErrArgs("정의"), "함수 정의: 인수 개수 오류입니다.",
             "function 정의: wrong number of arguments"),
            (e.ErrType("+", True), "함수 <내장함수 '+'>: 타입 오류입니다.",
             "function <builtin function '+'>: type error"),
        )
        for error, korean, english in cases:
            original = str(error), error.args
            for mode, expected in ((1, korean), (0, english), (-22, english)):
                with self.subTest(error=type(error).__name__, mode=mode):
                    self.mode = mode
                    self.lookups = 0
                    result = self.output(e.report_error, error, "파일 %s.mk", 7)
                    prefix = "오류" if mode == 1 else "error"
                    self.assertEqual(result, f"파일 %s.mk:7: {prefix}: {expected}\n")
                    self.assertEqual(self.lookups, 1)
                    self.assertEqual((str(error), error.args), original)
        class CustomError(e.ErrLisp):
            def __str__(self):
                return "invalid syntax 사용자 문장 %s"
        self.mode = 0
        self.assertEqual(self.output(e.report_error, CustomError()),
                         "error: invalid syntax 사용자 문장 %s\n")
        self.mode = 1
        self.assertEqual(self.output(e.report_error, SyntaxError("invalid syntax 사용자 문장")),
                         "오류: invalid syntax 사용자 문장\n")

    def test_snapshot_precedes_formatting_and_lexical_positions(self) -> None:
        e = self.errors
        owner = self

        class ChangingName:
            def __str__(self):
                owner.mode = 0
                return "식별자"

        self.assertEqual(self.output(e.report_error, e.ErrArgs(ChangingName())),
                         "오류: 함수 식별자: 인수 개수 오류입니다.\n")
        self.assertEqual(self.mode, 0)
        self.assertEqual(self.output(e.report_lexical, "이름 %q"),
                         "error: lexical error near token '이름 %q'\n")
        self.assertEqual(self.output(e.report_syntax, "원문.mk", 3, 9, "토큰 %d"),
                         "원문.mk:3:9: syntax error: '토큰 %d'\n")

    def test_real_reader_evaluator_and_normal_values(self) -> None:
        program = importlib.import_module("mowkow")
        # The real bundled library and evaluator run; only I/O is substituted.
        for mode, expected in ((1, "오류: 이름 '없는이름'을(를) 찾을 수 없습니다."),
                               (0, "error: name '없는이름' is not defined")):
            lines = iter(("없는이름", "(+ 1 2)", '"invalid syntax"', ""))
            self.mowio.readline = lambda prompt: next(lines)
            self.mode = mode
            result = self.output(program.main)
            self.assertIn(expected + "\n", result)
            self.assertIn('3\n"invalid syntax"\n', result)
            self.assertIn("머꼬'를 사용해 주셔서 고맙습니다.", result)

    def test_catalog_and_message_id_coverage(self) -> None:
        messages = self.errors._MESSAGES
        used = set()
        for name in ("_error.py", "_parse.py", "_eval.py", "mowkow.py"):
            tree = ast.parse((PORT / name).read_text())
            for node in ast.walk(tree):
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
                    if node.func.id == "error_message":
                        self.assertIsInstance(node.args[0], ast.Constant)
                        used.add(node.args[0].value)
        self.assertEqual(used, messages.keys())
        for korean, english in messages.values():
            self.assertEqual(korean.count("%s"), english.count("%s"))
            self.assertTrue(any("가" <= char <= "힣" for char in korean))


if __name__ == "__main__":
    unittest.main()
