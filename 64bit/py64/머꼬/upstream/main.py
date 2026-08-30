#coding: utf-8

# Ph. 14: Multiline
#       YY_reader, YY_input 설정
#       read_expr 호출 전에 next_token으로 LA를 먼저 설정

import os
import sys
import argparse
from typing import Any, Optional, Iterable, NoReturn

from _data import Data, nil, mksym, mkbuiltin
from _parse import YY_reader, read_expr
from _error import IsVerbose, eprint, ErrLisp
from _eval import mkenv, envset, do_eval, \
        builtin_car, builtin_cdr, builtin_cons, \
        builtin_add, builtin_sub, builtin_mul, builtin_div, \
        builtin_inteq, builtin_intlt, builtin_intgt, \
        builtin_apply, builtin_eq, builtin_ispair, builtin_isnil, builtin_not, builtin_and, builtin_or, \
        builtin_read, builtin_write, builtin_gensym 

import argparse

class KoreanHelpFormatter(argparse.HelpFormatter):
    """argparse의 영어 메시지를 한글로 변경하는 HelpFormatter"""
    
    def __init__(self, prog: str, indent_increment: int = 2, 
                 max_help_position: int = 24, width: Optional[int] = None):
        super().__init__(prog, indent_increment, max_help_position, width)
    
    def _format_usage(self, usage: Optional[str], actions: Iterable[argparse.Action], 
                      groups: Iterable[argparse._MutuallyExclusiveGroup], prefix: Optional[str]) -> str:
        """usage 메시지를 한글로 변경"""
        if prefix is None:
            prefix = '사용법: '
        return super()._format_usage(usage, actions, groups, prefix)
    

class KoreanArgumentParser(argparse.ArgumentParser):
    """한글 오류 메시지를 지원하는 ArgumentParser"""
    
    def __init__(self, *args, **kwargs):
        # 기본 formatter_class를 KoreanHelpFormatter로 설정
        if 'formatter_class' not in kwargs:
            kwargs['formatter_class'] = KoreanHelpFormatter
        super().__init__(*args, **kwargs)
        
        # 한글 메시지 딕셔너리
        self.korean_messages = {
            # 기본 오류 메시지들
            'unrecognized arguments': '인식되지 않는 인수',
            'the following arguments are required': '다음 인수들이 필요합니다',
            'argument': '인수',
            'invalid choice': '잘못된 선택',
            'choose from': '다음 중에서 선택하세요',
            'invalid int value': '잘못된 정수 값',
            'invalid float value': '잘못된 실수 값',
            'expected one argument': '하나의 인수가 필요합니다',
            'expected one': '하나의',
            'expected at least one argument': '최소 하나의 인수가 필요합니다',
            'expected at most one argument': '최대 하나의 인수만 허용됩니다',
            'ambiguous option': '모호한 옵션',
            'usage': '사용법',
            'optional arguments': '선택적 인수',
            'positional arguments': '위치 인수',
            'show this help message and exit': '이 도움말 메시지를 표시하고 종료',
            'error': '오류',
        }
    
    def error(self, message: str) -> NoReturn:
        """오류 메시지를 한글로 번역하여 출력"""
        korean_message = self._translate_message(message)
        # 'error: ' 접두사도 한글로 변경
        self.print_usage(sys.stderr)
        args = {'prog': self.prog, 'message': korean_message}
        self.exit(2, '%(prog)s: 오류: %(message)s\n' % args)
    
    def _translate_message(self, message: str) -> str:
        """영어 메시지를 한글로 번역"""
        korean_message = message
        
        # 일반적인 패턴들을 한글로 변경
        for english, korean in self.korean_messages.items():
            korean_message = korean_message.replace(english, korean)
        
        # 특수한 패턴들 처리
        if 'unrecognized arguments:' in korean_message:
            korean_message = korean_message.replace('unrecognized arguments:', '인식되지 않는 인수:')
        
        if 'the following arguments are required:' in korean_message:
            korean_message = korean_message.replace('the following arguments are required:', '다음 인수들이 필요합니다:')
        
        # 선택지 관련 메시지 처리
        if 'invalid choice:' in korean_message and '(choose from' in korean_message:
            parts = korean_message.split('(choose from')
            if len(parts) == 2:
                korean_message = f"잘못된 선택: {parts[0].split(':')[1].strip()} (다음 중에서 선택하세요{parts[1]}"
        
        return korean_message
    
    def print_help(self, file: Optional[Any] = None) -> None:
        """도움말 메시지의 섹션 제목들을 한글로 변경"""
        if file is None:
            file = sys.stdout
        
        # 원본 도움말 텍스트를 가져옴
        help_text = self.format_help()
        
        # 섹션 제목들을 한글로 변경
        help_text = help_text.replace('usage:', '사용법:')
        help_text = help_text.replace('positional arguments:', '위치 인수:')
        help_text = help_text.replace('optional arguments:', '선택적 인수:')
        help_text = help_text.replace('options:', '옵션:')
        help_text = help_text.replace('show this help message and exit', '이 도움말 메시지를 표시하고 종료')
        
        file.write(help_text)


def resource_path(relative_path):
    """ 실행 파일 내부 또는 개발 환경에서 리소스 경로 가져오기 """
    try:
        # PyInstaller가 사용하는 환경변수 _MEIPASS
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.dirname(os.path.abspath(__file__))

    return os.path.join(base_path, relative_path)

def load_file(env: Data, path: str) -> None:
    """ env 환경에서 path의 파일을 읽어 수행함"""
    #global YY_reader: Reader
    #global IsVerbose

    if IsVerbose:
        eprint(f"'{path}'을(를) 불러오는 중입니다...")
    YY_reader.readfile(path)
    fname = os.path.basename(path)
    tok = YY_reader.next_token()
    while YY_reader.remains() != "":
        try:
            expr = read_expr()
            result = do_eval(expr, env)
            if result is not None:
                eprint(result)
        except ErrLisp as err:
            line = YY_reader.line()
            eprint(f"{fname}:{line}: 오류: {err}")
        except SyntaxError as err:
            line = YY_reader.line()
            eprint(f"{fname}:{line}: 오류: {err}")

def eval_print_loop(env: Data) -> None:
    """ 표준 입력에서 읽고 env 하에서 실행한 후 출력을 반복함
        빈 행을 입력하면 종료
    """
    fname = "<표준 입력>"
    while True:
        try:
            YY_reader.resetpos()
            if YY_reader.read() == "":
                eprint("간편한 한글 프로그래밍 언어 '머꼬'를 사용해 주셔서 고맙습니다.")
                break
            _ = YY_reader.next_token()
            expr = read_expr()
            val = do_eval(expr, env)
            if val != None:
                print(val)
        except ErrLisp as err:
            eprint(f"오류: {err}")
        except EOFError:
            eprint("간편한 한글 프로그래밍 언어 '머꼬'를 사용해 주셔서 고맙습니다.")
            break
        except UnicodeDecodeError:
            eprint(f"오류: 모르는 문자가 입력되었습니다.")
        except SyntaxError:
            line = YY_reader.line()
            col = YY_reader.column()
            tok = YY_reader.LA()
            eprint(f"{fname}:{line}:{col}: 구문 오류: '{tok}'")

def main():
    #global YY_reader: Reader
    # global IsVerbose

    env = mkenv(nil)

    envset(env, mksym("머"), mkbuiltin(builtin_car))
    envset(env, mksym("꼬"), mkbuiltin(builtin_cdr))
    envset(env, mksym("짝"), mkbuiltin(builtin_cons))
    envset(env, mksym("+"), mkbuiltin(builtin_add))
    envset(env, mksym("-"), mkbuiltin(builtin_sub))
    envset(env, mksym("*"), mkbuiltin(builtin_mul))
    envset(env, mksym("/"), mkbuiltin(builtin_div))
    envset(env, mksym("#참"), mksym("#참"))
    envset(env, mksym("="), mkbuiltin(builtin_inteq))
    envset(env, mksym("<"), mkbuiltin(builtin_intlt))
    envset(env, mksym(">"), mkbuiltin(builtin_intgt))
    envset(env, mksym("적용"), mkbuiltin(builtin_apply))
    envset(env, mksym("같다?"), mkbuiltin(builtin_eq))
    envset(env, mksym("짝?"), mkbuiltin(builtin_ispair))
    envset(env, mksym("공?"), mkbuiltin(builtin_isnil))
    envset(env, mksym("부정"), mkbuiltin(builtin_not))
    envset(env, mksym("그리고"), mkbuiltin(builtin_and))
    envset(env, mksym("또는"), mkbuiltin(builtin_or))
    envset(env, mksym("~"), mkbuiltin(builtin_not))
    envset(env, mksym("&"), mkbuiltin(builtin_and))
    envset(env, mksym("|"), mkbuiltin(builtin_or))
    envset(env, mksym("읽기"), mkbuiltin(builtin_read))
    envset(env, mksym("쓰기"), mkbuiltin(builtin_write))
    envset(env, mksym("_모"), mkbuiltin(builtin_gensym))

    argparser = KoreanArgumentParser(
        description='머꼬 해석기',
        prog='mk'
    )

    argparser.add_argument('-o', '--output', 
                           dest="out_file", type=str, help='출력 파일명')
    # argparser.add_argument("-v", "--verbose", help="상세 정보 출력")
    argparser.add_argument("in_files", nargs="*", help="소스 파일 목록")
    arg = argparser.parse_args()
    # if arg.verbose:
    #     IsVerbose = True

    if arg.out_file != None:
        if os.path.exists(arg.out_file):
            eprint(f"출력 파일 '{arg.out_file}'가 이미 존재합니다.")
        else:
            print(arg.out_file)

    lib_file_path = resource_path("library_kor.scm")
    load_file(env, lib_file_path)

    for file in arg.in_files:
        if os.path.exists(file):
            load_file(env, file)
        else:
            eprint(f"소스 파일 '{file}'를 찾을 수 없습니다.")

    if not arg.in_files:
        # IsVerbose = True
        eval_print_loop(env)

if __name__ == "__main__":
    main()

"""
키워드: 정의(define), 람다(lambda), 만약(if), 인용(quote), 매크로(macro),
        특이인용(`), 비인용(,), 비인용연결(,@), _모(gensym), 조건(cond), 잠시(let*)
        # 비인용해제(unquote-splicing)
내장함수:   
    머(car), 꼬(cdr), 짝(cons), 
    +, -, *, /, 
    =, <, 적용(apply), 같다?(eq), 짝?(pair?), 공?(nil?), 그리고(and),
    읽기(read), 쓰기(write)
내장 리터럴: 공(nil), #참(t)
"""


""" TEST Session for Ph 15
>python _main.py
'library_kor.scm'을(를) 불러오는 중입니다...
머리
꼬리
머머
꼬머
그대로
아톰?
리스트?
절댓값
머리돌기
꼬리돌기
리스트
거꾸로
한맵
맵
접합
특이인용
임시
>  (조건 (#참 3))
3
>  (조건 ((< 3 2) 3) (#참 5))
5
>  (잠시
..   ((x 2)
..    (y (+ 1 x)))
..   (+ x y))
5
>  (잠시 ((x 3) (f (람다 (n) (만약 (= n 0) 1 (* n (f (- n 1))))))) (f x))
6
"""


""" TEST Session for Ph 13
Reading library.lisp...
abs
foldl
foldr
list
reverse
unary-map
map
append
caar
cadr
quasiquote
> `(+ 1 ,(+ 2 3))
(+ 1 5)
> (define l `(3 4))
l
> l
(3 4)
> `(1 2 l)
(1 2 l)
> `(1 2 ,@l)
(1 2 3 4)
> `(1 2 ,@l 5)
(1 2 3 4 5)
>

> (let ((x 3) (y 5)) (+ x y))
8
> x
Error: Symbol not bound for 'x'
> (define + (let ((old+ +)) (lambda xs (foldl old+ 0 xs))))
+
> (+ 1 2 3 4)
10
>
"""

""" 내장 함수 _모, 라이브러리 새함수 추가 후 테스트
>python _main.py
'library_kor.scm'을(를) 불러오는 중입니다...
머리
꼬리
머머
꼬머
그대로
아톰?
리스트?
절댓값
머리돌기
꼬리돌기
리스트
거꾸로
한맵
맵
접합
특이인용
임시
새함수
>  (새함수 (+ 2 3))
#기호2000
>  (#기호2000 1)
5
>  (#기호2000 100)
5
>
"""
