#!/usr/bin/env python3
"""Check console/app/Mowkow languages on a disposable IDE/AHCI disk copy."""

from __future__ import annotations

import argparse
import shutil
import tempfile
from pathlib import Path

from mowkow_parity import CMD_TIMEOUT, DEFAULT_IMAGE, MOWKOW_PROMPT, PROMPT, Console


class MessageConsole64(Console):
    def run_mode(self, command: str, korean: bool) -> str:
        before = len(self.text())
        self.type(command)
        self._mode(korean)
        self._send(["ret"])
        self._wait_after(before, PROMPT, CMD_TIMEOUT, command)
        return self.text()[before:]

    def expression(self, source: str, korean: bool) -> str:
        before = len(self.text())
        self.type(source)
        self._mode(korean)
        self._send(["ret"])
        self._wait_after(before, MOWKOW_PROMPT, CMD_TIMEOUT, source)
        return self.text()[before:]


def require(raw: str, expected: str) -> None:
    if expected not in raw:
        raise AssertionError(f"missing {expected!r} in {raw!r}")


def check(image: str, ahci: bool, log: str, os_only: bool = False) -> None:
    with MessageConsole64(image, ahci, log) as console:
        for korean in (True, False):
            for command, expected in (
                    ("help", "명령: help"),
                    ("hello", "64비트 앱에서 인사합니다.\n종료 코드: 0\n"),
                    ("missingcmd", "알 수 없는 명령어"),
                    ("run 없는파일", "파일을 찾을 수 없습니다."),
                    ("cat 없는파일", "파일을 열 수 없습니다.\n종료 코드: 1\n" if korean
                     else "open failed\n종료 코드: 1\n"),
                    ("mem", "사용 가능한 메모리: "),
                    ("tasks", "태스크 전환 횟수: "),
                    ("ticks", "타이머 틱: "),
                    ("ktest --bad", "ktest: FAIL usage\n종료 코드: 1\n")):
                require(console.run_mode(command, korean), expected)
            if os_only:
                print(f"{'Korean' if korean else 'English'} input: OS messages ok", flush=True)
                continue
            before = len(console.text())
            console.type("머꼬")
            console._send(["ret"])
            console._wait_after(before, MOWKOW_PROMPT, CMD_TIMEOUT, "Mowkow start")
            for source, expected in (
                    ("없는이름", "오류: 이름 '없는이름'을(를) 찾을 수 없습니다." if korean
                     else "error: name '없는이름' is not defined"),
                    ('(+ "한글" 1)', "오류: 함수 <내장함수 '+'>: 타입 오류입니다." if korean
                     else "error: function <builtin function '+'>: type error"),
                    (")", "구문 오류: ')'" if korean else "syntax error: ')'"),
                    ('"invalid syntax"', '\n"invalid syntax"\n'),
                    ('"abc', "오류: 토큰" if korean else "error: lexical error near token")):
                require(console.expression(source, korean), expected)
            before = len(console.text())
            console._send(["ret"])
            console._wait_after(before, PROMPT, CMD_TIMEOUT, "Mowkow exit")
            require(console.text()[before:], "머꼬'를 사용해 주셔서 고맙습니다.")
            print(f"{'Korean' if korean else 'English'}: console/app/Mowkow messages ok",
                  flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", default=DEFAULT_IMAGE)
    parser.add_argument("--ahci", action="store_true")
    parser.add_argument("--log", required=True, help="serial log path")
    parser.add_argument("--os-only", action="store_true", help="check OS commands and app exits")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="messages64-") as temporary:
        image = str(Path(temporary) / "disk.img")
        shutil.copyfile(args.image, image)
        check(image, args.ahci, args.log, args.os_only)


if __name__ == "__main__":
    main()
