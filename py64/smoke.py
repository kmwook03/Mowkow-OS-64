# 이식 부품 확인 (mowkow_porting.md 4-6단계). tools/mowkow_parity.py가 돌린다.
# 실패하면 예외로 죽고, 다 지나가면 마지막 줄에 smoke ok를 찍는다.
import mowio
import _compat
import _eval
from _data import mkbuiltin

# 4단계: fd64를 타는 import. 여기까지 왔으면 _eval이 _data/_parse/_error를 끌어왔다.
assert _eval.mkenv is not None

# 5단계: mowio
text = mowio.readfile("hello.mk")
assert len(text) > 0 and len(text) < len(text.encode())     # 한글이 있으니 글자 < 바이트
assert mowio.argv() is None                                 # py로 돌릴 때는 인자가 없다
assert mowio.ticks() >= 0
try:
    mowio.readfile("nosuchfile.mk")
    raise AssertionError("없는 파일인데 오류가 안 났다")
except OSError:
    pass

# 6단계: _compat 시임
for ch in ("가", "ㄱ", "ㅏ", "a", "힣"):
    assert _compat.isalpha(ch), ch
for ch in ("1", "(", " ", "", "漢"):
    assert not _compat.isalpha(ch), ch
assert _compat.token_prefix("(더하기 1 2)") == ""
assert _compat.token_prefix("더하기 1") == "더하기"
assert _compat.to_int("0xA", 16) == 10 and _compat.to_int("017", 8) == 15
assert str(mkbuiltin(_eval.builtin_car)) == "#<내장 함수: builtin_car>"

print("smoke ok")
