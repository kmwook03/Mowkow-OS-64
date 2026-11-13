#!/usr/bin/env python3
"""Exercise private processes, user-copy boundaries and isolated protection faults."""
from __future__ import annotations

import argparse
import mmap
import re
import shutil
import tempfile
import time
from pathlib import Path

from mowkow_parity import Console, DEFAULT_IMAGE, PROMPT


class ProcessConsole(Console):
    def wait_seen(self, offset: int, marker: str, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._check_alive()
            if marker in self.text()[offset:]:
                return
            time.sleep(0.1)
        raise TimeoutError(f"missing {marker!r}; see {self.log_path}")

    def move(self, x: int, y: int) -> None:
        self._qmp({"execute": "input-send-event", "arguments": {"events": [
            {"type": "rel", "data": {"axis": "x", "value": x}},
            {"type": "rel", "data": {"axis": "y", "value": y}},
        ]}})
        time.sleep(0.02)

    def focus(self, index: int) -> None:
        # Clamp at the lower-left corner, independent of the current pointer.
        for _ in range(20):
            self.move(-100, 100)
        x = 24 + index * 132
        while x:
            step = min(x, 100)
            self.move(step, 0)
            x -= step
        self.move(0, -14)
        for down in (True, False):
            self._qmp({"execute": "input-send-event", "arguments": {"events": [
                {"type": "btn", "data": {"down": down, "button": "left"}},
            ]}})
            time.sleep(0.08)
        self.hangul = self.modes[index]

    def launch(self, index: int, suffix: str = "") -> int:
        self.focus(index)
        before = len(self.text())
        self.type(f"isotest {index + 1}{suffix}")
        self.modes[index] = self.hangul
        self._send(["ret"])
        self.wait_seen(before, f"isotest {index + 1}: READY\n", 20)
        return before

    def finish(self, index: int) -> None:
        self.focus(index)
        before = len(self.text())
        self._send(["q"])
        self._wait_after(before, PROMPT, 20, "process exit")
        output = self.text()[before:]
        if f"isotest {index + 1}: PASS\n" not in output or "종료 코드: 0\n" not in output:
            raise AssertionError(f"process {index + 1} did not complete: {output!r}")

    def peer_backing(self, identifier: int) -> int:
        # Read-only external observation: locate the survivor's full 16-KiB
        # private heap cookie in physical RAM. No kernel ABI or address leak.
        # This pattern is never written to a file or stdout by isotest.
        with tempfile.TemporaryDirectory(prefix="process-ram64-") as temporary:
            dump = Path(temporary) / "ram.bin"
            self._qmp({"execute": "pmemsave", "arguments": {
                "val": 0x800000, "size": 0x1f800000, "filename": str(dump)}})
            pattern = bytes((identifier + i) & 255 for i in range(16384))
            with dump.open("rb") as stream, mmap.mmap(
                    stream.fileno(), 0, access=mmap.ACCESS_READ) as memory:
                offset = memory.find(pattern)
                if offset < 0 or memory.find(pattern, offset + 1) >= 0:
                    raise AssertionError("survivor heap backing was not unique")
            return 0x800000 + offset

    def protections(self) -> None:
        # Slot 0 is free, 1/2/3 retain their private data and blocked contexts.
        peer = self.peer_backing(2)
        print(f"survivor 2 heap backing: {peer:#x}", flush=True)
        # Keep one survivor actively checking data/CPU state throughout faults.
        self.finish(3)
        live = self.launch(3, " live")
        self.wait_seen(live, "isotest 4: LIVE\n", 20)
        cases = {
            "kernel-read": 5, "kernel-write": 7, "peer-read": 5,
            "peer-write": 7, "code-write": 7, "rodata-write": 7,
            "heap-exec": 21, "stack-exec": 21, "guard-low": 6,
            "guard-high": 6, "unmapped": 6,
        }
        for case, error in cases.items():
            self.focus(0)
            before = len(self.text())
            self.type(f"faulttest {case} {peer:x}")
            self.modes[0] = self.hangul
            self._send(["ret"])
            self._wait_after(before, PROMPT, 20, case)
            output = self.text()[before:]
            if ("faulttest: READY\n" not in output or "FAIL" in output or
                    "종료 코드: 142\n" not in output or
                    f"USER EXCEPTION vector=000000000000000e error={error:016x}" not in output):
                raise AssertionError(f"{case} fault mismatch: {output!r}")
            if case.startswith("peer-"):
                if f"cr2={peer:016x}" not in output:
                    raise AssertionError("fault did not target survivor backing")
            after = len(self.text())
            self.wait_seen(after, "isotest 4: LIVE\n", 20)
            print(f"{case}: isolated fault, live survivor PASS", flush=True)
        self.focus(0)
        output = self.run(f"faulttest pointers {peer:x}")
        self.modes[0] = self.hangul
        if "faulttest: PASS pointers" not in output or "종료 코드: 0" not in output:
            raise AssertionError(f"pointer probe failed: {output!r}")
        if re.search(r"FAIL|KERNEL PANIC|kernel exception halted", self.text()[live:]):
            raise AssertionError("survivor or kernel failed during protection probes")
        before = len(self.text())
        self.type("faulttest input")
        self._send(["ret"])
        self.wait_seen(before, "faulttest: INPUT\n", 20)
        self.wait_seen(len(self.text()), "isotest 4: LIVE\n", 20)
        self.type("ok")
        self._send(["ret"])
        self._wait_after(before, PROMPT, 20, "blocked copyout")
        output = self.text()[before:]
        if "faulttest: PASS input\n" not in output or "종료 코드: 0\n" not in output:
            raise AssertionError(f"blocked read failed: {output!r}")
        self.finish(1)
        self.finish(2)
        self.run("help")
        print("syscall pointers, survivor data and GUI focus: PASS", flush=True)
        # The live CPU task deliberately remains running; Console owns QEMU.

    def exercise(self) -> None:
        self.run("window")
        self.modes = [False, True, True, True]
        for index in range(3):
            before = len(self.text())
            self.type("new")
            self._send(["ret"])
            self.wait_seen(before, "> > ", 10)
            self.modes[index] = False
            self.hangul = True
        start = len(self.text())
        for index in range(4):
            self.launch(index)
        if ": WAIT\n" in self.text()[start:]:
            raise AssertionError("CPU probes did not overlap; increase their duration")
        for index in range(4):
            self.wait_seen(start, f"isotest {index + 1}: WAIT\n", 70)
        if "FAIL" in self.text()[start:] or "EXCEPTION" in self.text()[start:]:
            raise AssertionError(self.text()[start:])
        print("four simultaneous image/heap/stack and CPU/FP contexts: PASS", flush=True)
        # Other tasks remain suspended in TTY input while one returns and reuses a slot.
        self.finish(0)
        again = self.launch(0, " short")
        self.wait_seen(again, "isotest 1: WAIT\n", 20)
        self.finish(0)
        fault = self.launch(0, " fault")
        self._wait_after(fault, PROMPT, 20, "user fault return")
        if "종료 코드: 134\n" not in self.text()[fault:]:
            raise AssertionError("UD2 did not exit only its owning process")
        self.protections()
        print("blocking resume, slot reuse, user fault and survivor contexts: PASS", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", default=DEFAULT_IMAGE)
    parser.add_argument("--ahci", action="store_true")
    parser.add_argument("--log", default=f"/tmp/mowkow-process-{time.time_ns()}.log")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="process-regression64-") as temporary:
        image = Path(temporary) / "disk.img"
        shutil.copyfile(args.image, image)
        with ProcessConsole(str(image), args.ahci, args.log) as console:
            console.exercise()


if __name__ == "__main__":
    main()
