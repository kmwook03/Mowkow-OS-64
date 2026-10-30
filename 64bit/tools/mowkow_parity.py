#!/usr/bin/env python3
"""
머꼬 병행 검사 -- 이미지 안의 머꼬와 호스트 CPython의 업스트림 머꼬를
같은 입력으로 돌려 견준다 (mowkow_porting.md 결정 9, 시험 계획).

    python3 tools/mowkow_parity.py [--ahci] [--image IMG] [--case 이름]

QEMU를 화면 없이 띄우고, QMP key event로 콘솔에 명령을 쳐 넣고, COM1으로
나온 것을 받는다. 콘솔은 한글 입력으로 뜨므로 한글은 두벌식 자판 순서로
친다(TYPE 표). 그래서 `머꼬 add.mk`도, `읽기`에 넣는 `가나`도 사람이 치는
길과 같은 길로 들어간다.

견주는 규칙(결정 9: 글자 그대로가 아니라 행동으로):
  - 명령을 친 줄과 다음 `> ` 프롬프트 사이만 본다 (배너와 부팅 로그는 빠진다)
  - 빈 줄은 양쪽에서 버린다 (한글 조합 중인 글자는 COM1에 나오지 않는다)
  - 장치 쪽에 되울린 입력 줄은 순서대로 하나씩 버린다 (호스트는 되울리지 않는다)
그 나머지 -- 값과 오류의 차례 -- 가 같아야 한다.
"""
from __future__ import annotations

import argparse
import errno
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UPSTREAM = os.path.join(ROOT, "64bit", "py64", "머꼬", "upstream")
DEFAULT_IMAGE = os.path.join(ROOT, "img64", "mowkow64.img")

PROMPT = "> "
MOWKOW_PROMPT = ">> "
REFERENCE_MOWKOW_PROMPT = ">  "
MOWKOW_CONTINUATION = ".. "
BOOT_TIMEOUT = 60
CMD_TIMEOUT = 180
QMP_TIMEOUT = 5
CONNECT_TIMEOUT = 10
QMP_MESSAGE_MAX = 1024 * 1024

# -- 두벌식 자판. 한글을 사람이 치는 그대로 자판 순서로 바꾼다. --
LEAD = "ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ"
VOWEL = "ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ"
TAIL = " ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ"
JAMO_KEYS = {
    "ㄱ": "r", "ㄲ": "R", "ㄴ": "s", "ㄷ": "e", "ㄸ": "E", "ㄹ": "f", "ㅁ": "a",
    "ㅂ": "q", "ㅃ": "Q", "ㅅ": "t", "ㅆ": "T", "ㅇ": "d", "ㅈ": "w", "ㅉ": "W",
    "ㅊ": "c", "ㅋ": "z", "ㅌ": "x", "ㅍ": "v", "ㅎ": "g",
    "ㅏ": "k", "ㅐ": "o", "ㅑ": "i", "ㅒ": "O", "ㅓ": "j", "ㅔ": "p", "ㅕ": "u",
    "ㅖ": "P", "ㅗ": "h", "ㅘ": "hk", "ㅙ": "ho", "ㅚ": "hl", "ㅛ": "y",
    "ㅜ": "n", "ㅝ": "nj", "ㅞ": "np", "ㅟ": "nl", "ㅠ": "b", "ㅡ": "m",
    "ㅢ": "ml", "ㅣ": "l",
    "ㄳ": "rt", "ㄵ": "sw", "ㄶ": "sg", "ㄺ": "fr", "ㄻ": "fa", "ㄼ": "fq",
    "ㄽ": "ft", "ㄾ": "fx", "ㄿ": "fv", "ㅀ": "fg", "ㅄ": "qt",
}
SHIFTED = {"(": "shift-9", ")": "shift-0", "+": "shift-equal", "*": "shift-8",
           "?": "shift-slash", "!": "shift-1", "_": "shift-minus",
           '"': "shift-apostrophe", "<": "shift-comma", ">": "shift-dot"}
PLAIN = {" ": "spc", ".": "dot", ",": "comma", "-": "minus", "/": "slash",
         "'": "apostrophe", "=": "equal", ";": "semicolon"}


def hangul_keys(ch):
    """한글 글자 하나를 두벌식 자판 순서로. 한글이 아니면 None."""
    code = ord(ch)
    if 0xAC00 <= code <= 0xD7A3:
        index = code - 0xAC00
        jamo = (LEAD[index // 588], VOWEL[(index % 588) // 28], TAIL[index % 28])
        return "".join(JAMO_KEYS[j] for j in jamo if j != " ")
    if ch in JAMO_KEYS:             # 호환 자모 하나 (0육ㄱ의 ㄱ 같은 것)
        return JAMO_KEYS[ch]
    return None


class Console:
    """One owned QEMU process and a private, bounded, synchronous QMP session."""

    def __init__(self, image: str, ahci: bool, log_path: str | None = None) -> None:
        self.log_path = log_path
        self.proc = None
        self.connection = None
        self.stderr = None
        self.temporary = None
        self.received = bytearray()
        self.request_id = 0
        try:
            self.temporary = tempfile.TemporaryDirectory(prefix="mowkow-qmp64-")
            if self.log_path is None:
                fd, self.log_path = tempfile.mkstemp(prefix="mowkow-parity-", suffix=".log")
                os.close(fd)
            Path(self.log_path).write_bytes(b"")
            self.stderr = open(self.log_path + ".qemu.log", "wb")
            endpoint = str(Path(self.temporary.name) / "qmp.sock")
            disk = (["-machine", "q35", "-drive",
                     f"file={image},format=raw,if=none,id=disk0",
                     "-device", "ich9-ahci,id=ahci",
                     "-device", "ide-hd,drive=disk0,bus=ahci.0"] if ahci else
                    ["-drive", f"file={image},format=raw,if=ide"])
            self.proc = subprocess.Popen(
                ["qemu-system-x86_64", *disk, "-boot", "c", "-m", "512M",
                 "-display", "none", "-serial", f"file:{self.log_path}",
                 "-qmp", f"unix:{endpoint},server,nowait"],
                stdout=subprocess.DEVNULL, stderr=self.stderr,
            )
            self._connect(endpoint)
            greeting = self._receive(time.monotonic() + QMP_TIMEOUT)
            if not isinstance(greeting.get("QMP"), dict):
                raise RuntimeError("invalid QMP greeting")
            self._qmp({"execute": "qmp_capabilities"})
            self.hangul = True
            self._wait(PROMPT, BOOT_TIMEOUT, "boot prompt")
            self._ready_input()
        except BaseException:
            self.close(suppress_errors=True)
            if self.log_path is not None:
                print(f"QEMU logs: {self.log_path}, {self.log_path}.qemu.log", file=sys.stderr)
            raise

    def __enter__(self) -> Console:
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.close(suppress_errors=exc_type is not None)

    def _check_alive(self) -> None:
        if self.proc is None or self.proc.poll() is not None:
            raise RuntimeError(f"QEMU exited; see {self.log_path}.qemu.log")

    def _connect(self, endpoint: str) -> None:
        deadline = time.monotonic() + CONNECT_TIMEOUT
        while time.monotonic() < deadline:
            self._check_alive()
            connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                connection.settimeout(max(0.001, deadline - time.monotonic()))
                connection.connect(endpoint)
            except BaseException as error:
                connection.close()
                if not isinstance(error, OSError) or error.errno not in (
                        errno.ENOENT, errno.ECONNREFUSED):
                    raise
                time.sleep(0.1)
            else:
                self.connection = connection
                return
        raise TimeoutError("QMP connection timeout")

    def _receive(self, deadline: float) -> dict:
        while True:
            self._check_alive()
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("QMP response timeout")
            newline = self.received.find(b"\n")
            if newline >= 0:
                line = self.received[:newline]
                del self.received[:newline + 1]
                try:
                    response = json.loads(line)
                except (ValueError, UnicodeError) as error:
                    raise RuntimeError("invalid QMP JSON") from error
                if not isinstance(response, dict):
                    raise RuntimeError("invalid QMP message")
                return response
            self.connection.settimeout(remaining)
            try:
                chunk = self.connection.recv(4096)
            except TimeoutError as error:
                raise TimeoutError("QMP response timeout") from error
            if not chunk:
                raise RuntimeError("QMP disconnected")
            self.received.extend(chunk)
            if len(self.received) > QMP_MESSAGE_MAX:
                raise RuntimeError("QMP message exceeds size limit")

    def _qmp(self, message: dict) -> dict:
        self._check_alive()
        if self.connection is None:
            raise RuntimeError("QMP connection is closed")
        self.request_id += 1
        request = {**message, "id": self.request_id}
        deadline = time.monotonic() + QMP_TIMEOUT
        self.connection.settimeout(QMP_TIMEOUT)
        self.connection.sendall((json.dumps(request) + "\n").encode("utf-8"))
        while True:
            response = self._receive(deadline)
            if "event" in response:
                continue
            if response.get("id") != self.request_id:
                raise RuntimeError("unexpected QMP response id")
            if "error" in response:
                raise RuntimeError(f"QMP error: {response['error']}")
            if "return" not in response:
                raise RuntimeError("invalid QMP response")
            return response

    def _send(self, keys: list[str]) -> None:
        # Explicit releases avoid send-key's delayed virtual-timer key-up
        # overlapping the next host-paced stroke (especially Shift+Space).
        events = [{"type": "key", "data": {"down": down,
                   "key": {"type": "qcode", "data": key}}}
                  for down, group in ((True, keys), (False, reversed(keys)))
                  for key in group]
        self._qmp({"execute": "input-send-event", "arguments": {"events": events}})
        time.sleep(0.03)

    def text(self) -> str:
        with open(self.log_path, encoding="utf-8", errors="replace") as log:
            return log.read()

    def _wait(self, tail: str, timeout: float, what: str) -> None:
        self._wait_after(-1, tail, timeout, what)

    def _wait_after(self, offset: int, tail: str, timeout: float, what: str) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._check_alive()
            current = self.text()
            matched = current.endswith(tail)
            if tail == PROMPT:
                # The new Mowkow prompt and Python's >>> also end in "> ".
                matched = current == PROMPT or current.endswith("\n" + PROMPT)
            if len(current) > offset and matched:
                return
            time.sleep(0.1)
        raise TimeoutError(f"timeout during {what}; see {self.log_path}")

    def _ready_input(self) -> None:
        # console64_init prints its prompt before KBC initialization. Only a
        # fresh empty-line response proves that the input/event path is live.
        deadline = time.monotonic() + BOOT_TIMEOUT
        while time.monotonic() < deadline:
            before = len(self.text())
            self._send(["ret"])
            remaining = deadline - time.monotonic()
            try:
                self._wait_after(before, PROMPT, min(1.0, remaining), "input readiness")
                return
            except TimeoutError:
                pass
        raise TimeoutError(f"input readiness timeout; see {self.log_path}")

    def _mode(self, want_hangul: bool) -> None:
        if self.hangul != want_hangul:
            self._send(["shift", "spc"])
            self.hangul = want_hangul

    def type(self, text: str) -> None:
        for ch in text:
            keys = hangul_keys(ch)
            if keys is not None:
                self._mode(True)
                for key in keys:
                    self._send(["shift", key.lower()] if key.isupper() else [key])
            else:
                self._mode(False)
                if ch in SHIFTED:
                    self._send(SHIFTED[ch].split("-"))
                elif ch in PLAIN:
                    self._send([PLAIN[ch]])
                elif ch.isupper():
                    self._send(["shift", ch.lower()])
                else:
                    self._send([ch])

    def run(self, command: str, inputs: tuple | list = ()) -> str:
        """명령 한 줄과 그 뒤에 넣을 입력들. 프롬프트로 돌아올 때까지의 출력을 준다."""
        before = len(self.text())
        self.type(command)
        self._send(["ret"])
        for line in inputs:
            time.sleep(1.0)     # 해석기가 바빠도 키는 큐에 쌓인다
            self.type(line)
            self._send(["ret"])
        self._wait_after(before, PROMPT, CMD_TIMEOUT, command)
        return self.text()[before:]

    def interrupt(self, command: str, keys: list[str]) -> str:
        """명령의 자체 prompt가 뜨면 Ctrl key를 보내고 OS prompt 복귀를 본다."""
        before = len(self.text())
        self.type(command)
        self._send(["ret"])
        self._wait_after(before, MOWKOW_PROMPT, CMD_TIMEOUT, f"{command} REPL")
        offset = len(self.text())
        self._send(keys)
        self._wait_after(offset, PROMPT, CMD_TIMEOUT, "'%s' interrupt" % command)
        return self.text()[before:]

    def py_system_exit(self) -> str:
        """friendly REPL에서 SystemExit을 내고 OS prompt로 돌아온다."""
        before = len(self.text())
        self.type("py")
        self._send(["ret"])
        self._wait_after(before, ">>> ", CMD_TIMEOUT, "py REPL prompt")
        offset = len(self.text())
        self.type("raise SystemExit")
        self._send(["ret"])
        self._wait_after(offset, PROMPT, CMD_TIMEOUT, "py SystemExit")
        return self.text()[before:]

    def close(self, suppress_errors: bool = False) -> None:
        errors = []
        if self.proc is not None:
            try:
                if self.proc.poll() is None:
                    try:
                        self.proc.terminate()
                    except ProcessLookupError:
                        pass
                    except OSError:
                        self.proc.kill()
                try:
                    self.proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait(timeout=5)
                self.proc = None
            except BaseException as error:
                errors.append(error)
                if not isinstance(error, Exception):
                    try:
                        self.proc.kill()
                        self.proc.wait(timeout=5)
                        self.proc = None
                    except BaseException as cleanup_error:
                        errors.append(cleanup_error)
        for name in ("connection", "stderr", "temporary"):
            resource = getattr(self, name)
            if resource is None:
                continue
            try:
                if name == "temporary":
                    resource.cleanup()
                else:
                    resource.close()
                setattr(self, name, None)
            except BaseException as error:
                errors.append(error)
        if errors:
            message = f"QEMU cleanup failed ({self.log_path}): {errors}"
            if not suppress_errors:
                for error in errors:
                    if not isinstance(error, Exception):
                        raise error
                raise RuntimeError(message)
            print(message, file=sys.stderr)


def drop_prompts(raw: str, prompts: tuple[str, ...]) -> list[str]:
    """Remove exact prompt tokens before trimming; preserve bare > and .. values.

    Host input() prints adjacent prompts without echoing input. Strip only the
    prompts of that producer, including their spaces, rather than >/. characters.
    """
    out = []
    for line in raw.split("\n"):
        while line.startswith(prompts):
            for prompt in prompts:
                if line.startswith(prompt):
                    line = line[len(prompt):]
                    break
        line = line.strip()
        if line:
            out.append(line)
    return out


def strip_device(raw: str, command: str, inputs: list[str] | tuple[str, ...]) -> list[str]:
    """장치 출력에서 명령 줄, 프롬프트, 되울린 입력, 빈 줄을 걷어낸다."""
    lines = raw.split("\n")
    if lines and command in lines[0]:
        lines = lines[1:]
    if lines and lines[-1] == PROMPT:
        lines = lines[:-1]
    prompts = (MOWKOW_PROMPT, MOWKOW_CONTINUATION) if command == "머꼬" else ()
    out = drop_prompts("\n".join(lines), prompts)
    for echo in inputs:
        # 되울린 입력은 앞에서부터 하나씩만 지운다. 한글 조합 경로에 따라
        # COM1에는 ASCII만 남을 수 있어 그 꼴도 함께 본다.
        variants = {echo.strip(), "".join(c for c in echo if ord(c) < 128).strip()}
        for i, line in enumerate(out):
            if line in variants:
                del out[i]
                break
    return out


def strip_host(raw: str, *, repl: bool = False) -> list[str]:
    prompts = (REFERENCE_MOWKOW_PROMPT, MOWKOW_CONTINUATION) if repl else ()
    return drop_prompts(raw, prompts)


def check_repl_prompts(device_raw: str, host_raw: str) -> bool:
    """Check the unnormalized REPL transcript independently of value parity."""
    return (
        "\n>> 0육ㄱ\n10\n>> (+ 1\n..  2)\n3\n>> '>\n>\n>> \n" in device_raw
        and host_raw.startswith(">  10\n>  ..  3\n>  >\n>  ")
    )


def run_host(mk_name: str, inputs: list[str]) -> str:
    proc = subprocess.run(
        [sys.executable, "main.py", os.path.join("test_code", mk_name)],
        cwd=UPSTREAM, input="".join(line + "\n" for line in inputs),
        capture_output=True, text=True, timeout=CMD_TIMEOUT, check=True)
    return proc.stdout + proc.stderr


# (이름, 이미지 안 파일, 업스트림 test_code 파일 또는 None, 입력들)
CASES = [
    ("hello", "hello.mk", "hello.mk", []),
    ("add", "add.mk", "add.mk", ["3", "4"]),
    ("gcd_lcm", "gcd_lcm.mk", "gcd_lcm.mk", ["12", "18"]),
    ("greet", "greet.mk", "greet.mk", ["가나"]),
    ("library", "lib_all.mk", None, []),        # 라이브러리 20가지, 업스트림 밖
]


def main(argv: list[str] | None = None, console_factory: type[Console] = Console) -> int:
    parser = argparse.ArgumentParser(description="머꼬 병행 검사")
    parser.add_argument("--image", default=DEFAULT_IMAGE)
    parser.add_argument("--ahci", action="store_true", help="q35 + AHCI로 부팅")
    parser.add_argument("--case", help="이 검사 하나만")
    parser.add_argument("--log", help="serial log path (default: unique file under /tmp)")
    args = parser.parse_args(argv)

    cases = [c for c in CASES if args.case in (None, c[0])]
    if not cases:
        parser.error("그런 검사가 없다: %s" % args.case)

    failures = []
    with console_factory(args.image, args.ahci, args.log) as console:
        print(f"serial log: {console.log_path}", flush=True)
        smoke = console.run("py smoke.py")
        ok = "smoke ok" in smoke
        print("%-10s %s" % ("smoke", "ok" if ok else "FAIL"))
        if not ok:
            failures.append("smoke")
            print("   " + smoke.strip().replace("\n", "\n   "))

        for name, image_file, upstream_file, inputs in cases:
            command = "머꼬 " + image_file
            device = strip_device(console.run(command, inputs), image_file, inputs)
            if upstream_file is None:               # 업스트림에 없는 파일: 장치끼리만
                host = strip_host(subprocess.run(
                    [sys.executable, "main.py",
                     os.path.join(ROOT, "64bit", "py64", "머꼬", image_file)],
                    cwd=UPSTREAM, capture_output=True, text=True,
                    timeout=CMD_TIMEOUT, check=True).stdout)
            else:
                host = strip_host(run_host(upstream_file, inputs))
            if device == host:
                print("%-10s ok (%d줄)" % (name, len(host)))
            else:
                failures.append(name)
                print("%-10s FAIL" % name)
                print("   호스트: %r" % host)
                print("   장치  : %r" % device)

        # Keep raw prompt checks separate from normalization: a quoted > must
        # survive as data, and multiline input must retain the .. prompt.
        repl_lines = ["0육ㄱ", "(+ 1", "2)", "'>", ""]
        device_raw = console.run("머꼬", repl_lines)
        # 작별 인사는 업스트림에서 stderr로 간다(eprint). 우리 eprint는 stdout이다
        # -- 결정 9가 말한 "경로가 다른 것"이라 양쪽을 합쳐서 견준다.
        proc = subprocess.run(
            [sys.executable, "main.py"], cwd=UPSTREAM,
            input="".join(line + "\n" for line in repl_lines),
            capture_output=True, text=True, timeout=CMD_TIMEOUT, check=True)
        host_raw = proc.stdout + proc.stderr
        prompts_ok = check_repl_prompts(device_raw, host_raw)
        device = strip_device(device_raw, "머꼬", repl_lines)
        host = strip_host(host_raw, repl=True)
        if prompts_ok and device == host:
            print("%-10s ok (%d줄, raw >> / ..)" % ("repl", len(host)))
        else:
            failures.append("repl")
            print("%-10s FAIL" % "repl")
            print("   호스트: %r" % host)
            print("   장치  : %r" % device)
            if not prompts_ok:
                print("   프롬프트 원문 호스트: %r" % host_raw)
                print("   프롬프트 원문 장치  : %r" % device_raw)

        deep = console.run("머꼬 deep.mk")
        ok = "RuntimeError: maximum recursion depth exceeded" in deep
        print("%-10s %s" % ("deep", "ok" if ok else "FAIL"))
        if not ok:
            failures.append("deep")

        ctrl_c = console.interrupt("머꼬", ["ctrl", "c"])
        ok = "Traceback" not in ctrl_c
        print("%-10s %s" % ("ctrl-c", "ok" if ok else "FAIL"))
        if not ok:
            failures.append("ctrl-c")

        ctrl_d = console.interrupt("머꼬", ["ctrl", "d"])
        ok = "머꼬'를 사용해 주셔서 고맙습니다." in ctrl_d
        print("%-10s %s" % ("ctrl-d", "ok" if ok else "FAIL"))
        if not ok:
            failures.append("ctrl-d")

        system_exit = console.py_system_exit()
        ok = "Traceback" not in system_exit
        print("%-10s %s" % ("systemexit", "ok" if ok else "FAIL"))
        if not ok:
            failures.append("systemexit")

        cleanup = console.run("py smoke.py")
        ok = "smoke ok" in cleanup
        print("%-10s %s" % ("cleanup", "ok" if ok else "FAIL"))
        if not ok:
            failures.append("cleanup")
    print("\n%d개 중 %d개 실패" % (len(cases) + 7, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
