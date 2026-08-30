#!/usr/bin/env python3
"""
머꼬 병행 검사 -- 이미지 안의 머꼬와 호스트 CPython의 업스트림 머꼬를
같은 입력으로 돌려 견준다 (mowkow_porting.md 결정 9, 시험 계획).

    python3 tools/mowkow_parity.py [--ahci] [--image IMG] [--case 이름]

QEMU를 화면 없이 띄우고, QMP send-key로 콘솔에 명령을 쳐 넣고, COM1으로
나온 것을 받는다. 콘솔은 한글 입력으로 뜨므로 한글은 두벌식 자판 순서로
친다(TYPE 표). 그래서 `머꼬 add.mk`도, `읽기`에 넣는 `가나`도 사람이 치는
길과 같은 길로 들어간다.

견주는 규칙(결정 9: 글자 그대로가 아니라 행동으로):
  - 명령을 친 줄과 다음 `> ` 프롬프트 사이만 본다 (배너와 부팅 로그는 빠진다)
  - 빈 줄은 양쪽에서 버린다 (한글 조합 중인 글자는 COM1에 나오지 않는다)
  - 장치 쪽에 되울린 입력 줄은 순서대로 하나씩 버린다 (호스트는 되울리지 않는다)
그 나머지 -- 값과 오류의 차례 -- 가 같아야 한다.
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UPSTREAM = os.path.join(ROOT, "64bit", "py64", "머꼬", "upstream")
DEFAULT_IMAGE = os.path.join(ROOT, "img64", "mowkow64.img")

PROMPT = "> "
BOOT_TIMEOUT = 60
CMD_TIMEOUT = 180

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
    """QEMU 하나를 띄워 놓고 콘솔에 명령을 쳐 넣는다."""

    def __init__(self, image, ahci, log_path):
        disk = (["-machine", "q35",
                 "-drive", "file=%s,format=raw,if=none,id=disk0" % image,
                 "-device", "ich9-ahci,id=ahci",
                 "-device", "ide-hd,drive=disk0,bus=ahci.0"] if ahci else
                ["-drive", "file=%s,format=raw,if=ide" % image])
        self.log_path = log_path
        open(log_path, "wb").close()
        self.proc = subprocess.Popen(
            ["qemu-system-x86_64"] + disk + ["-boot", "c", "-m", "512M",
             "-display", "none", "-serial", "file:%s" % log_path,
             "-qmp", "tcp:127.0.0.1:%d,server,nowait" % 4444],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.qmp = None
        for _ in range(50):
            try:
                self.qmp = socket.create_connection(("127.0.0.1", 4444)).makefile("rw")
                break
            except OSError:
                time.sleep(0.2)
        if self.qmp is None:
            raise RuntimeError("QMP에 붙지 못했다")
        self.qmp.readline()
        self._qmp({"execute": "qmp_capabilities"})
        self.hangul = True          # 콘솔은 한글 입력으로 뜬다
        self._wait(PROMPT, BOOT_TIMEOUT, "부팅과 첫 프롬프트")

    def _qmp(self, message):
        self.qmp.write(json.dumps(message) + "\n")
        self.qmp.flush()
        return self.qmp.readline()

    def _send(self, keys):
        self._qmp({"execute": "send-key",
                   "arguments": {"keys": [{"type": "qcode", "data": k} for k in keys]}})
        time.sleep(0.03)

    def text(self):
        with open(self.log_path, encoding="utf-8", errors="replace") as log:
            return log.read()

    def _wait(self, tail, timeout, what):
        """화면에 tail이 나올 때까지 기다린다. 프롬프트면 '명령이 끝났다'는 뜻이다."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.text().endswith(tail):
                return
            time.sleep(0.2)
        raise TimeoutError("%s를 기다리다 시간이 지났다" % what)

    def _mode(self, want_hangul):
        if self.hangul != want_hangul:
            self._send(["shift", "spc"])
            self.hangul = want_hangul

    def type(self, text):
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

    def run(self, command, inputs=()):
        """명령 한 줄과 그 뒤에 넣을 입력들. 프롬프트로 돌아올 때까지의 출력을 준다."""
        before = len(self.text())
        self.type(command)
        self._send(["ret"])
        for line in inputs:
            time.sleep(1.0)     # 해석기가 바빠도 키는 큐에 쌓인다
            self.type(line)
            self._send(["ret"])
        self._wait(PROMPT, CMD_TIMEOUT, "'%s'" % command)
        return self.text()[before:]

    def close(self):
        try:
            self._qmp({"execute": "quit"})
        except Exception:
            pass
        self.proc.terminate()
        self.proc.wait()


def drop_prompts(raw):
    """프롬프트(`> `, `>  `, `.. `)를 떼고 빈 줄을 버린다. 업스트림은 프롬프트를
    표준 출력에 붙여 찍으므로(input(prompt)) 양쪽에 똑같이 적용해야 한다."""
    out = []
    for line in raw.split("\n"):
        line = line.strip()
        while line.startswith(">") or line.startswith(".."):
            line = line.lstrip(">.").lstrip()
        if line:
            out.append(line)
    return out


def strip_device(raw, command, inputs):
    """장치 출력에서 명령 줄, 프롬프트, 되울린 입력, 빈 줄을 걷어낸다."""
    lines = raw.split("\n")
    if lines and command in lines[0]:
        lines = lines[1:]
    out = drop_prompts("\n".join(lines))
    for echo in inputs:
        # 되울린 입력은 앞에서부터 하나씩만 지운다. 콘솔은 조합 중인 한글을
        # 프레임버퍼에만 그리므로 COM1에는 한글이 빠진 채로 되울린다 --
        # `0육ㄱ`을 친 줄이 `0`으로 나온다. 그래서 ASCII만 남긴 꼴도 함께 본다.
        variants = {echo.strip(), "".join(c for c in echo if ord(c) < 128).strip()}
        for i, line in enumerate(out):
            if line in variants:
                del out[i]
                break
    return out


def strip_host(raw):
    return drop_prompts(raw)


def run_host(mk_name, inputs):
    proc = subprocess.run(
        [sys.executable, "main.py", os.path.join("test_code", mk_name)],
        cwd=UPSTREAM, input="".join(line + "\n" for line in inputs),
        capture_output=True, text=True)
    return proc.stdout + proc.stderr


# (이름, 이미지 안 파일, 업스트림 test_code 파일 또는 None, 입력들)
CASES = [
    ("hello", "hello.mk", "hello.mk", []),
    ("add", "add.mk", "add.mk", ["3", "4"]),
    ("gcd_lcm", "gcd_lcm.mk", "gcd_lcm.mk", ["12", "18"]),
    ("greet", "greet.mk", "greet.mk", ["가나"]),
    ("library", "lib_all.mk", None, []),        # 라이브러리 20가지, 업스트림 밖
]


def main():
    parser = argparse.ArgumentParser(description="머꼬 병행 검사")
    parser.add_argument("--image", default=DEFAULT_IMAGE)
    parser.add_argument("--ahci", action="store_true", help="q35 + AHCI로 부팅")
    parser.add_argument("--case", help="이 검사 하나만")
    parser.add_argument("--log", default="/tmp/mowkow-parity.log")
    args = parser.parse_args()

    cases = [c for c in CASES if args.case in (None, c[0])]
    if not cases:
        parser.error("그런 검사가 없다: %s" % args.case)

    console = Console(args.image, args.ahci, args.log)
    failures = []
    try:
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
                    cwd=UPSTREAM, capture_output=True, text=True).stdout)
            else:
                host = strip_host(run_host(upstream_file, inputs))
            if device == host:
                print("%-10s ok (%d줄)" % (name, len(host)))
            else:
                failures.append(name)
                print("%-10s FAIL" % name)
                print("   호스트: %r" % host)
                print("   장치  : %r" % device)

        # REPL 한 판: mowio.readline, 한글 입력, 0육 리터럴, 빈 줄로 나가기를
        # 한 번에 지난다. 호스트는 같은 줄을 표준 입력으로 받는다.
        repl_lines = ["0육ㄱ", "(+ 1 2)", ""]
        device = strip_device(console.run("머꼬", repl_lines), "머꼬", repl_lines)
        # 작별 인사는 업스트림에서 stderr로 간다(eprint). 우리 eprint는 stdout이다
        # -- 결정 9가 말한 "경로가 다른 것"이라 양쪽을 합쳐서 견준다.
        proc = subprocess.run(
            [sys.executable, "main.py"], cwd=UPSTREAM,
            input="".join(line + "\n" for line in repl_lines),
            capture_output=True, text=True)
        host = strip_host(proc.stdout + proc.stderr)
        if device == host:
            print("%-10s ok (%d줄)" % ("repl", len(host)))
        else:
            failures.append("repl")
            print("%-10s FAIL" % "repl")
            print("   호스트: %r" % host)
            print("   장치  : %r" % device)

        deep = console.run("머꼬 deep.mk")
        ok = "RuntimeError: maximum recursion depth exceeded" in deep
        print("%-10s %s" % ("deep", "ok" if ok else "FAIL"))
        if not ok:
            failures.append("deep")
    finally:
        console.close()

    print("\n%d개 중 %d개 실패" % (len(cases) + 3, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
