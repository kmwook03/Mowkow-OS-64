#!/usr/bin/env python3
"""Check guest shutdown and persistent files on a disposable IDE/AHCI image."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from app_regression64 import check_app
from mowkow_parity import Console, DEFAULT_IMAGE, PROMPT


class ShutdownConsole(Console):
    def shutdown(self, command: str) -> None:
        before = len(self.text())
        self.type(command)
        if self.connection is None or self.proc is None:
            raise RuntimeError("shutdown requires a connected QEMU process")
        # Poweroff can close QMP before its Enter response. Submit that last key
        # without waiting for a reply; the owned process must exit by itself.
        self.request_id += 1
        events = [{"type": "key", "data": {"down": down,
                   "key": {"type": "qcode", "data": "ret"}}} for down in (True, False)]
        request = {"execute": "input-send-event", "id": self.request_id,
                   "arguments": {"events": events}}
        self.connection.sendall((json.dumps(request) + "\n").encode())
        try:
            status = self.proc.wait(timeout=15)
        except subprocess.TimeoutExpired as error:
            raise AssertionError(f"guest shutdown timed out: {self.log_path}") from error
        result = self.text()[before:]
        if (status != 0 or result.count("시스템 종료: 저장장치를 동기화합니다.\n") != 1 or
                result.count("저장 완료. 시스템 전원을 끕니다.\n") != 1 or
                "실패" in result or result.endswith(PROMPT)):
            raise AssertionError(f"invalid guest shutdown ({status}): {result!r}")


def run(image: Path, ahci: bool, log: Path) -> None:
    with ShutdownConsole(str(image), ahci, str(log) + "-write.log") as console:
        help_text = console.run("help")
        if "shutdown 시스템종료" not in help_text:
            raise AssertionError("shutdown commands missing from help")
        check_app(console.run("wtest HUGE"), "wtest")
        console.shutdown("shutdown")
    print("English command: ordered save and guest poweroff PASS", flush=True)
    # A fresh VM discards every kernel cache. wtest checks exact data, size and EOF.
    with ShutdownConsole(str(image), ahci, str(log) + "-reboot.log") as console:
        check_app(console.run("wtest --verify-huge"), "wtest")
        console.shutdown("시스템종료")
    print("Reboot persistence and Korean command guest poweroff PASS", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, default=Path(DEFAULT_IMAGE))
    parser.add_argument("--ahci", action="store_true")
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    log = args.log or Path(tempfile.mkdtemp(prefix="mowkow-shutdown64-")) / "serial"
    log.parent.mkdir(parents=True, exist_ok=True)
    print(f"shutdown logs: {log}", flush=True)
    with tempfile.TemporaryDirectory(prefix="shutdown-regression64-") as temporary:
        image = Path(temporary) / "disk.img"
        shutil.copyfile(args.image, image)
        run(image, args.ahci, log)


if __name__ == "__main__":
    main()
