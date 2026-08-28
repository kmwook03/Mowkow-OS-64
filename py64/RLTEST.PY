# mowio.readline 확인용 (mowkow_porting.md 2단계).
# py RLTEST.PY 로 돌린다 - py REPL 안에서 돌리면 repl_active가 이미 켜져 있어
# 정작 머꼬가 쓰는 길(스크립트 안)이 안 걸린다.
import mowio

try:
    s = mowio.readline("입력> ")
    print("len", len(s), "bytes", len(s.encode()))
    print("echo", s)
except KeyboardInterrupt:
    print("KeyboardInterrupt")
except EOFError:
    print("EOFError")
