#!/usr/bin/env python3
"""Inventory configured MicroPython error text; no vendor writes or translations."""

from __future__ import annotations

import argparse
import ast
import bisect
import hashlib
import json
import re
import shlex
import subprocess
from pathlib import Path

from feature_probe64 import ROOT


MARKER = "P0_ERROR_TEXT64"
STRINGS = re.compile(r'"(?:\\.|[^"\\])*"')
LINE = re.compile(r'^#\s+(\d+)\s+"([^"]+)"')
BOUNDARY = re.compile(r"\b(?:mp_raise_\w+|mp_obj_new_exception\w*|"
                      r"mp_obj_exception_make_new|m_malloc_fail)\b")


def make_values() -> dict[str, list[str]]:
    """Read the real common inventory and each target's compiler configuration."""
    names = ["MPY_CORE_SRCS", "MPY_PORT_SRCS", "A64_MPY_PORT_SRCS", "MPY_SHARED_OBJS",
             "MPY_OBJS_DIR", "X64_CC", "A64_CC", "X64_NM", "A64_NM", "MPY_OBJS",
             "MPY_PORT_OBJS", "A64_MPY_LINK_OBJS", "MPY_PORT_CFLAGS",
             "A64_MPY_PORT_CFLAGS", "MPY_VENDOR_CFLAGS", "A64_MPY_VENDOR_CFLAGS",
             "X64_OBJCOPY", "A64_OBJCOPY", "X64_LD", "A64_LD"]
    source = "include Makefile\n"
    source += "\n".join(f"$(info AUDIT64_{name}=$({name}))" for name in names)
    source += "\n.PHONY: audit64-values\naudit64-values:\n\t@:\n"
    result = subprocess.run(
        ["make", "--no-print-directory", "-s", "-f", "-", "audit64-values"],
        cwd=ROOT, input=source, text=True, capture_output=True, check=True, timeout=30,
    )
    return {line.split("=", 1)[0].removeprefix("AUDIT64_"):
            shlex.split(line.split("=", 1)[1])
            for line in result.stdout.splitlines() if line.startswith("AUDIT64_")}


def active_records(source: str) -> list[dict[str, object]]:
    """Read preprocessor line markers and fail on any nonliteral error text.

    This is a preprocessor inventory, including tables and optimizer-dead code;
    it does not assert that every configured message is runtime reachable.
    """
    records: list[dict[str, object]] = []
    path = ""
    number = 0
    lines: list[str] = []
    offsets: list[int] = []
    locations: list[tuple[str, int]] = []
    offset = 0
    for line in source.splitlines():
        directive = LINE.match(line)
        if directive:
            number = int(directive[1])
            path = directive[2]
            continue
        offsets.append(offset)
        locations.append((path, number))
        lines.append(line + "\n")
        offset += len(line) + 1
        number += 1
    body = "".join(lines)
    # Ignore marker-like bytes in literal messages; only scan C identifiers.
    tokens = re.compile(STRINGS.pattern + r"|'(?:\\.|[^'\\])*'|\b" + MARKER + r"\(")
    for match in tokens.finditer(body):
        if match[0].startswith(('"', "'")):
            continue
        path, number = locations[bisect.bisect_right(offsets, match.start()) - 1]
        tail = body[match.end():]
        literals = re.match(r'(?:\s*"(?:\\.|[^"\\])*")+\s*\)', tail)
        if literals is None:
            raise ValueError(f"unparsed error text at {path}:{number}: {tail[:200]}")
        text = "".join(ast.literal_eval(value) for value in STRINGS.findall(literals[0]))
        location = Path(path)
        if location.is_absolute():
            location = location.relative_to(ROOT)
        records.append({"path": location.as_posix(), "line": number, "text": text})
    return records


def symbol_boundaries(obj: Path, nm: list[str]) -> dict[str, list[str]]:
    result = subprocess.run([*nm, str(obj)], cwd=ROOT, text=True, capture_output=True,
                            check=True, timeout=30)
    imports: set[str] = set()
    definitions: set[str] = set()
    for line in result.stdout.splitlines():
        tokens = line.split()
        if len(tokens) < 2 or not BOUNDARY.fullmatch(tokens[-1]):
            continue
        (imports if tokens[-2] == "U" else definitions).add(tokens[-1])
    return {"imports": sorted(imports), "definitions": sorted(definitions)}


def inventory(folder: Path) -> dict[str, object]:
    values = make_values()
    shared = ["third_party/micropython/" +
              str(Path(path).relative_to(values["MPY_OBJS_DIR"][0]).with_suffix(".c"))
              for path in values["MPY_SHARED_OBJS"]]
    forced = folder / "error-marker64.h"
    forced.write_text('#include "py/misc.h"\n#undef MP_ERROR_TEXT\n'
                      f'#define MP_ERROR_TEXT(x) {MARKER}(x)\n')
    entries: dict[str, dict[str, object]] = {}
    profiles: dict[str, object] = {}
    boundaries: dict[str, object] = {}
    for arch, compiler, flags, port_flags, port in (
        ("x86_64", "X64_CC", "MPY_VENDOR_CFLAGS", "MPY_PORT_CFLAGS", "MPY_PORT_SRCS"),
        ("aarch64", "A64_CC", "A64_MPY_VENDOR_CFLAGS", "A64_MPY_PORT_CFLAGS",
         "A64_MPY_PORT_SRCS"),
    ):
        sources = sorted(set(values["MPY_CORE_SRCS"] + values[port] + shared))
        commands = {name: values[compiler] + [flag for flag in values[setting]
                                             if flag not in ("-MMD", "-MP")]
                    for name, setting in (("vendor", flags), ("port", port_flags))}
        profiles[arch] = {"commands": commands, "sources": sources}
        for source in sources:
            command = commands["port" if source in values[port] else "vendor"]
            result = subprocess.run([*command, "-E", "-include", str(forced), source],
                                    cwd=ROOT, text=True, capture_output=True,
                                    check=True, timeout=30)
            for record in active_records(result.stdout):
                text = str(record.pop("text"))
                entry = entries.setdefault(text, {
                    "id": "mp64-" + hashlib.sha256(text.encode()).hexdigest()[:16],
                    "text": text, "classification": "builtin_error",
                    "policy": "render_only; capture console mode; preserve arguments",
                    "translation": "pending", "sites": [],
                })
                site = {**record, "architecture": arch}
                if site not in entry["sites"]:
                    entry["sites"].append(site)
        names = (values["MPY_OBJS"] + values["MPY_PORT_OBJS"] + values["MPY_SHARED_OBJS"]
                 if arch == "x86_64" else values["A64_MPY_LINK_OBJS"])
        objects = [ROOT / name for name in sorted(names)]
        if not objects or any(not obj.is_file() for obj in objects):
            raise RuntimeError(f"missing {arch} objects: build both OS targets first")
        nm = values["X64_NM" if arch == "x86_64" else "A64_NM"] or [
            values[compiler][-1].removesuffix("gcc") + "nm"]
        boundaries[arch] = {obj.relative_to(ROOT).as_posix(): result
                            for obj in objects if any(
                                (result := symbol_boundaries(obj, nm)).values())}
    revision = subprocess.check_output(
        ["git", "-C", "third_party/micropython", "rev-parse", "HEAD"], cwd=ROOT,
        text=True, timeout=30).strip()
    return {"schema": 1, "upstream_commit": revision,
            "scope": "preprocessor-active MP_ERROR_TEXT; not exhaustive output/reachability",
            "exclusions": ["non-MP_ERROR_TEXT, errno, qstr, type-only, dynamic/user text",
                           "first-party non-error output; nano; Mowkow"],
            "profiles": profiles, "boundaries": boundaries,
            "messages": [entries[text] for text in sorted(entries)]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build64/feature-p0/mp-audit")
    parser.add_argument("--check", type=Path, help="fail if the committed catalog is stale")
    args = parser.parse_args()
    folder = args.output.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    report = inventory(folder)
    destination = folder / "catalog.json"
    serialized = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    destination.write_text(serialized)
    if args.check is not None and json.loads(args.check.read_text()) != report:
        raise RuntimeError(f"stale catalog: compare {args.check} and {destination}")
    print(f"MicroPython active catalog: {len(report['messages'])} messages; {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
