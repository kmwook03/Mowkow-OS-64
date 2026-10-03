#!/usr/bin/env python3
"""Small, dependency-free style gate for first-party 64-bit sources."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCAN_ROOTS = (
    ROOT / "64bit" / "src64",
    ROOT / "64bit" / "app64",
    ROOT / "64bit" / "tools",
)
SOURCE_SUFFIXES = {".c", ".h", ".S", ".asm", ".ld", ".py"}
C_LIKE_SUFFIXES = {".c", ".h"}
BANNED_STRING_APIS = re.compile(
    r"\b(?:gets|sprintf|strcat|strcpy|vsprintf)\s*\("
)
BANNED_API_ALLOWLIST = {
    Path("64bit/src64/include/kstring64.h"),
    Path("64bit/src64/lib/kstring64.c"),
    Path("64bit/src64/mpport/libc/stdio.h"),
}


def source_files() -> list[Path]:
    """Return stable, generated-file-free input for the style gate."""
    files: list[Path] = []
    for root in SCAN_ROOTS:
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            if "__pycache__" in path.parts or "upstream" in path.parts:
                continue
            files.append(path)
    return sorted(files)


def long_line_is_allowed(path: Path, line: str) -> bool:
    """Keep documented exceptions for strings, tables, and assembly."""
    if path.suffix in {".S", ".asm", ".ld"}:
        return True
    if path.suffix in C_LIKE_SUFFIXES:
        stripped = line.lstrip()
        return '"' in line or stripped.startswith("{")
    return False


def check_header_guard(path: Path, lines: list[str]) -> list[str]:
    if path.suffix != ".h":
        return []
    directives = [line.strip() for line in lines if line.lstrip().startswith("#")]
    if len(directives) < 3:
        return ["missing header guard"]
    ifndef = next((line for line in directives if line.startswith("#ifndef ")), "")
    if not ifndef:
        return ["missing header guard"]
    guard = ifndef.split(maxsplit=1)[1]
    if f"#define {guard}" not in directives:
        return [f"header guard {guard} is not defined"]
    if not any(line.startswith("#endif") for line in directives):
        return [f"header guard {guard} is not closed"]
    return []


def check_path(path: Path) -> list[tuple[int, str]]:
    """Return line-oriented violations for one source file."""
    relative = path.resolve().relative_to(ROOT)
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    violations: list[tuple[int, str]] = []

    for number, raw_line in enumerate(lines, 1):
        line = raw_line.rstrip("\r\n")
        if line.rstrip(" \t") != line:
            violations.append((number, "trailing whitespace"))
        if len(line) > 100 and not long_line_is_allowed(path, line):
            violations.append((number, f"line length {len(line)} exceeds 100"))
        if path.suffix in C_LIKE_SUFFIXES and line.startswith("    "):
            violations.append((number, "C indentation uses spaces instead of a tab"))
        if path.suffix == ".py" and line.startswith("\t"):
            violations.append((number, "Python indentation uses a tab"))
        if (relative not in BANNED_API_ALLOWLIST and
                BANNED_STRING_APIS.search(line)):
            violations.append((number, "unbounded string API is forbidden"))

    for message in check_header_guard(path, [line.rstrip("\r\n") for line in lines]):
        violations.append((1, message))
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path)
    args = parser.parse_args()
    paths = args.paths or source_files()
    count = 0
    for path in paths:
        path = path if path.is_absolute() else ROOT / path
        for line, message in check_path(path):
            print(f"{path.relative_to(ROOT)}:{line}: {message}")
            count += 1
    if count:
        print(f"style64: {count} violation(s)", file=sys.stderr)
        return 1
    print(f"style64: checked {len(paths)} file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
