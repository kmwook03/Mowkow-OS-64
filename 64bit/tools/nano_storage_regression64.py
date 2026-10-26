#!/usr/bin/env python3
"""Exercise private upstream nano storage on disposable IDE/AHCI images.

Requires built x86_64 and nano64-apps artifacts, QEMU and mtools mcopy. The
normal image/editor are unchanged. Logs survive; temporary images are removed.
"""
from __future__ import annotations

import argparse
import subprocess
import tempfile
import time
from pathlib import Path

import mkfat32_64
from mowkow_parity import CMD_TIMEOUT, PROMPT, Console

ROOT = Path(__file__).resolve().parents[2]


class NanoConsole(Console):
    def wait_text(self, offset: int, fragment: str) -> None:
        deadline = time.monotonic() + CMD_TIMEOUT
        while time.monotonic() < deadline:
            self._check_alive()
            if fragment in self.text()[offset:]:
                return
            time.sleep(0.1)
        raise TimeoutError(f"nano: missing {fragment!r}; see {self.log_path}")

    def edit(self, filename: str, insertion: str = "", end: bool = False,
             extra_options: str = "") -> None:
        before = len(self.text())
        self.type(f"nano9 {extra_options}{filename}")
        self._send(["ret"])
        self.wait_text(before, "^X Exit")
        if end:
            self._send(["ctrl", "e"])
        if insertion:
            self.type(insertion)
        before_save = len(self.text())
        self._send(["ctrl", "o"])
        self.wait_text(before_save, "Write to File")
        self._send(["ret"])
        self.wait_text(before_save, "Wrote ")
        self._send(["ctrl", "x"])
        self._wait_after(before, PROMPT, CMD_TIMEOUT, "nano exit")
        result = self.text()[before:]
        if result.count("exit 0\n") != 1 or "exit 1\n" in result:
            raise AssertionError(f"nano did not exit successfully: {self.log_path}")
        if "commands: help" not in self.run("help"):
            raise AssertionError("cooked console did not recover")

    def oversized_save(self) -> None:
        before = len(self.text())
        self.type("nano9 EXACT.TXT")
        self._send(["ret"])
        self.wait_text(before, "^X Exit")
        self._send(["ctrl", "e"])
        self.type("x")
        before_save = len(self.text())
        self._send(["ctrl", "o"])
        self.wait_text(before_save, "Write to File")
        self._send(["ret"])
        self.wait_text(before_save, "Save exceeds 64 KiB; file was not changed")
        self._send(["ctrl", "x"])
        self.wait_text(before_save, "Save modified buffer?")
        self.type("n")
        self._wait_after(before, PROMPT, CMD_TIMEOUT, "oversized save discard")
        if self.text()[before:].count("exit 0\n") != 1:
            raise AssertionError("oversized save/discard did not exit 0")


def verify(image: Path, filename: str, expected: bytes, folder: Path) -> None:
    destination = folder / "readback"
    subprocess.run(["mcopy", "-o", "-i", str(image), f"::{filename}", str(destination)],
                   check=True, timeout=30, capture_output=True)
    if destination.read_bytes() != expected:
        raise AssertionError(f"{filename}: complete bytes/size/EOF mismatch")


def run(ahci: bool, log: Path) -> None:
    log.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nano-storage64-") as temporary:
        folder = Path(temporary)
        fixtures = {"SEED.TXT": "시작\n".encode(), "DOS.TXT": b"one\r\ntwo\r\n",
                    "NOEND.TXT": b"tail", "EMPTY.TXT": b"", "ALIAS.TXT": b"keep\n",
                    "EXACT.TXT": b"a" * 65535 + b"\n", "LARGE.TXT": b"b" * 65537}
        specs = [f"nano9={ROOT / 'build64/nano-app/x86_64/nano.elf'}",
                 f"cat={ROOT / 'build64/app/cat/cat.elf'}"]
        for name, content in fixtures.items():
            (folder / name).write_bytes(content)
            specs.append(f"{name}={folder / name}")
        image = folder / "disk.img"
        mkfat32_64.build(image, (ROOT / "build64/boot/boot64.bin").read_bytes(),
                        (ROOT / "build64/boot/loader64.bin").read_bytes(),
                        (ROOT / "build64/kernel/kernel64.bin").read_bytes(),
                        (ROOT / "common/font/H04.FNT").read_bytes(), specs)
        with NanoConsole(str(image), ahci, str(log) + "-edit.log") as console:
            console.edit("NEW.TXT", "한글")
            console.edit("ALIAS.TXT.EXTRA", "x")
            console.edit("SEED.TXT", " 한글", end=True)
            console.edit("DOS.TXT")
            console.edit("NOEND.TXT", extra_options="-L ")
            console.edit("EMPTY.TXT")
            console.edit("EXACT.TXT")
            console.oversized_save()
            before = len(console.text())
            console.type("nano9 LARGE.TXT")
            console._send(["ret"])
            console.wait_text(before, "Only regular files up to 64 KiB can be opened")
            console._send(["ctrl", "x"])
            console._wait_after(before, PROMPT, CMD_TIMEOUT, "oversized open exit")
        expected = {**fixtures, "NEW.TXT": "한글\n".encode(),
                    "SEED.TXT": "시작 한글\n".encode(), "ALIAS.TXT.EXTRA": b"x\n"}
        for name, content in expected.items():
            verify(image, name, content, folder)
        print("nano create/edit/DOS/64KiB/open-limit and exact disk readback: PASS", flush=True)
        with NanoConsole(str(image), ahci, str(log) + "-reboot.log") as console:
            console.edit("NEW.TXT")
            result = console.run("cat NEW.TXT")
            if "한글\n" not in result or "exit 0\n" not in result:
                raise AssertionError("reboot file contents or cat exit mismatch")
        for name, content in expected.items():
            verify(image, name, content, folder)
        print("nano reboot/reopen/save/cooked recovery and full bytes/EOF: PASS", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ahci", action="store_true")
    parser.add_argument("--log", type=Path)
    args = parser.parse_args()
    log = args.log or Path(tempfile.mkdtemp(prefix="mowkow-nano-storage64-")) / "serial"
    print(f"nano storage logs: {log}", flush=True)
    run(args.ahci, log)


if __name__ == "__main__":
    main()
