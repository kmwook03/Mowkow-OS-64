#coding: utf-8
"""머꼬 오류의 구조화된 인수와 콘솔별 표시 정책."""

import mowio
from _compat import eprint

IsVerbose = False

# Message IDs name fixed formats, never user text. Select before interpolation.
_MESSAGES = {
    "syntax": ("구문 오류", "syntax error"),
    "unbound": ("이름 '%s'을(를) 찾을 수 없습니다.", "name '%s' is not defined"),
    "args": ("함수 %s: 인수 개수 오류입니다.", "function %s: wrong number of arguments"),
    "type": ("함수 %s: 타입 오류입니다.", "function %s: type error"),
    "error": ("오류: %s", "error: %s"),
    "lexical": ("토큰 '%s' 부근에서 어휘 오류", "lexical error near token '%s'"),
    "syntax_token": ("구문 오류: '%s'", "syntax error: '%s'"),
    "builtin": ("<내장함수 '%s'>", "<builtin function '%s'>"),
}


def error_mode():
    mode = mowio.input_mode()
    return mode >= 0 and (mode & 1) != 0


def error_message(message_id, values, korean):
    return _MESSAGES[message_id][0 if korean else 1] % values


class ErrLisp(Exception):
    pass


class ErrSyntax(ErrLisp):
    def __init__(self):
        pass

    def __str__(self):
        return error_message("syntax", (), True)


class ErrUnbound(ErrLisp):
    def __init__(self, sym):
        self.symname = sym

    def __str__(self):
        return error_message("unbound", (self.symname,), True)


class ErrArgs(ErrLisp):
    def __init__(self, fun, builtin=False):
        self.funname = fun
        self.builtin = builtin

    def __str__(self):
        return error_message("args", (function_label(self, True),), True)


class ErrType(ErrLisp):
    def __init__(self, fun, builtin=False):
        self.funname = fun
        self.builtin = builtin

    def __str__(self):
        return error_message("type", (function_label(self, True),), True)


def function_label(err, korean):
    if err.builtin:
        return error_message("builtin", (err.funname,), korean)
    return err.funname


def report_error(err, path=None, line=None):
    korean = error_mode()
    if type(err) is ErrUnbound:
        text = error_message("unbound", (err.symname,), korean)
    elif type(err) is ErrArgs:
        text = error_message("args", (function_label(err, korean),), korean)
    elif type(err) is ErrType:
        text = error_message("type", (function_label(err, korean),), korean)
    elif type(err) is ErrSyntax or (type(err) is SyntaxError and not err.args):
        text = error_message("syntax", (), korean)
    else:
        text = str(err)
    message = error_message("error", (text,), korean)
    if path is not None:
        message = "%s:%d: %s" % (path, line, message)
    eprint(message)


def report_lexical(token):
    korean = error_mode()
    text = error_message("lexical", (token,), korean)
    eprint(error_message("error", (text,), korean))


def report_syntax(path, line, column, token):
    korean = error_mode()
    text = error_message("syntax_token", (token,), korean)
    eprint("%s:%d:%d: %s" % (path, line, column, text))
