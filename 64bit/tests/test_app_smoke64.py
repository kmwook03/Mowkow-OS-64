from __future__ import annotations

import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "64bit" / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "app_regression64", ROOT / "64bit" / "tools" / "app_regression64.py"
)
assert SPEC is not None and SPEC.loader is not None
REGRESSION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REGRESSION)
sys.path.pop(0)


class AppSmoke64Test(unittest.TestCase):
    def test_app_results_and_faults(self) -> None:
        with tempfile.TemporaryDirectory(prefix="app-smoke64-") as temporary:
            for app in ("KTEST", "MTEST", "WTEST"):
                with self.subTest(app=app):
                    executable = Path(temporary) / app
                    subprocess.run(
                        [os.environ.get("HOST_CC", "cc"), "-O2", "-std=c11",
                         "-Wall", "-Wextra", "-Werror", "-fno-builtin",
                         "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                         "-fno-omit-frame-pointer", "-no-pie", f"-DAPP_{app}64",
                         "-I", str(ROOT / "64bit" / "app64" / "crt" / "include"),
                         str(ROOT / "64bit" / "tests" / "app_smoke64_host.c"),
                         "-o", str(executable)], check=True, cwd=ROOT,
                    )
                    subprocess.run(
                        [str(executable)], check=True, timeout=30,
                        env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"},
                    )

    def test_result_parser_rejects_false_success(self) -> None:
        valid = "mtest\nmtest: PASS\nexit 0\n> "
        REGRESSION.check_app(valid, "mtest")
        REGRESSION.check_app("ktest --bad\nktest: FAIL usage\nexit 1\n> ", "ktest", 1)
        bad = (
            valid.replace("exit 0", "exit 1"),
            valid.replace("PASS", "PASS extra"),
            valid.replace("PASS", "FAIL\nmtest: PASS"),
            valid.replace("exit 0", "exit 0\nexit 0"),
            valid.replace("PASS", "PASS\nmtest: PASS"),
            valid.replace("exit 0\n", ""),
            valid[:-2],
        )
        for raw in bad:
            with self.subTest(raw=raw), self.assertRaises(AssertionError):
                REGRESSION.check_app(raw, "mtest")


if __name__ == "__main__":
    unittest.main()
