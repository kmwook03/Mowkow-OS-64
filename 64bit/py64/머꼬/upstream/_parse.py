#coding: utf-8

from _data import *
from _error import *
import re
import io

# Ph 9
#       lex: 문자 ' 처리
#       read_expr: tok이 '인 경우 추가(quote 노드 추가)
#                  car(cdr(qlst))를 재설정하는 것에서 버그가 있었음

# Ph 12. Library
#       read_simple(str): "-1"이나 "+3"도 인식하도록 수정함

# Ph 13. Quasiquotation
#       lex: ","와 ",@", "`"를 인식하도록 수정함
#       read_expr: "`"를 quasiquote로, ","를 unquote로, ",@"를 unquote-splicing으로 바꾸도록 수정함 

# Ph 14. Multiline 처리
#       클래스: Reader
#       전역 변수: YY_reader 추가
#       전역 함수: match 추가, lex -> next_token으로 변경
#       read_list에서 전에 '('를 읽었다는 가정을 없애고 match로 진행함

# 전역변수 reader
YY_reader = None   # The historical prefix "YY_" is attached to make it global

def slurp(path: str) -> str:
    with io.open(path, mode='r', encoding='utf-8') as reader:
        return reader.read()

def read_esc_char(srep: str) -> tuple[str, int]:
    """정수에 대한 문자를 반환하기 위한 것이지만 그냥 \"만 건너뛰고 '\"'와 2를 반환하는 것으로 구현함"""
    # if not srep[1].isdigit():       # alphabetic escape sequence
    #     return srep[1], 2
    # if srep[1:3].upper() == "0X":   # hexadecimal
    #     matched = re.search(r"0[xX][0-9a-fA-F]+", srep[1:])
    #     hex_str = matched.group(0)
    #     c = chr(int(hex_str, base=16))
    #     return c, len(hex_str)+1
    # if srep[1] == "0":              # octal
    #     matched = re.search(r"0[0-7]+", srep[1:])
    #     oct_str = matched.group(0)
    #     c = chr(int(oct_str, base=8))
    #     return c, len(oct_str)+1
    if srep[1] == '"':
        return srep[:2], 2
    return None, 1                  # 오류(unknown escape sequence)

class Reader:
    def __init__(self):
        self._LA = None
        self._input = ""
        self._line = 1
        self._column = 0
        self._depth = 0
        self._prompt1 = ">  "
        self._prompt2 = ".. "
    def next_token(self) -> str:        # returns the token and set it to LA
        s = self._input
        eols = ["\n", "\r"]
        ws = [" ", "\t"] + eols
        while s != "":
            if s[0] in eols:                        # 줄바꿈 문자
                s = s[1:]
                self._input = s
                self._line += 1
                self._column = 0
                continue
            if s[0] in ws:                          # 그 외 공백 문자
                s = s[1:]
                self._input = s
                self._column += 1
                continue
            if s[0] in ";":
                i = 0
                while s[i] not in eols:
                    i += 1
                s = s[i:]
                self._input = s
                self._line += 1
                self._column = 0
                continue
            if s.startswith(",@"):      # ",@" should be tested before ","
                tok = ",@"
            elif s[0] in "().',`":
                tok = s[0]
            elif s[0] == '"':
                i = 1
                while i < len(s) and s[i] not in ['"'] + eols:
                    if s[i] == '\\':
                        c, k = read_esc_char(s[i:])
                        i += k
                        continue
                    i += 1
                if i == len(s) or s[i] in eols:
                    print(f"오류: 토큰 '{s[:i]}' 부근에서 어휘 오류")
                    # 오류 복구: 닫는 따옴표를 토큰에 추가
                    if i != len(s): # eols를 만난 경우
                        i -= 1      # eol 문자 전까지 내용을 문자열 토큰으로 만들기 위함
                    tok = s[:i] + '"'
                    s = s[i:]
                else:               # s[i] == '"':
                    i += 1      # '"'를 읽음
                    tok = s[:i]
                    # s = s[i:]
            else:
                toks = re.split(r"[\s()]", s)
                tok = toks[0]
            tlen = len(tok)
            self._input = s[tlen:]
            self._column += tlen        # 문자열 리터럴의 경우 _column이 작아지는 오류가 발생할 수 있음
            self._LA = tok
            return tok
        if self._depth > 0:
            self.read2()
            return self.next_token()
        tok = ""
        self._LA = tok
        return tok
    def match(self, tok) -> None:
        if tok != self._LA:
            print(f"오류: 토큰 '{YY_reader.LA()}' 부근에서 어휘 오류")
    def LA(self) -> str:
        return self._LA
    def read(self) -> str:
        try:
            self._input = input(self._prompt1)
        except EOFError:
            return ""
        else:
            return self._input
    def read2(self) -> str:
        try:
            self._input = input(self._prompt2 + self.indent())
        except EOFError:
            return ""
        else:
            return self._input
    def readfile(self, fname) -> str:
        self._input = slurp(fname)
        self.resetpos()
        return self._input
    def resetpos(self):
        """입력 위치 재설정(행: 1, 열: 0)"""
        self._line = 1
        self._column = 0
    def line(self) -> int:
        """리더의 현재 행 번호 반환"""
        return self._line
    def column(self) -> int:
        """리더의 현재 열 번호 반환"""
        return self._column
    def nestin(self) -> None:
        self._depth += 1
    def nestout(self) -> None:
        self._depth -= 1
    def indent(self) -> str:
        return self._depth * " "
    def remains(self) -> str:
        return self._input

YY_reader = Reader()   # The historical prefix "YY_" is attached to make it global

def read_expr() -> Data:
    if YY_reader.LA() == "(":
        data = read_list()
        return data
    elif YY_reader.LA() == ")":
        raise SyntaxError
    elif YY_reader.LA() == "'":
        qlst = cons(mksym("인용"), cons(nil, nil))
        _ = YY_reader.next_token()
        data = read_expr()
        qdta = cdr(qlst)
        qdta.setcar(data)
        return qlst
    elif YY_reader.LA() == "`" or YY_reader.LA() == "\\":
        qlst = cons(mksym("특이인용"), cons(nil, nil))
        _ = YY_reader.next_token()
        data = read_expr()
        qdta = cdr(qlst)
        qdta.setcar(data)
        return qlst
    elif YY_reader.LA() == ",@":
        qlst = cons(mksym("비인용연결"), cons(nil, nil))
        _ = YY_reader.next_token()
        data = read_expr()
        qdta = cdr(qlst)
        qdta.setcar(data)
        return qlst
    elif YY_reader.LA() == ",":
        qlst = cons(mksym("비인용"), cons(nil, nil))
        _ = YY_reader.next_token()
        data = read_expr()
        qdta = cdr(qlst)
        qdta.setcar(data)
        return qlst
    elif YY_reader.LA() == "":
        YY_reader.read2()
        _ = YY_reader.next_token()
        return read_expr()
    else:
        tok = YY_reader.LA()
        data = read_atom(tok)
        _ = YY_reader.next_token()
        return data
    # for test: return mkint(1910)

def read_atom(s: str) -> Data:
    """read an integer, nil, or a string"""
    if s.isdigit() or (len(s) > 1 and (s[0] == '+' or s[0] == '-') and s[1:].isdigit()):
        return mkint(int(s))
    elif s[:2] in ["0x", "0X"] and all(c in "0123456789abcedfABCDEF" for c in s[2:]):
        return mkint(int(s, base=16))
    elif s[:2] in ["0육"] and all(c in "0123456789abcedfABCDEFㄱㄴㄷㄹㅁㅂ" for c in s[2:]):
        new_lit = s.replace("0육", "0x")
        for i, lit in enumerate("ㄱㄴㄷㄹㅁㅂ"):
            new_lit = new_lit.replace(lit, chr(ord('A')+i))
        return mkint(int(new_lit, base=16))
    elif s == '공':
        return nil
    elif s[0] == '"':
        return mkstr(s)
    else:
        return mksym(s)         # 맞는 심볼만 검사 후 raise SyntaxError를 할 수도 있음

def read_list() -> Data:
    """read a list except the leading '('"""
    # YY_reader.match('(')          # 검사할 필요 없음
    YY_reader.nestin()
    YY_reader.next_token()
    result = nil
    if YY_reader.LA() == ')':
        YY_reader.nestout()
        YY_reader.match(')')
        YY_reader.next_token()
        return result
    fst = read_expr()  # retract tok and read an expression
    lst = cons(fst, nil)
    prev = lst
    while YY_reader.LA() != ')' and YY_reader.LA() != '.':
        elem = read_expr()    # using the lookahead
        last = cons(elem, nil)
        prev.setcdr(last)
        prev = last
    if YY_reader.LA() == '.':
        YY_reader.match('.')    #s1 = s2 였었나? YY_reader 도입 전에는 match('.')
        _ = YY_reader.next_token()
        elem = read_expr()
        prev.setcdr(elem)   # last.setcdr(elem)와 같음. 이 시점에서는 assert(_ == ')')
    YY_reader.nestout()
    YY_reader.match(')')
    YY_reader.next_token()
    return lst

def _main_p():
    """test function for parsing"""
    while (_ := YY_reader.read()) != "":
        try:
            _ = YY_reader.next_token()
            expr = read_expr()
            print(expr)
        except SyntaxError:
            print("Syntax Error")

if __name__ == "__main__":
    _main_p()
