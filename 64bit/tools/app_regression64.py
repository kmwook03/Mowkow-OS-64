#!/usr/bin/env python3
"""Run parity and app smoke tests on a disposable IDE/AHCI image copy.

ktest --auto receives a, Left and Ctrl-X after READY. wtest HUGE writes three
files, then --verify-huge checks their full contents after a fresh QEMU boot.
This is a bounded smoke suite, not comprehensive app/storage coverage.
"""
from __future__ import annotations

import argparse
import shutil
import sys
import tempfile
from pathlib import Path

import mowkow_parity
from mowkow_parity import CMD_TIMEOUT, DEFAULT_IMAGE, PROMPT, Console


class AppConsole(Console):
    """App assertions over the shared parity/QMP lifecycle and input path."""

    def run_ktest(self) -> str:
        before = len(self.text())
        self.type("ktest --auto")
        self._send(["ret"])
        self._wait_after(before, "ktest: READY\n", CMD_TIMEOUT, "ktest READY")
        self._send(["a"])
        self._send(["left"])
        self._send(["ctrl", "x"])
        self._wait_after(before, PROMPT, CMD_TIMEOUT, "ktest exit")
        return self.text()[before:]


def check_app(raw: str, name: str, status: int = 0) -> None:
    """Require one exact result and one real process exit; reject stale markers."""
    lines = [line.strip() for line in raw.splitlines()]
    expected = f"{name}: {'PASS' if status == 0 else 'FAIL usage'}"
    results = [line for line in lines if line == f"{name}: PASS" or
               line.startswith(f"{name}: FAIL")]
    exits = [line for line in lines if line.startswith("exit ")]
    if results != [expected] or exits != [f"exit {status}"] or not raw.endswith(PROMPT):
        raise AssertionError(f"{name}: invalid result/exit/prompt: {raw!r}")


def run_apps(image: str, ahci: bool, log: str) -> None:
    with AppConsole(image, ahci, log + "-apps.log") as console:
        check_app(console.run_ktest(), "ktest")
        print("ktest-auto ok", flush=True)
        help_output = console.run("help")
        if "commands: help" not in help_output:
            raise AssertionError("cooked help did not return after raw ktest")
        print("ktest-cooked ok", flush=True)
        check_app(console.run("ktest --bad"), "ktest", 1)
        print("ktest-failure ok", flush=True)
        for _ in range(2):
            check_app(console.run("mtest"), "mtest")
        print("mtest/reentry ok", flush=True)
        check_app(console.run("wtest HUGE"), "wtest")
        print("wtest-huge ok", flush=True)
    # A new process boots the same disk, losing all kernel caches and app state.
    with AppConsole(image, ahci, log + "-reboot.log") as console:
        check_app(console.run("wtest --verify-huge"), "wtest")
        print("wtest-persistence ok", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", default=DEFAULT_IMAGE)
    parser.add_argument("--ahci", action="store_true")
    parser.add_argument("--log", help="serial prefix (default: unique directory under /tmp)")
    args = parser.parse_args()
    kind = "ahci" if args.ahci else "ide"
    log = args.log or str(Path(tempfile.mkdtemp(prefix=f"mowkow-regression-{kind}-")) / "serial")
    print(f"regression log prefix: {log}", flush=True)
    with tempfile.TemporaryDirectory(prefix="app-regression64-") as temporary:
        image = str(Path(temporary) / "disk.img")
        shutil.copyfile(args.image, image)
        parity = ["--image", image, "--log", log + "-parity.log"]
        if args.ahci:
            parity.append("--ahci")
        # Keep the existing 12 parity checks in the regression target.
        if mowkow_parity.main(parity, console_factory=AppConsole) != 0:
            return 1
        run_apps(image, args.ahci, log)
    return 0


if __name__ == "__main__":
    sys.exit(main())
