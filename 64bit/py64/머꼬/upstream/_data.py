#coding: utf-8

# Ph 4(eval): Pair.car() and Pair.cdr()
#             global car(Data) and cdr(Data)
#             Symbol.value(self)
#             global islist(Data): recursive
#             Data.issymbol(), Data.isint(), Symbol.issymbol(), Int.isint()
#             islist(Data)
# bug: Pair의 함수 이름과 필드 이름을 똑같이 car, cdr로 설정했었음.
#      -> 필드를 _car, _cdr로 바꿈

# Ph 5(car):    class Builtin
#               global mkbuiltin, cplist
#               Pair.setcar(Data)
#               Integer.value()

# Ph 7(clo):    class Closure
#               Data.isclosure()
#               global allsymbols(Data)
#               from _error import *

# Ph 10(var):   Closure.__init__ does not use allsymbols
#               but checks the parameter types manually

# Ph 11(macro): Macro(Data) class is a copycat of Closure(Data)
#               Macro.isclosure()를 Macro.ismacro()로 대체
#               Data.ismacro() 추가
#               Macro 생성자를 호출하는 전역 함수 mkmacro 추가

from _error import ErrType, ErrSyntax

class Data:
    """클래스 Data는 머꼬의 모든 데이터 클래스의 부모 클래스입니다."""
    def __init__(self):
        """Data 클래스는 추상 클래스이므로 비워 두었습니다."""
    def ispair(self) -> bool:
        """짝(pair)인지 점검하는 함수. 짝이면 참"""
        return False
    def isnil(self) -> bool:
        """공(nil)인지 점검하는 함수. 공이면 참"""
        return False
    def issymbol(self) -> bool:
        return False
    def isint(self) -> bool:
        return False
    def isstr(self) -> bool:
        return False
    def isbuiltin(self) -> bool:
        return False
    def isclosure(self) -> bool:
        return False
    def ismacro(self) -> bool:
        return False
    def value(self) -> None:
        """ place holder for value """    
        return None
    def car(self) -> None:
        """place holder for car"""
        return None
    def cdr(self) -> None:
        """place holder for cdr"""
        return None

class Nil(Data):
    def __init__(self):
        pass
    def __str__(self):
        return "공"
    def isnil(self) -> bool:
        return True

class Pair(Data):
    def __init__(self, car_val: Data, cdr_val: Data):
        self._car, self._cdr = car_val, cdr_val
    def ispair(self) -> bool:
        return True
    def __str__(self) -> str:
        rval = f"({self._car}"
        tail = self._cdr
        while not tail.isnil():
            if tail.ispair():               # Bug230111: self.cdr.ispair() -> tail.ispair()
                rval += f" {tail._car}"     # type: ignore
                tail = tail._cdr            # type: ignore
            else:
                rval += f" . {tail}"
                break                       # break가 꼭 필요함
        rval += ")"
        return rval
    def setcar(self, car_val: Data) -> None:
        self._car = car_val
    def setcdr(self, cdr_val: Data) -> None:
        self._cdr = cdr_val
    def car(self) -> Data:
        return self._car
    def cdr(self) -> Data:
        return self._cdr

class Symbol(Data):
    def __init__(self, s: str):
        self._val = s
    def issymbol(self) -> bool:
        return True
    def __str__(self) -> str:
        return self._val
    def value(self) -> str:
        return self._val

class Integer(Data):
    def __init__(self, i: int):
        self._val = i
    def isint(self) -> bool:
        return True
    def __str__(self) -> str:
        return str(self._val)
    def value(self) -> int:
        return self._val

class String(Data):
    def __init__(self, s: str):
        self._val = s
    def isstr(self) -> bool:
        return True
    def __str__(self) -> str:
        return self._val                # Des250429: f'"{self._val}"'로 했다가 바꾸었음
    def value(self) -> str:
        return self._val

class Builtin(Data):
    def __init__(self, fn):
        self._fun = fn
    def isbuiltin(self) -> bool:
        return not super().isbuiltin()
    def __str__(self) -> str:
        fname = f"{self._fun}".split()[2]
        return f"#<내장 함수: {fname}>"
    def fun(self):
        return self._fun

class Closure(Data):
    def __init__(self, env: Data, params: Data, body: Data):
        ## commented out for ch 10 (variadic)
        # if not islist(params) or not islist(body):
        #     raise ErrSyntax()      ####
        # allsymbols(params)
        if not islist(body):
            raise ErrSyntax()
        p = params
        while not p.isnil():
            if p.issymbol():    # the last symbol such as c in (f a b . c)
                break
            elif not p.ispair() or not car(p).issymbol():
                raise ErrType("<#클로저>")
            p = cdr(p)          # pyright: ignore[reportArgumentType]
        self._val = cons(env, cons(params, body))
    def isclosure(self) -> bool:
        return not super().isclosure()
    def __str__(self) -> str:
        return f"#<클로저>"
    def val(self) -> Data:          # returns the closure value (lambda)
        return self._val
    def env(self) -> Data:
        return car(self._val)       # pyright: ignore[reportArgumentType]
    def params(self) -> Data:
        return car(cdr(self._val))  # pyright: ignore[reportArgumentType]
    def body(self) -> Data:
        return cdr(cdr(self._val))  # pyright: ignore[reportArgumentType]

class Macro(Data):
    def __init__(self, env: Data, params: Data, body: Data):
        if not islist(body):
            raise ErrSyntax()
        p = params
        while not p.isnil():
            if p.issymbol():    # the last symbol such as c in (f a b . c)
                break
            elif not p.ispair() or not car(p).issymbol():
                raise ErrType("<#매크로>")
            p = cdr(p)          # pyright: ignore[reportArgumentType]
        self._val = cons(env, cons(params, body))
    def ismacro(self) -> bool:
        return True
    def __str__(self) -> str:
        return f"#<매크로>"
    def val(self) -> Data:          # returns the closure value (lambda)
        return self._val

def cons(car_val: Data, cdr_val: Data) -> Data:
    return Pair(car_val, cdr_val)

def car(p: Data) -> Data:
    return p.car()

def cdr(p: Data) -> Data:
    return p.cdr()

def mkint(ival: int) -> Data:
    return Integer(ival)

def mksym(sval: str) -> Data:
    return Symbol(sval)

def mkstr(sval: str) -> Data:
    return String(sval)

def mkbuiltin(fn) -> Data:
    return Builtin(fn)

def mkclosure(env: Data, params: Data, body: Data) -> Data:
    return Closure(env, params, body)

def mkmacro(env: Data, params: Data, body: Data) -> Data:
    return Macro(env, params, body)


nil = Nil()

def islist(d: Data) -> bool:
    if d.isnil():
        return True
    else:
        return d.ispair() and islist(cdr(d))        # pyright: ignore[reportArgumentType] 
        # Bug_20230104_a: cdr(d).ispair() -> islist(cdr(d))

def cplist(ls: Data) -> Data:
    if ls.isnil():
        return nil
    head = cons(car(ls), nil)               # pyright: ignore[reportArgumentType]
    prev = head
    ls = cdr(ls)                            # pyright: ignore[reportArgumentType]
    while not ls.isnil():
        prev.setcdr(cons(car(ls), nil))     # pyright: ignore[reportAttributeAccessIssue, reportArgumentType]
        prev = cdr(prev)        # pyright: ignore[reportArgumentType]
        ls = cdr(ls)            # pyright: ignore[reportArgumentType]
    return head

def allsymbols(params: Data) -> None:
    p = params
    while not p.isnil():
        if not car(p).issymbol():       # pyright: ignore[reportArgumentType]
            raise ErrType("<#클로저>")
        p = cdr(p)                      # pyright: ignore[reportArgumentType]

if __name__ == '__main__':
    print(mkint(1910))
    print(mksym("안녕"))
    print(mkstr("안녕, 문자열?"))
    print(cons(mkint(8), cons(mkint(2), cons(mkint(9), nil))))
    print(cons(mkint(3), mkint(4)))
