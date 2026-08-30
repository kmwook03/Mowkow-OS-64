
/* eval.c */
#include "eval.h"
#include "parse.h"
#include "error.h"
#include <string.h>
#include <stdio.h>
#include "../include/apilib.h"
#include "../include/mylib.h"

Value* global_env;

Value* mkenv(Value* parent) {
    return cons(parent, NIL);
}

Value* envget(Value* env, Value* symbol) {
    Value* parent = car(env);
    Value* binds = cdr(env);
    while (!is_nil(binds)) {
        Value* entry = car(binds);
        if (strcmp(car(entry)->as.sval, symbol->as.sval) == 0) {
            return cdr(entry);
        }
        binds = cdr(binds);
    }
    if (is_nil(parent)) {
        raise_unbound_error(symbol->as.sval);
        return NIL;
    }
    return envget(parent, symbol);
}

void envset(Value* env, Value* symbol, Value* val) {
    Value* binds = cdr(env);
    while (!is_nil(binds)) {
        Value* entry = car(binds);
        if (strcmp(car(entry)->as.sval, symbol->as.sval) == 0) {
            setcdr(entry, val);
            return;
        }
        binds = cdr(binds);
    }
    Value* new_entry = cons(symbol, val);
    setcdr(env, cons(new_entry, cdr(env)));
}

/* Built-in Implementations */
Value* builtin_car(Value* args) { return car(car(args)); }
Value* builtin_cdr(Value* args) { return cdr(car(args)); }
Value* builtin_cons(Value* args) { return cons(car(args), car(cdr(args))); }
Value* builtin_add(Value* args) {
    Value *a = car(args), *b = car(cdr(args));
    return mkint(a->as.ival + b->as.ival);
}

Value* builtin_sub(Value* args) {
    Value *a = car(args), *b = car(cdr(args));
    return mkint(a->as.ival - b->as.ival);
}

Value* builtin_mul(Value* args) {
    Value *a = car(args), *b = car(cdr(args));
    return mkint(a->as.ival * b->as.ival);
}

Value* builtin_div(Value* args) {
    Value *a = car(args), *b = car(cdr(args));
    if (b->as.ival == 0) {
        raise_type_error("div");
        return NIL;
    }
    return mkint(a->as.ival / b->as.ival);
}

Value* builtin_inteq(Value* args) {
    if (is_nil(args) || is_nil(cdr(args)) || !is_nil(cdr(cdr(args)))) {
        raise_args_error("=");
        return NIL;
    }

    Value* a = car(args);
    Value* b = car(cdr(args));

    if (!is_int(a) || !is_int(b)) {
        raise_type_error("=");
        return NIL;
    }

    if (a->as.ival == b->as.ival) {
        return mksym("#참");
    } else {
        return NIL;
    }
}

Value* builtin_intlt(Value* args) {
    if (is_nil(args) || is_nil(cdr(args)) || !is_nil(cdr(cdr(args)))) {
        raise_args_error("<");
        return NIL;
    }
    
    Value* a = car(args);
    Value* b = car(cdr(args));

    if (!is_int(a) || !is_int(b)) {
        raise_type_error("<");
        return NIL;
    }

    if (a->as.ival < b->as.ival) {
        return mksym("#참");
    } else {
        return NIL;
    }
}

Value* builtin_intgt(Value* args) {
    /* 1. 인자 개수 확인: 정확히 2개여야 함 (isbinary) */
    if (is_nil(args) || is_nil(cdr(args)) || !is_nil(cdr(cdr(args)))) {
        raise_args_error(">");
        return NIL;
    }

    Value* a = car(args);
    Value* b = car(cdr(args));

    /* 2. 타입 확인: 둘 다 정수여야 함 */
    if (!is_int(a) || !is_int(b)) {
        raise_type_error(">");
        return NIL;
    }

    /* 3. 비교 로직 */
    if (a->as.ival > b->as.ival) {
        return mksym("#참");
    } else {
        return NIL;
    }
}

Value* apply(Value* fn, Value* args) {
    if (fn->type == T_BUILTIN) return fn->as.fn(args);
    if (fn->type != T_CLOSURE) { raise_type_error("apply"); return NIL; }

    Value* env = mkenv(fn->as.clo.env);
    Value* params = fn->as.clo.params;
    Value* body = fn->as.clo.body;

    while (!is_nil(params)) {
        if (is_symbol(params)) { // Variadic
            envset(env, params, args);
            break;
        }
        envset(env, car(params), car(args));
        params = cdr(params);
        args = cdr(args);
    }

    Value* result = NIL;
    while (!is_nil(body)) {
        result = mk_eval(car(body), env);
        if (g_has_error) return NIL;
        body = cdr(body);
    }
    return result;
}

Value* builtin_apply(Value* args) {
    /* 1. 인자 개수 확인: 정확히 2개여야 함 (fn, list) */
    if (is_nil(args) || is_nil(cdr(args)) || !is_nil(cdr(cdr(args)))) {
        raise_args_error("적용");
        return NIL;
    }

    Value* fn = car(args);
    Value* list_args = car(cdr(args));

    /* 2. 타입 확인: 두 번째 인자는 반드시 리스트여야 함 */
    if (!is_list(list_args)) {
        raise_syntax_error();
        return NIL;
    }

    /* 3. 함수 적용 호출 */
    return apply(fn, list_args);
}

/* [헬퍼 함수] 두 값의 동등성 재귀 비교 (builtin_eq용) */
int check_equality(Value* a, Value* b) {
    /* 1. 포인터가 같으면(같은 객체면) 참 */
    if (a == b) return 1;

    /* 2. 타입이 다르면 거짓 */
    if (a->type != b->type) return 0;

    /* 3. 타입별 값 비교 */
    switch (a->type) {
        case T_INTEGER:
            return a->as.ival == b->as.ival;
        case T_SYMBOL:
        case T_STRING:
            return strcmp(a->as.sval, b->as.sval) == 0;
        case T_NIL:
            return 1; /* 둘 다 NIL (위의 타입 체크 통과) */
        case T_BUILTIN:
            return a->as.fn == b->as.fn;
        case T_PAIR:
            /* 리스트는 재귀적으로 car와 cdr 비교 */
            return check_equality(car(a), car(b)) && check_equality(cdr(a), cdr(b));
        case T_CLOSURE:
        case T_MACRO:
            /* 클로저는 환경(포인터), 인자(리스트), 바디(리스트) 비교 */
            return (a->as.clo.env == b->as.clo.env) &&
                   check_equality(a->as.clo.params, b->as.clo.params) &&
                   check_equality(a->as.clo.body, b->as.clo.body);
        default:
            return 0;
    }
}

Value* builtin_eq(Value* args) {
    /* 1. 인자 개수 확인: 정확히 2개여야 함 (isbinary) */
    if (is_nil(args) || is_nil(cdr(args)) || !is_nil(cdr(cdr(args)))) {
        raise_args_error("같다?");
        return NIL;
    }

    Value* a = car(args);
    Value* b = car(cdr(args));

    /* 2. 동등성 검사 수행 */
    if (check_equality(a, b)) {
        return mksym("#참");
    } else {
        return NIL;
    }
}

Value* builtin_ispair(Value* args) {
    /* 1. 인자 개수 확인: 정확히 1개여야 함 (isunary) */
    if (is_nil(args) || !is_nil(cdr(args))) {
        raise_args_error("짝?");
        return NIL;
    }

    /* 2. 대상 값 추출 */
    Value* target = car(args);

    /* 3. 타입 확인 (is_pair는 data.c에 정의됨) */
    if (is_pair(target)) {
        return mksym("#참");
    } else {
        return NIL;
    }
}

Value* builtin_isnil(Value* args) {
    /* 1. 인자 개수 확인: 정확히 1개여야 함 (isunary) */
    if (is_nil(args) || !is_nil(cdr(args))) {
        raise_args_error("공?");
        return NIL;
    }

    /* 2. 대상 값 추출 */
    Value* target = car(args);

    /* 3. NIL 여부 확인 (is_nil은 data.c에 정의됨) */
    if (is_nil(target)) {
        return mksym("#참");
    } else {
        return NIL;
    }
}

Value* builtin_not(Value* args) {
    /* 1. 인자 개수 확인: 정확히 1개여야 함 (isunary) */
    if (is_nil(args) || !is_nil(cdr(args))) {
        raise_args_error("부정");
        return NIL;
    }

    /* 2. 대상 값 추출 */
    Value* target = car(args);

    /* 3. 논리 부정: NIL이면 #참, 그 외(참)면 NIL 반환 */
    if (is_nil(target)) {
        return mksym("#참");
    } else {
        return NIL;
    }
}

Value* builtin_and(Value* args) {
    /* 1. 인자 개수 확인: 정확히 2개여야 함 (isbinary) */
    if (is_nil(args) || is_nil(cdr(args)) || !is_nil(cdr(cdr(args)))) {
        raise_args_error("그리고");
        return NIL;
    }

    Value* a = car(args);
    Value* b = car(cdr(args));

    /* 2. 논리 처리 */
    /* 첫 번째 인자가 심볼 "#참"이면 두 번째 값 반환 */
    if (is_symbol(a) && strcmp(a->as.sval, "#참") == 0) {
        return b;
    } 
    /* 첫 번째 인자가 NIL(공/거짓)이면 NIL 반환 */
    else if (is_nil(a)) {
        return NIL;
    } 
    /* 그 외의 경우 타입 에러 (Strict Type Check) */
    else {
        raise_type_error("그리고");
        return NIL;
    }
}

Value* builtin_or(Value* args) {
    /* 1. 인자 개수 확인: 정확히 2개여야 함 (isbinary) */
    if (is_nil(args) || is_nil(cdr(args)) || !is_nil(cdr(cdr(args)))) {
        raise_args_error("또는");
        return NIL;
    }

    Value* a = car(args);
    Value* b = car(cdr(args));

    /* 2. 논리 처리 */
    /* 첫 번째 값이 NIL(거짓)이면 두 번째 값 반환 */
    if (is_nil(a)) {
        return b;
    } 
    /* 첫 번째 값이 참이면 첫 번째 값 반환 */
    else {
        return a;
    }
}

void trim(char *s) {
    char *p = s;
    int l = strlen(p);

    // 1. 뒤쪽 공백/개행 제거
    while (l > 0 && isspace(p[l - 1])) {
        p[--l] = 0;
    }

    // 2. 앞쪽 공백 제거
    while (*p && isspace(*p)) {
        ++p, --l;
    }

    // 3. 문자열 앞으로 당기기
    if (p != s) {
        memmove(s, p, l + 1);
    }
}

Value* builtin_read(Value* args) {
    if (!is_nil(args)) {
        raise_args_error("읽기");
        return NIL;
    }

    printf("입력>  ");
    char buf[256];

    while (1) {
        gets(buf); 
        trim(buf);
        if (strlen(buf) > 0) break;
    }

    int len = my_strlen(buf);
    if (len == 0) return NIL;

    if (len > 1 && buf[0] == '0' && buf[1] != 'x' && buf[1] != 'X' && is_digits(buf, 8)) {
        return mkint((int)strtol(buf, 0, 8));
    }
    
    if (len > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
        if (is_digits(buf + 2, 16)) {
            return mkint((int)strtol(buf + 2, NULL, 16));
        }
    }

    if (strncmp(buf, "0육", my_strlen("0육")) == 0) {
        // "0육" 부분을 "0x"로 간주하고 뒤의 한글(ㄱ~ㅂ)을 A~F로 매핑해야 함.
        // C언어에서 UTF-8 문자열 치환은 복잡하므로,
        // 임시 버퍼에 변환된 문자열을 만들어서 strtol에 넘깁니다.
        
        char hexbuf[256];
        int i = my_strlen("0육"); // "0육" 다음부터 읽기
        int j = 0;
        int valid = 1;

        while (buf[i] != '\0' && j < 250) {
            unsigned char c = (unsigned char)buf[i];
            
            // 숫자나 a-f, A-F는 그대로 복사
            if (isxdigit(c)) {
                hexbuf[j++] = c;
                i++;
            } 
            // 한글 ㄱ~ㅂ 처리 (UTF-8 3바이트)
            // ㄱ: E3 84 B1 -> A
            // ㄴ: E3 84 B4 -> B
            // ㄷ: E3 84 B7 -> C
            // ㄹ: E3 84 B9 -> D
            // ㅁ: E3 85 81 -> E
            // ㅂ: E3 85 82 -> F
            else if (c == 0xE3) {
                if (strncmp(&buf[i], "ㄱ", 3) == 0) { hexbuf[j++] = 'A'; i+=3; }
                else if (strncmp(&buf[i], "ㄴ", 3) == 0) { hexbuf[j++] = 'B'; i+=3; }
                else if (strncmp(&buf[i], "ㄷ", 3) == 0) { hexbuf[j++] = 'C'; i+=3; }
                else if (strncmp(&buf[i], "ㄹ", 3) == 0) { hexbuf[j++] = 'D'; i+=3; }
                else if (strncmp(&buf[i], "ㅁ", 3) == 0) { hexbuf[j++] = 'E'; i+=3; }
                else if (strncmp(&buf[i], "ㅂ", 3) == 0) { hexbuf[j++] = 'F'; i+=3; }
                else { valid = 0; break; }
            } else {
                valid = 0; break;
            }
        }
        hexbuf[j] = '\0';

        if (valid) {
            return mkint((int)strtol(hexbuf, NULL, 16));
        }
    }

    if (is_digits(buf, 10)) {
        return mkint(atoi(buf));
    }

    // (5) 문자열: 양 끝이 " 인 경우
    if (len >= 2 && buf[0] == '"' && buf[len-1] == '"') {
        buf[len-1] = '\0'; // 뒷 따옴표 제거
        return mkstr(buf + 1); // 앞 따옴표 건너뜀
    }

    // (6) 심볼: 알파벳 등
    return mksym(buf);
}

Value* builtin_write(Value* args) {
    // 1. 인자 확인: 단항 (isunary)
    if (is_nil(args) || !is_nil(cdr(args))) {
        raise_args_error("쓰기"); // 파이썬 코드에선 fname="출력" 이지만 함수명은 쓰기
        return NIL;
    }

    Value* a = car(args);

    printf("출력>  ");
    // 2. 타입별 출력
    switch (a->type) {
        case T_INTEGER:
            printf("%d\n", a->as.ival);
            break;
        case T_SYMBOL:
            printf("%s\n", a->as.sval);
            break;
        case T_BUILTIN:
            // print_value는 #<Builtin..>을 찍지만, 쓰기는 그냥 내용을 찍는게 목표라면 확인 필요
            // 파이썬 코드: print(a, end="") -> __str__ 호출
            print_value(a); 
            printf("\n");
            break;
        case T_PAIR:
            print_value(a);
            break;
        case T_STRING:
            printf("%s\n", a->as.sval);
            break;
        case T_NIL:
            printf("()\n");
            break;
        default:
            // Closure, Macro 등
            print_value(a);
            printf("\n");
            break;
    }

    return NIL;
}


Value* mk_eval(Value* expr, Value* env) {
    if (g_has_error) return NIL;
    if (is_symbol(expr)) return envget(env, expr);
    if (!is_pair(expr)) return expr;
    // 순차 수식(Improper list) 체크가 필요하다면 여기서 is_list(expr) 호출

    Value* fun = car(expr);
    Value* args = cdr(expr);

    if (is_symbol(fun)) {
        char* fname = fun->as.sval;

        /* [인용] */
        if (strcmp(fname, "인용") == 0) return car(args);

        /* [정의] */
        if (strcmp(fname, "정의") == 0) {
            Value *sym = car(args);
            Value *val;
            if (is_pair(sym)) { // (정의 (f x) ...)
                 val = mkclosure(env, cdr(sym), cdr(args));
                 sym = car(sym);
            } else {
                 val = mk_eval(car(cdr(args)), env);
            }
            envset(env, sym, val);
            return NIL; // Defined symbol name
        }

        /* [만약] */
        if (strcmp(fname, "만약") == 0) {
            Value* cond = mk_eval(car(args), env);
            if (g_has_error) return NIL;
            if (is_nil(cond)) return mk_eval(car(cdr(cdr(args))), env);
            else return mk_eval(car(cdr(args)), env);
        }

        /* [람다] */
        if (strcmp(fname, "람다") == 0) {
            return mkclosure(env, car(args), cdr(args));
        }

        /* [조건] (cond) */
        if (strcmp(fname, "조건") == 0) {
            Value* clause_list = args;
            while (!is_nil(clause_list)) {
                Value* clause = car(clause_list);
                // 구문 체크: 절은 리스트여야 함
                if (!is_pair(clause)) { raise_syntax_error(); return NIL; }

                Value* cond_res = mk_eval(car(clause), env);
                if (g_has_error) return NIL;

                // 조건이 참(NIL이 아님)이면 해당 값 평가 후 반환
                if (!is_nil(cond_res)) {
                    return mk_eval(car(cdr(clause)), env);
                }
                clause_list = cdr(clause_list);
            }
            return NIL; // 모든 조건이 거짓이면 NIL
        }

        /* [매크로] (defmacro) */
        if (strcmp(fname, "매크로") == 0) {
            // (매크로 (이름 인자) 바디) 구조 확인
            if (is_nil(args) || is_nil(car(args))) { raise_args_error("매크로"); return NIL; }
            
            Value* header = car(args); // (이름 인자)
            if (!is_pair(header)) { raise_syntax_error(); return NIL; }

            Value* name = car(header);
            if (!is_symbol(name)) { raise_type_error("매크로"); return NIL; }

            // 매크로 객체 생성 및 환경 등록
            Value* macro = mkmacro(env, cdr(header), cdr(args));
            envset(env, name, macro);
            return NIL; // 반환값 없음 (혹은 name 반환)
        }

        /* [잠시] (let) - 추가됨 */
        if (strcmp(fname, "잠시") == 0) {
            // (잠시 ((이름 값) ...) 바디)
            if (is_nil(args) || is_nil(cdr(args))) { raise_args_error("잠시"); return NIL; }

            Value* binds = car(args);
            Value* body = car(cdr(args));
            Value* local_env = mkenv(env); // 새 스코프 생성

            while (!is_nil(binds)) {
                Value* pair = car(binds);
                if (!is_pair(pair)) { raise_syntax_error(); return NIL; }

                Value* sym = car(pair);
                if (!is_symbol(sym)) { raise_type_error("잠시"); return NIL; }

                // 값 평가 (파이썬 코드 로직에 따라 local_env 사용 -> let* 방식)
                Value* val_expr = car(cdr(pair));
                Value* val = mk_eval(val_expr, local_env); 
                if (g_has_error) return NIL;

                envset(local_env, sym, val);
                binds = cdr(binds);
            }
            // 바디 평가
            return mk_eval(body, local_env);
        }
    }

    /* 일반 함수 호출 처리 */
    Value* fn = mk_eval(fun, env);
    if (g_has_error) return NIL;
    
    if (fn->type == T_MACRO) {
        /* 매크로 내부 구조 추출 */
        Value* menv = fn->as.clo.env;
        Value* mparams = fn->as.clo.params;
        Value* mbody = fn->as.clo.body;
        
        /* 매크로 확장을 위한 임시 클로저 생성 (apply 함수 재사용을 위해) */
        Value* expander = mkclosure(menv, mparams, mbody);
        
        /* 매크로는 인자를 평가하지 않고(args 그대로) 적용하여 확장된 코드를 얻음 */
        Value* expansion = apply(expander, args);
        if (g_has_error) return NIL;
        
        /* 확장된 코드를 현재 환경에서 다시 평가 */
        return mk_eval(expansion, env);
    }

    // Evaluate arguments
    Value* ev_args = cplist(args);
    Value* p = ev_args;
    while (!is_nil(p)) {
        setcar(p, mk_eval(car(p), env));
        if (g_has_error) return NIL;
        p = cdr(p);
    }
    
    return apply(fn, ev_args);
}

void init_eval(void) {
    global_env = mkenv(NIL);
    
    /* 기존 등록 함수들 */
    envset(global_env, mksym("머"), mkbuiltin(builtin_car));
    envset(global_env, mksym("꼬"), mkbuiltin(builtin_cdr));
    envset(global_env, mksym("짝"), mkbuiltin(builtin_cons));
    envset(global_env, mksym("+"), mkbuiltin(builtin_add));
    envset(global_env, mksym("-"), mkbuiltin(builtin_sub));
    envset(global_env, mksym("*"), mkbuiltin(builtin_mul));
    envset(global_env, mksym("/"), mkbuiltin(builtin_div));
    envset(global_env, mksym("env"), mkbuiltin(builtin_print_env));
    envset(global_env, mksym("#참"), mksym("#참"));
    
    /* 논리/비교 함수 */
    envset(global_env, mksym("="), mkbuiltin(builtin_inteq));
    envset(global_env, mksym("<"), mkbuiltin(builtin_intlt));
    envset(global_env, mksym(">"), mkbuiltin(builtin_intgt));
    envset(global_env, mksym("같다?"), mkbuiltin(builtin_eq));
    envset(global_env, mksym("짝?"), mkbuiltin(builtin_ispair));
    envset(global_env, mksym("공?"), mkbuiltin(builtin_isnil));
    envset(global_env, mksym("부정"), mkbuiltin(builtin_not));
    envset(global_env, mksym("그리고"), mkbuiltin(builtin_and));
    envset(global_env, mksym("또는"), mkbuiltin(builtin_or));
    envset(global_env, mksym("읽기"), mkbuiltin(builtin_read));
    envset(global_env, mksym("쓰기"), mkbuiltin(builtin_write));
    
    /* 기능 함수 */
    envset(global_env, mksym("적용"), mkbuiltin(builtin_apply));
}

/* [DEBUG] 환경 변수 전체 출력 함수 */
void print_env_scope(Value* env) {
    if (is_nil(global_env)) {
        printf("Environment is NIL.\n");
        return;
    }

    Value* binds = cdr(global_env); // (parent . binds) 구조에서 binds 가져오기
    int count = 0;

    printf("--- Current Scope Variables ---\n");
    
    while (!is_nil(binds)) {
        Value* entry = car(binds); // entry는 (key . value) 쌍
        
        if (is_pair(entry)) {
            Value* key = car(entry);
            Value* val = cdr(entry);

            printf("[%d] ", count++);
            
            // 키 출력
            if (is_symbol(key)) {
                printf("%s", key->as.sval);
            } else {
                print_value(key);
            }

            printf(" = ");

            // 값 출력 (값이 너무 길면 ... 처리 하거나 그냥 다 출력)
            print_value(val); 
            printf("\n");
        }
        
        binds = cdr(binds); // 다음 바인딩으로 이동
    }
    printf("-------------------------------\n");
}

/* 내장 함수로 등록하기 위한 래퍼 */
Value* builtin_print_env(Value* args) {
    // (환경) 호출 시 인자는 무시하고 현재 global_env를 출력하거나,
    // 클로저 내부 확인용으로 쓸 수 있습니다.
    // 여기서는 편의상 global_env를 출력합니다.
    print_env_scope(global_env);
    return NIL;
}
