#coding: utf-8
"""
MicroPython에 없는 CPython 동작만 되살리는 자리 (mowkow_porting.md 결정 1의 예외).

여기 있는 것은 새 의미가 아니라 빠진 의미다. 한 파일로 모아 두어야 핵심
모듈(_data/_parse/_eval)이 한 줄씩만 바뀌고, 이식 때문에 생긴 차이를 한자리에서
읽을 수 있다. 업스트림에는 없는 파일이다.
"""

import mowio

# 한글로 인정하는 범위 (결정 6). 모두 유니코드 분류 Lo라 CPython의
# str.isalpha()도 이 부분집합에는 같은 답을 준다.
_HANGUL_RANGES = (
    (0x1100, 0x11FF),   # 한글 자모 (조합용)
    (0x3131, 0x318E),   # 호환 자모 - 0육 리터럴의 ㄱㄴㄷㄹㅁㅂ와 모음
    (0xA960, 0xA97C),   # 자모 확장-A
    (0xAC00, 0xD7A3),   # 한글 음절
    (0xD7B0, 0xD7FB),   # 자모 확장-B
)


def isalpha(s: str) -> bool:
    """str.isalpha()의 한글판.

    MicroPython의 unichar_isalpha는 ASCII만 안다(py/objstr.c). 그래서 한글
    심볼이 전부 False가 된다. 한글과 ASCII 글자만 참이고 나머지 문자(한자,
    그리스 문자 등)는 거짓이다 - CPython보다 좁은 것은 여기 하나뿐이다(결정 6).

    ASCII도 str.isalpha()에 맡기지 않고 직접 견준다. 그래야 이 함수의 답이
    호스트에 따라 달라지지 않아서, CPython으로도 그대로 검사할 수 있다.
    """
    if s == "":
        return False
    for ch in s:
        if ("a" <= ch <= "z") or ("A" <= ch <= "Z"):
            continue
        code = ord(ch)
        for low, high in _HANGUL_RANGES:
            if low <= code <= high:
                break
        else:
            return False
    return True


def funname(fn) -> str:
    """내장 함수의 이름.

    CPython에서 f"{fn}"은 "<function 이름 at 0x...>"이라 공백으로 나눈 [2]가
    이름이다. MicroPython은 "<function>"만 찍어서 IndexError가 난다.
    """
    return getattr(fn, "__name__", "?")


def eprint(*args, **kwargs) -> None:
    """stderr가 없다(MICROPY_PY_SYS_STDFILES는 켜지 않는다). 콘솔이 하나뿐이라
    표준 출력으로 보낸다."""
    print(*args, **kwargs)


def readline(prompt: str = "") -> str:
    """input() 대신. 콘솔의 줄 편집기를 그대로 쓰므로 조합 중인 한글 음절이
    화면에서 자란다(결정 5). Ctrl-C는 KeyboardInterrupt, Ctrl-D는 EOFError."""
    return mowio.readline(prompt)


def to_int(s: str, base: int) -> int:
    """int(s, base=N)의 자리.

    MicroPython의 내장 함수는 키워드 인수를 받지 않는다("TypeError: function
    doesn't take keyword arguments"). 자리 인수로 바꿔 부르는 것뿐이고 뜻은
    같다 - 16진(0x, 0육)과 8진 리터럴이 이 길로 온다.
    """
    return int(s, base)


def token_prefix(s: str) -> str:
    """re.split(r"[\\s()]", s)[0]와 같다 - 첫 공백이나 괄호 앞까지.

    re는 EXTRA 등급이라 커널에 없다. 쓰이는 것은 [0] 하나뿐이라 정규식 엔진을
    통째로 넣는 대신 앞부분만 잘라 준다.
    """
    for i in range(len(s)):
        if s[i] in " \t\n\r\x0b\x0c()":
            return s[:i]
    return s
