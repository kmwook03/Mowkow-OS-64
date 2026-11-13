#!/usr/bin/env python3
"""Exercise four private processes through real GUI focus and blocking input."""
from __future__ import annotations

import argparse
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
        for index in (3, 1, 2):
            self.finish(index)
        self.run("help")
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
