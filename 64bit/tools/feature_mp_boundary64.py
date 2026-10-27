#!/usr/bin/env python3
"""Build isolated, identity-preserving MicroPython object aliases; no vendor writes."""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
from pathlib import Path

from feature_probe64 import PROBES, ROOT, WARNINGS, run
from feature_mp_audit64 import make_values


PINNED64 = {
    "objexcept": "8a375dbfb56445f08a1d7cf1d6e5a12c291902a8185bb1a724f0d314319c174e",
    "runtime": "8ee7aded13aa2af2c0254349fd7af6e77f68b980700e6bdebe498d755967fd70",
}
TARGETS64 = {
    "objexcept": ["mp_obj_exception_make_new", "mp_obj_new_exception",
                  "mp_obj_new_exception_args", "mp_obj_new_exception_msg",
                  "mp_obj_new_exception_msg_varg", "mp_obj_new_exception_msg_vlist"],
    "runtime": ["mp_raise_type_arg", "mp_raise_OSError_with_filename"],
}
# No inlining/cloning may bind a same-TU call to the original body before linking.
FLAGS64 = ["-fno-inline", "-fno-ipa-cp", "-fno-ipa-sra", "-fno-ipa-icf",
           "-fno-optimize-sibling-calls"]


def capture(command: list[str]) -> str:
    return subprocess.check_output(command, cwd=ROOT, text=True, timeout=30)


def functions(table: str, names: list[str], binding: str) -> dict[str, tuple[str, int]]:
    """Require unique, defined ELF functions; do not guess offsets for missing symbols."""
    result: dict[str, tuple[str, int]] = {}
    for line in table.splitlines():
        fields = line.split()
        if not fields or fields[-1] not in names:
            continue
        if (len(fields) != 6 or fields[1:3] != [binding, "F"] or
                not fields[3].startswith(".text") or fields[-1] in result):
            raise ValueError(f"invalid/duplicate function definition: {line}")
        result[fields[-1]] = (fields[3], int(fields[0], 16))
    if set(result) != set(names):
        raise ValueError(f"missing functions: {sorted(set(names) - result.keys())}")
    return result


def relocation_rows(output: str) -> list[str]:
    return [line.strip() for line in output.splitlines()
            if re.match(r"^[0-9a-fA-F]+\s+R_", line)]


def text_rows(output: str) -> list[str]:
    return [line.strip() for line in output.splitlines()
            if re.match(r"^\s+[0-9a-fA-F]+\s+[0-9a-fA-F]{2,8}\b", line)]


def code_sections(headers: str) -> list[str]:
    """AArch64 uses function sections and an empty .text in the real flags."""
    sections: list[str] = []
    previous: tuple[str, int] | None = None
    for line in headers.splitlines():
        fields = line.split()
        if len(fields) == 7 and fields[0].isdigit():
            previous = (fields[1], int(fields[2], 16))
        elif previous is not None:
            if "CODE" in fields and previous[1] != 0:
                sections.append(previous[0])
            previous = None
    if not sections:
        raise ValueError("no nonempty executable sections")
    return sections


def alias_object(raw: Path, modified: Path, names: list[str], objcopy: list[str],
                 objdump: list[str], log: Path) -> dict[str, object]:
    """Add entry aliases on a separate copy, preserving code and relocation targets."""
    if raw.resolve() == modified.resolve():
        raise ValueError("object aliases require a separate output copy")
    before = functions(capture([*objdump, "-t", str(raw)]), names, "g")
    options: list[str] = []
    for name, (section, offset) in before.items():
        options += [f"--add-symbol=__real_{name}={section}:0x{offset:x},global,function",
                    f"--weaken-symbol={name}"]
    run([*objcopy, *options, str(raw), str(modified)], ROOT, log)
    table = capture([*objdump, "-t", str(modified)])
    weak = functions(table, names, "w")
    aliases = functions(table, [f"__real_{name}" for name in names], "g")
    if before != weak or any(aliases[f"__real_{name}"] != before[name] for name in names):
        raise ValueError("object alias or weak definition changed original address")
    old_relocs = relocation_rows(capture([*objdump, "-r", str(raw)]))
    new_relocs = relocation_rows(capture([*objdump, "-r", str(modified)]))
    if old_relocs != new_relocs:
        raise ValueError("aliasing rewrote relocations")
    sections = code_sections(capture([*objdump, "-h", str(raw)]))
    if sections != code_sections(capture([*objdump, "-h", str(modified)])):
        raise ValueError("aliasing changed executable sections")
    for section in sections:
        old_text = text_rows(capture([*objdump, "-s", "-j", section, str(raw)]))
        if not old_text or old_text != text_rows(
                capture([*objdump, "-s", "-j", section, str(modified)])):
            raise ValueError("aliasing rewrote or lacks instruction bytes")
    log.with_suffix(".symbols").write_text(table)
    log.with_suffix(".relocations").write_text("\n".join(new_relocs) + "\n")
    return {
        "original_sha256": hashlib.sha256(raw.read_bytes()).hexdigest(),
        "aliased_sha256": hashlib.sha256(modified.read_bytes()).hexdigest(),
        "definitions": {name: {"section": section, "offset": offset}
                        for name, (section, offset) in before.items()},
        "instructions_and_relocations_unchanged": True,
        "checked_code_sections": sections,
    }


def build_objects(folder: Path) -> dict[str, object]:
    values = make_values()
    report: dict[str, object] = {"source_sha256": {}, "profiles": {}}
    for unit, expected in PINNED64.items():
        source = ROOT / f"third_party/micropython/py/{unit}.c"
        actual = hashlib.sha256(source.read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError(f"pinned vendor hash mismatch: {source}")
        report["source_sha256"][unit] = actual
    for arch, prefix, flag_name, port_flags in (
        ("x86_64", "X64", "MPY_VENDOR_CFLAGS", "MPY_PORT_CFLAGS"),
        ("aarch64", "A64", "A64_MPY_VENDOR_CFLAGS", "A64_MPY_PORT_CFLAGS"),
    ):
        destination = folder / arch
        destination.mkdir(parents=True, exist_ok=True)
        compiler = values[f"{prefix}_CC"]
        objcopy = values[f"{prefix}_OBJCOPY"]
        objdump = [objcopy[-1].removesuffix("objcopy") + "objdump"]
        flags = [flag for flag in values[flag_name] if flag not in ("-MMD", "-MP")]
        profile: dict[str, object] = {"compiler": compiler, "flags": flags + FLAGS64,
                                     "objects": {}}
        transformed: list[str] = []
        for unit, names in TARGETS64.items():
            raw = destination / f"{unit}-original.o"
            modified = destination / f"{unit}-aliased.o"
            run([*compiler, *flags, *FLAGS64, "-c",
                 f"third_party/micropython/py/{unit}.c", "-o", str(raw)], ROOT,
                destination / f"{unit}-compile.log")
            profile["objects"][unit] = alias_object(
                raw, modified, names, objcopy, objdump, destination / f"{unit}-alias.log")
            new_relocs = relocation_rows(capture([*objdump, "-r", str(modified)]))
            required = ("mp_obj_exception_make_new", "mp_obj_new_exception_msg_vlist") if (
                unit == "objexcept") else ("mp_raise_type_arg",)
            for name in required:
                if not any(re.search(r"\s" + name + r"(?:[+-]|$)", row) for row in new_relocs):
                    raise ValueError(f"same-TU relocation missing for {name}")
            transformed.append(str(modified))
        shim = destination / "forward.o"
        # This cross-build fixture only forwards to real originals. It verifies
        # ABI/symbol/relocation mechanics; it is never an OS image or runtime pass.
        run([*compiler, *values[port_flags], "-I", str(PROBES), "-c",
             str(PROBES / "mp_boundary_forward64.c"), "-o", str(shim)], ROOT,
            destination / "forward-compile.log")
        run([*compiler, *values[port_flags], "-c", str(PROBES / "mp_origin_store64.c"),
             "-o", str(destination / "origin-store.o")], ROOT,
            destination / "origin-store-compile.log")
        linked = destination / "boundary-linked.o"
        run([*values[f"{prefix}_LD"], "-r", *transformed, str(shim), "-o", str(linked)],
            ROOT, destination / "partial-link.log")
        table = capture([*objdump, "-t", str(linked)])
        all_names = sum(TARGETS64.values(), [])
        strong = functions(table, all_names, "g")
        originals = functions(table, [f"__real_{name}" for name in all_names], "g")
        if any(strong[name] == originals[f"__real_{name}"] for name in all_names):
            raise ValueError("public forwarding wrapper aliases its original body")
        (destination / "linked-symbols.log").write_text(table)
        profile["partial_link"] = "PASS; strong public names, distinct original aliases"
        report["profiles"][arch] = profile
    (folder / "objects.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def experiment(folder: Path, cc: str, objects: list[Path]) -> dict[str, object]:
    """Keep original failure probes and compare a separate strong-symbol executable."""
    folder.mkdir(parents=True, exist_ok=True)
    report = build_objects(folder)
    executable = folder / "mp-boundary-probe64"
    defines = ["-DP0_BOUNDARY64=1"]
    for name in TARGETS64["objexcept"]:
        if name != "mp_obj_new_exception":
            defines.append(f"-D__wrap_{name}={name}")
    selected = [obj for obj in objects if obj.name not in ("objexcept.o", "runtime.o")]
    selected += [folder / "x86_64" / f"{unit}-aliased.o" for unit in TARGETS64]
    command = [cc, "-O2", "-g", "-std=c11", "-no-pie", *WARNINGS, *defines,
               "-I", str(ROOT / "64bit/src64/mpport"), "-I", str(ROOT / "build64/mpgen"),
               "-isystem", str(ROOT / "third_party/micropython"),
               "-idirafter", str(ROOT / "64bit/src64/include"),
               str(PROBES / "mp_exception_probe64.c"),
               str(PROBES / "mp_lifetime_probe64.c"), *map(str, selected), "-lm",
               "-Wl,--wrap=gc_alloc", "-Wl,--wrap=mp_obj_print_exception",
               "-Wl,--defsym=__real_mp_raise_OSError=mp_raise_OSError",
               f"-Wl,-Map={folder / 'host-link.map'}", "-o", str(executable)]
    run(command, ROOT, folder / "host-link.log")
    table = capture(["objdump", "-t", str(executable)])
    names = sum(TARGETS64.values(), [])
    public = functions(table, names, "g")
    originals = functions(table, [f"__real_{name}" for name in names], "g")
    if any(public[name] == originals[f"__real_{name}"] for name in names):
        raise ValueError("host public constructor is not a distinct wrapper")
    (folder / "host-symbols.log").write_text(table)
    # Each runtime subprocess is bounded, even if a production assert halts.
    for mode in ("identity", "raises", "users", "secondary"):
        run(["timeout", "30s", str(executable), f"--boundary-{mode}"], ROOT,
            folder / f"{mode}.log")
    for mode in ("live", "gc", "reuse", "lock", "oom"):
        run(["timeout", "30s", str(executable), f"--lifetime-{mode}"], ROOT,
            folder / f"{mode}.log")
    compact_report = compact_experiment(folder, command)
    report.update({"runtime": "PASS; identity and constructor provenance boundary only",
                   "compact_store": compact_report,
                   "host": "x86_64 real core with isolated objexcept/runtime rebuild",
                   "aarch64": "compile/symbol/partial-link only; no runtime",
                   "production_os": "unchanged; not tested in QEMU or Pi 5",
                   "full_message_translation": "NOT COMPLETE; prior 149 pending IDs remain"})
    (folder / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    return report


def compact_experiment(folder: Path, command: list[str]) -> dict[str, object]:
    compact = folder / "compact"
    compact.mkdir(parents=True, exist_ok=True)
    executable = compact / "mp-compact-probe64"
    link = [value for value in command[:-2] if not value.startswith("-Wl,-Map=")]
    run([*link, "-DP0_COMPACT64=1", str(PROBES / "mp_origin_store64.c"),
         f"-Wl,-Map={compact / 'link.map'}", "-o", str(executable)], ROOT,
        compact / "link.log")
    for mode in ("identity", "raises", "users", "secondary"):
        run(["timeout", "30s", str(executable), f"--boundary-{mode}"], ROOT,
            compact / f"{mode}.log")
    for mode in ("live", "gc", "reuse", "lock", "oom"):
        run(["timeout", "30s", str(executable), f"--lifetime-{mode}"], ROOT,
            compact / f"{mode}.log")
    for mode in ("typed", "owned"):
        run(["timeout", "30s", str(executable), f"--compact-{mode}"], ROOT,
            compact / f"{mode}.log")
    partial: list[dict[str, str]] = []
    filenames: list[dict[str, str]] = []
    for count in range(9):
        for mode, marker, records in (
            (f"partial-{count}", "COMPACT_PARTIAL64", partial),
            (f"filename-0-{count}", "COMPACT_FILENAME64", filenames),
            (f"filename-1-{count}", "COMPACT_FILENAME64", filenames),
        ):
            log = compact / f"{mode}.log"
            run(["timeout", "30s", str(executable), f"--compact-{mode}"], ROOT, log)
            rows = [line for line in log.read_text().splitlines() if line.startswith(marker)]
            if len(rows) != 1:
                raise ValueError(f"missing/duplicate compact result: {log}")
            records.append(dict(re.findall(r"(\w+)=(\S+)", rows[0])))
    fragmented = compact / "partial-fragment.log"
    run(["timeout", "30s", str(executable), "--compact-partial-fragment"], ROOT, fragmented)
    rows = [line for line in fragmented.read_text().splitlines()
            if line.startswith("COMPACT_PARTIAL64")]
    if len(rows) != 1:
        raise ValueError("missing/duplicate fragmented-heap result")
    partial.append(dict(re.findall(r"(\w+)=(\S+)", rows[0])))
    if not any(row["emergency"] == "0" and row["args"] == "0" for row in partial):
        raise ValueError("did not exercise a normal object with lost args")
    if not any(row["emergency"] == "0" and row["truncated"] == "1" for row in partial):
        raise ValueError("did not exercise a normal object with truncated original text")
    if not any(row["rejected"] == "1" for row in filenames):
        raise ValueError("did not exercise lost, unowned filename rejection")
    if not any(row["interned"] == "1" and row["type"] == "OSError" and
               row["args"] == "0" and row["rejected"] == "0" for row in filenames):
        raise ValueError("did not preserve a filename qstr after losing the original tuple")
    size = re.search(r"static table (\d+) bytes", (compact / "lock.log").read_text())
    if size is None or int(size[1]) > (2 * 1024 * 1024) * 3 // 10:
        raise ValueError("compact metadata exceeds candidate 30-percent heap budget")
    result = {"metadata_bytes": int(size[1]), "heap_bytes": 2 * 1024 * 1024,
              "partial": partial, "filenames": filenames,
              "status": "PASS for scalar/qstr/owned arguments; detached strings remain a limit"}
    (compact / "report.json").write_text(json.dumps(result, indent=2) + "\n")
    return result
