#coding: utf-8
"""
머꼬 진입점(업스트림 main.py).

업스트림 main.py는 argparse와 os.path, PyInstaller의 _MEIPASS 탐색이 절반이라
남는 것은 load_file과 eval_print_loop은 업스트림에서 그대로 가져왔다.
(파일 이름을 os.path.basename 대신 받은 경로 그대로 쓰는 것만 다르다)

콘솔의 `머꼬` 명령: import -> main() 호출.
library_kor.scm으로 만든 바탕 환경이 import 캐시에 남아 부팅 뒤 한 번만 만들어진다.
"""

import mowio

from _data import nil, mksym, mkbuiltin
from _parse import YY_reader, read_expr
from _error import eprint, ErrLisp
from _eval import mkenv, envset, do_eval, \
        builtin_car, builtin_cdr, builtin_cons, \
        builtin_add, builtin_sub, builtin_mul, builtin_div, \
        builtin_inteq, builtin_intlt, builtin_intgt, \
        builtin_apply, builtin_eq, builtin_ispair, builtin_isnil, \
        builtin_not, builtin_and, builtin_or, \
        builtin_read, builtin_write, builtin_gensym

LIBRARY = "library_kor.scm"

# 업스트림 main()의 envset 목록 그대로.
_BUILTINS = (
    ("머", builtin_car), ("꼬", builtin_cdr), ("짝", builtin_cons),
    ("+", builtin_add), ("-", builtin_sub), ("*", builtin_mul), ("/", builtin_div),
    ("=", builtin_inteq), ("<", builtin_intlt), (">", builtin_intgt),
    ("적용", builtin_apply), ("같다?", builtin_eq), ("짝?", builtin_ispair),
    ("공?", builtin_isnil), ("부정", builtin_not), ("그리고", builtin_and),
    ("또는", builtin_or), ("~", builtin_not), ("&", builtin_and), ("|", builtin_or),
    ("읽기", builtin_read), ("쓰기", builtin_write), ("_모", builtin_gensym),
)

_base = None        # 부팅 뒤 한 번만 만든다


def load_file(env, path) -> None:
    """env 환경에서 path의 파일을 읽어 수행."""
    YY_reader.readfile(path)
    YY_reader.next_token()
    while YY_reader.remains() != "":
        try:
            expr = read_expr()
            result = do_eval(expr, env)
            if result is not None:
                eprint(result)
        except ErrLisp as err:
            eprint(f"{path}:{YY_reader.line()}: 오류: {err}")
        except SyntaxError as err:
            eprint(f"{path}:{YY_reader.line()}: 오류: {err}")


def eval_print_loop(env) -> None:
    """표준 입력에서 읽고 env 하에서 실행한 후 출력을 반복한다. 빈 행이면 종료
    (업스트림 main.py:154-180)."""
    fname = "<표준 입력>"
    while True:
        try:
            YY_reader.resetpos()
            if YY_reader.read() == "":
                eprint("간편한 한글 프로그래밍 언어 '머꼬'를 사용해 주셔서 고맙습니다.")
                break
            YY_reader.next_token()
            expr = read_expr()
            val = do_eval(expr, env)
            if val != None:
                print(val)
        except ErrLisp as err:
            eprint(f"오류: {err}")
        except EOFError:
            eprint("간편한 한글 프로그래밍 언어 '머꼬'를 사용해 주셔서 고맙습니다.")
            break
        except SyntaxError:
            eprint(f"{fname}:{YY_reader.line()}:{YY_reader.column()}: 구문 오류: '{YY_reader.LA()}'")


def base_env():
    """내장 함수와 library_kor.scm이 든 바탕 환경. 처음 부를 때만 만든다."""
    global _base
    if _base is None:
        env = mkenv(nil)
        for name, fn in _BUILTINS:
            envset(env, mksym(name), mkbuiltin(fn))
        envset(env, mksym("#참"), mksym("#참"))
        load_file(env, LIBRARY)
        _base = env
    return _base


def main() -> None:
    """세션 하나. 바탕 환경은 함께 쓰고 그 위에 새 틀을 얹으므로, 여기서 만든
    정의는 세션이 끝나면 사라진다."""
    env = mkenv(base_env())
    path = mowio.argv()
    if path is None:
        eval_print_loop(env)
    else:
        load_file(env, path)
