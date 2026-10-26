#!/usr/bin/env python3
"""Reproduce P0 host experiments; never count these as OS runtime passes."""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
import pty
import shlex
import struct
import subprocess
import tarfile
import termios
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROBES = ROOT / "64bit" / "tools" / "p0"
NANO_VERSION = "9.3"
NANO_URL = "https://www.nano-editor.org/dist/v9/nano-9.3.tar.xz"
NANO_SHA256 = "a9affc52c9596ee0682bcdd4ceb3c0b235eb221d83d733dfd9b0dff45897cb0d"
NANO_OPTIONS = [
    "--disable-browser", "--disable-color", "--disable-comment", "--disable-extra",
    "--disable-formatter", "--disable-histories", "--disable-justify", "--disable-libmagic",
    "--disable-linter", "--disable-linenumbers", "--disable-mouse", "--disable-multibuffer",
    "--disable-nanorc", "--disable-operatingdir", "--disable-speller", "--disable-tabcomp",
    "--disable-wordcomp", "--disable-wrapping", "--enable-utf8", "--disable-nls",
]
WRAPPED = [
    "mp_obj_new_exception_msg", "mp_obj_new_exception_msg_varg",
    "mp_obj_new_exception_msg_vlist", "mp_obj_new_exception_args",
    "mp_raise_OSError", "mp_obj_print_exception",
]
WARNINGS = ["-Wall", "-Wextra", "-Werror", "-Wformat=2", "-Wshadow", "-Wundef",
            "-Wmissing-prototypes", "-Wstrict-prototypes", "-Wcast-align"]


def run(command: list[str], folder: Path, log: Path) -> None:
    """Keep exact commands and diagnostics without hiding nonzero exit codes."""
    with log.open("w") as output:
        output.write(shlex.join(command) + "\n")
        output.flush()
        result = subprocess.run(command, cwd=folder, stdout=output, stderr=output,
                                timeout=600)
    if result.returncode:
        raise RuntimeError(f"{log}: exit {result.returncode}\n" + log.read_text()[-6000:])


def symbols(obj: Path, nm: str = "nm") -> list[str]:
    result = subprocess.run([nm, "-u", str(obj)], text=True, capture_output=True,
                            check=True, timeout=30)
    return sorted({line.split()[-1] for line in result.stdout.splitlines()
                   if line.split() and "U" in line.split()})


def micropython(folder: Path, cc: str) -> dict[str, object]:
    """Use separate production objects: -r would resolve calls before --wrap."""
    run(["make", "-j4", "x86_64"], ROOT, folder / "mp-build.log")
    objects = sorted((ROOT / "build64" / "upy").glob("*.o"))
    objects += sorted((ROOT / "build64" / "upy" / "shared").rglob("*.o"))
    executable = folder / "mp-exception-probe64"
    command = [cc, "-O2", "-g", "-std=c11", "-no-pie", *WARNINGS,
               "-I", str(ROOT / "64bit" / "src64" / "mpport"),
               "-I", str(ROOT / "build64" / "mpgen"),
               "-isystem", str(ROOT / "third_party" / "micropython"),
               "-idirafter", str(ROOT / "64bit" / "src64" / "include"),
               str(PROBES / "mp_exception_probe64.c"), *map(str, objects), "-lm",
               *[f"-Wl,--wrap={name}" for name in WRAPPED], "-o", str(executable)]
    run(command, ROOT, folder / "mp-link.log")
    source = folder / "사용자.py"
    source.write_text("file_missing_identifier\n", encoding="utf-8")
    run([str(executable), str(source)], ROOT, folder / "mp-run.log")
    revision = subprocess.check_output(
        ["git", "-C", "third_party/micropython", "rev-parse", "HEAD"], cwd=ROOT,
        text=True).strip()
    return {"upstream_commit": revision, "wrapped": WRAPPED, "host_probe": "PASS",
            "os_runtime": "NOT RUN", "catalog": "five exception types; not exhaustive",
            "limits": ["64 rooted provenance records", "translation allocates",
                       "same-object-file calls bypass GNU ld --wrap",
                       "raw REPL is linked but current OS entry only calls friendly REPL"]}


def nano_events() -> list[tuple[int, int]]:
    """Independent input and expected bytes; no editor helpers build either."""
    events = [(2, 0x3131), (2, 0xD55C), (2, 0), (1, 0xD55C),
              (2, 0xAE00), (1, 0xAE00), (5, 1), (4, (20 << 16) | 60)]
    # Undo, redo, search, and replace through actual upstream key bindings.
    for keys, checkpoint in (("\x1bu", 2), ("\x1be", 3), ("\x17한\r", 4),
                             ("\x1c한\r가\ry", 5), ("\x0f\r\x18", 0)):
        events.extend((1, ord(char)) for char in keys)
        if checkpoint:
            events.append((5, checkpoint))
    return events


def nano_run(executable: Path, folder: Path) -> None:
    events = folder / "nano-events"
    events.write_text("".join(f"{kind} {payload:x}\n" for kind, payload in nano_events()))
    target = folder / "한글-edit"
    target.write_bytes(b"")
    trace = folder / "nano-curses-trace.log"
    master, slave = pty.openpty()
    try:
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 25, 80, 0, 0))
        environment = {**os.environ, "P0_EVENTS": str(events), "P0_TRACE": str(trace),
                       "LC_ALL": "C.UTF-8", "TERM": "xterm"}
        process = subprocess.Popen([str(executable), str(target)], cwd=folder,
                                   stdin=slave, stdout=slave, stderr=slave,
                                   env=environment, start_new_session=True)
        try:
            process.wait(timeout=20)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
        os.set_blocking(master, False)
        terminal = b""
        while True:
            try:
                part = os.read(master, 65536)
            except (BlockingIOError, OSError):
                break
            if not part:
                break
            terminal += part
        (folder / "nano-terminal.log").write_bytes(terminal)
        if process.returncode != 0:
            raise RuntimeError(f"nano exit {process.returncode}: {terminal!r}")
        if target.read_bytes() != "가글\n".encode():
            raise RuntimeError(f"unexpected nano save: {target.read_bytes()!r}")
        if "PASS " not in trace.read_text():
            raise RuntimeError(f"curses fixture did not pass: {trace.read_text()}")
    finally:
        os.close(master)
        os.close(slave)


def nano(archive: Path, folder: Path, cc: str) -> dict[str, object]:
    actual_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
    if actual_hash != NANO_SHA256:
        raise ValueError(f"nano archive hash mismatch: {actual_hash}")
    source = folder / f"nano-{NANO_VERSION}"
    if not source.exists():
        with tarfile.open(archive) as bundle:
            bundle.extractall(folder, filter="data")
    build = folder / "nano-build"
    build.mkdir(exist_ok=True)
    run([str(source / "configure"), *NANO_OPTIONS, f"CC={cc}"], build,
        folder / "nano-configure.log")
    run(["make", "-j4"], build, folder / "nano-build.log")
    objects = sorted((build / "src").glob("*.o"))
    combined = folder / "nano-core.o"
    run(["ld", "-r", *map(str, objects), "-o", str(combined)], ROOT,
        folder / "nano-inventory-link.log")
    dependencies = symbols(combined)
    (folder / "nano-symbols.json").write_text(json.dumps(dependencies, indent=2) + "\n")
    executable = folder / "nano-curses-probe64"
    run([cc, "-O2", "-g", "-std=gnu11", *WARNINGS, "-DHAVE_CONFIG_H",
         "-isystem", str(build), "-isystem", str(source / "src"),
         "-idirafter", str(ROOT / "64bit" / "src64" / "include"),
         str(PROBES / "nano_curses_probe64.c"),
         str(ROOT / "64bit" / "src64" / "lib" / "utf864.c"),
         *map(str, objects), str(build / "lib" / "libgnu.a"), "-o", str(executable)],
        ROOT, folder / "nano-probe-link.log")
    nano_run(executable, folder)
    return {"version": NANO_VERSION, "source": NANO_URL, "sha256": actual_hash,
            "configure": NANO_OPTIONS, "undefined_symbols": dependencies,
            "host_editor_probe": "PASS", "cross_link": "NOT RUN",
            "os_tty_runtime": "NOT RUN", "libc": "host glibc + bundled gnulib",
            "curses": "first-party cell/event model; no ncurses linked",
            "preserved_upstream_files": ["COPYING", "COPYING.DOC", "AUTHORS", "ChangeLog", "NEWS"],
            "limits": ["--disable-multibuffer also removes built-in help",
                       "POSIX and regex remain required", "no 64KiB bound yet",
                       "scripted host events are not OS TTY calls"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="official nano-9.3.tar.xz (pinned SHA-256)")
    parser.add_argument("--only", choices=("nano", "micropython", "all"), default="all")
    args = parser.parse_args()
    if args.only != "micropython" and args.archive is None:
        parser.error("--archive is required for nano; see docs/feature_p0.md")
    folder = ROOT / "build64" / "feature-p0"
    folder.mkdir(parents=True, exist_ok=True)
    cc = os.environ.get("HOST_CC", "cc")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                       text=True).strip()
    report: dict[str, object] = {
        "scope": "host feasibility, not OS acceptance",
        "source_commit": revision,
        "changes": "uncommitted P0 probes; see per-file SHA-256",
        "probe_sha256": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in (Path(__file__), *sorted(PROBES.glob("*.c")))},
    }
    report["status"] = "RUNNING"
    destination = folder / "report.json"
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    try:
        if args.only in ("nano", "all"):
            report["nano"] = nano(args.archive.resolve(), folder, cc)
        if args.only in ("micropython", "all"):
            report["micropython"] = micropython(folder, cc)
    except Exception as error:
        report["status"] = "FAIL"
        report["error"] = str(error)
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        raise
    report["status"] = "HOST PASS; P0 INCOMPLETE"
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(f"P0 host probes: PASS; evidence: {folder / 'report.json'}")
    print("P0 remains incomplete until both OS app links and actual TTY runtime pass.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
