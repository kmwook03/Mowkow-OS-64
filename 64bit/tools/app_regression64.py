#!/usr/bin/env python3
"""Run parity and app smoke tests on a disposable IDE/AHCI image copy.

ktest --auto receives a, Left and Ctrl-X after READY. wtest HUGE writes three
files, then --verify-huge checks their full contents after a fresh QEMU boot.
This is a bounded smoke suite, not comprehensive app/storage coverage.
"""
from __future__ import annotations

import argparse
import json
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import mowkow_parity
from mowkow_parity import BOOT_TIMEOUT, CMD_TIMEOUT, DEFAULT_IMAGE, PROMPT, Console


class AppConsole(Console):
    """Reuse parity keyboard encoding with a private, bounded QMP session."""

    def __init__(self, image: str, ahci: bool, log_path: str) -> None:
        self.log_path = log_path
        self.proc = None
        self.qmp = None
        self.connection = None
        self.temporary = tempfile.TemporaryDirectory(prefix="app-qmp64-")
        self.stderr = None
        try:
            endpoint = str(Path(self.temporary.name) / "qmp.sock")
            disk = (["-machine", "q35", "-drive",
                     f"file={image},format=raw,if=none,id=disk0",
                     "-device", "ich9-ahci,id=ahci",
                     "-device", "ide-hd,drive=disk0,bus=ahci.0"] if ahci else
                    ["-drive", f"file={image},format=raw,if=ide"])
            Path(log_path).write_bytes(b"")
            self.stderr = open(log_path + ".qemu.log", "wb")
            self.proc = subprocess.Popen(
                ["qemu-system-x86_64", *disk, "-boot", "c", "-m", "512M",
                 "-display", "none", "-serial", f"file:{log_path}",
                 "-qmp", f"unix:{endpoint},server,nowait"],
                stdout=subprocess.DEVNULL, stderr=self.stderr,
            )
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                connection.settimeout(5)
                try:
                    connection.connect(endpoint)
                except OSError:
                    connection.close()
                    if self.proc.poll() is not None:
                        raise RuntimeError(f"QEMU exited; see {log_path}.qemu.log")
                    time.sleep(0.1)
                    continue
                self.connection = connection
                self.qmp = connection.makefile("rw", encoding="utf-8")
                break
            if self.qmp is None:
                raise TimeoutError("QMP connection timeout")
            json.loads(self.qmp.readline())
            self._qmp({"execute": "qmp_capabilities"})
            self.hangul = True
            self._wait(PROMPT, BOOT_TIMEOUT, "boot")
        except BaseException:
            self.close()
            raise

    def _qmp(self, message: dict) -> dict:
        self.qmp.write(json.dumps(message) + "\n")
        self.qmp.flush()
        while True:
            line = self.qmp.readline()
            if not line:
                raise RuntimeError("QMP disconnected")
            response = json.loads(line)
            if "error" in response:
                raise RuntimeError(f"QMP error: {response['error']}")
            if "return" in response:
                return response

    def _send(self, keys: list[str]) -> None:
        self._qmp({"execute": "send-key", "arguments": {
            "keys": [{"type": "qcode", "data": key} for key in keys],
            "hold-time": 20,
        }})
        time.sleep(0.03)

    def _wait(self, tail: str, timeout: float, what: str) -> None:
        self._wait_after(-1, tail, timeout, what)

    def _wait_after(self, offset: int, tail: str, timeout: float, what: str) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            current = self.text()
            if len(current) > offset and current.endswith(tail):
                return
            if self.proc.poll() is not None:
                raise RuntimeError(f"QEMU exited during {what}")
            time.sleep(0.1)
        raise TimeoutError(f"timeout during {what}; see {self.log_path}")

    def run(self, command: str, inputs: tuple | list = ()) -> str:
        before = len(self.text())
        self.type(command)
        self._send(["ret"])
        for line in inputs:
            time.sleep(1)
            self.type(line)
            self._send(["ret"])
        self._wait_after(before, PROMPT, CMD_TIMEOUT, command)
        return self.text()[before:]

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

    def close(self) -> None:
        try:
            if self.proc is not None and self.proc.poll() is None:
                self.proc.terminate()
                try:
                    self.proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait(timeout=5)
        finally:
            if self.qmp is not None:
                self.qmp.close()
            if self.connection is not None:
                self.connection.close()
            if self.stderr is not None:
                self.stderr.close()
            self.temporary.cleanup()


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
    console = AppConsole(image, ahci, log + "-apps.log")
    try:
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
    finally:
        console.close()
    # A new process boots the same disk, losing all kernel caches and app state.
    console = AppConsole(image, ahci, log + "-reboot.log")
    try:
        check_app(console.run("wtest --verify-huge"), "wtest")
        print("wtest-persistence ok", flush=True)
    finally:
        console.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", default=DEFAULT_IMAGE)
    parser.add_argument("--ahci", action="store_true")
    parser.add_argument("--log", help="serial prefix (default: /tmp/mowkow-regression-{ide,ahci})")
    args = parser.parse_args()
    log = args.log or f"/tmp/mowkow-regression-{'ahci' if args.ahci else 'ide'}"
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
