from __future__ import annotations

import ast
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "64bit/src64"
CATALOG = SOURCE / "include/message64.def"


class MessageCatalog64Test(unittest.TestCase):
    def test_fixed_formats_policies_and_registered_ids(self) -> None:
        literal = r'"(?:[^"\\]|\\.)*"'
        pattern = re.compile(r'MESSAGE64\((\w+), (NOTICE|ERROR|OS_ERROR), (' + literal +
                             r'), (' + literal + r')\)')
        rows = pattern.findall(CATALOG.read_text())
        ids = {row[0] for row in rows}
        self.assertEqual(len(ids), len(rows))
        self.assertEqual(len(rows), CATALOG.read_text().count("MESSAGE64("))
        for identifier, policy, korean, english in rows:
            with self.subTest(identifier=identifier):
                korean, english = ast.literal_eval(korean), ast.literal_eval(english)
                self.assertTrue(identifier.endswith("64"))
                self.assertTrue(any("가" <= char <= "힣" for char in korean))
                self.assertNotIn("%", korean)
                self.assertNotIn("%", english)
                self.assertNotIn("\n", korean)
                if policy == "ERROR":
                    self.assertTrue(english and english.isascii())
                else:
                    self.assertEqual(english, "")
        used = set()
        for folder in (SOURCE, ROOT / "64bit/app64"):
            for path in folder.rglob("*"):
                if path.suffix not in (".c", ".h") or path.name == "message64.h":
                    continue
                used.update(re.findall(r'\bMESSAGE_([A-Z0-9_]+64)\b', path.read_text()))
        self.assertEqual(ids, used - {"ID64"})

    def test_console_direct_literals_remain_protocol_only(self) -> None:
        # New user-facing messages must have an ID. Raw writes keep data opaque.
        allowed = {"0", "0x", "\n", " KiB\n", "  "}
        for name in ("console_command64.c", "console_editor64.c", "console64.c"):
            text = (SOURCE / "kernel" / name).read_text()
            for literal in re.findall(r'console_puts_con64\(con, ("(?:[^"\\]|\\.)*")\)',
                                      text):
                self.assertIn(ast.literal_eval(literal), allowed, name)


if __name__ == "__main__":
    unittest.main()
