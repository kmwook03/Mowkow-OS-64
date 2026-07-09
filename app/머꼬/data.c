#include "data.h"
#include "../include/mylib.h"
#include <stdio.h>

#define MAX_NODES 20000
#define STR_POOL_SIZE 20000 /* 문자열 저장용 메모리 풀 크기 */

static Value node_pool[MAX_NODES];
static int pool_top = 0;

/* 문자열 풀 (malloc 대신 사용) */
static char str_pool[STR_POOL_SIZE];
static int str_top = 0;

static Value nil_instance;
Value *NIL;

void init_mowkow(void) {
    pool_top = 0;
    str_top = 0; /* 초기화 */
    
    nil_instance.type = T_NIL;
    NIL = &nil_instance;
}

static Value* alloc_val(void) {
    if (pool_top >= MAX_NODES) {
        printf("[PANIC] Out of Memory (Nodes)!\n");
        while(1);
    }
    return &node_pool[pool_top++];
}

/* 문자열 할당 함수 */
static char* alloc_str(char* s) {
    int len = strlen(s);
    if (str_top + len + 1 >= STR_POOL_SIZE) {
        printf("[PANIC] Out of Memory (String Pool)!\n");
        while(1);
    }
    
    char* dest = &str_pool[str_top];
    strcpy(dest, s);
    str_top += len + 1; // 길이 + NULL문자
    return dest;
}

Value* mkint(int v) {
    Value* n = alloc_val();
    n->type = T_INTEGER;
    n->as.ival = v;
    return n;
}

Value* mksym(char* s) {
    Value* n = alloc_val();
    n->type = T_SYMBOL;
    
    /* 문자열 할당 함수 사용 */
    n->as.sval = alloc_str(s);
    
    return n;
}

Value* mkstr(char* s) {
    Value* n = alloc_val();
    n->type = T_STRING;
    
    /* 문자열 할당 함수 사용 */
    n->as.sval = alloc_str(s);
    
    return n;
}

Value* mkbuiltin(BuiltinFunc fn) {
    Value* n = alloc_val();
    n->type = T_BUILTIN;
    n->as.fn = fn;
    return n;
}

Value* cons(Value* car_val, Value* cdr_val) {
    Value* n = alloc_val();
    n->type = T_PAIR;
    n->as.pair.car = car_val;
    n->as.pair.cdr = cdr_val;
    return n;
}

Value* mkclosure(Value* env, Value* params, Value* body) {
    Value* n = alloc_val();

    if (!is_list(body)) {
        printf("[SyntaxError] Body must be a list.\n");
        return NIL;
    }
    
    n->type = T_CLOSURE;
    n->as.clo.env = env;
    n->as.clo.params = params;
    n->as.clo.body = body;
    return n;
}

Value* mkmacro(Value* env, Value* params, Value* body) {
    Value* n = alloc_val();
    
    if (!is_list(body)) {
        printf("[SyntaxError] Macro body must be a list.\n");
        return NIL;
    }

    n->type = T_MACRO;
    n->as.clo.env = env;
    n->as.clo.params = params;
    n->as.clo.body = body;
    return n;
}

Value* car(Value* v) {
    if (v->type != T_PAIR) return NIL;
    return v->as.pair.car;
}

Value* cdr(Value* v) {
    if (v->type != T_PAIR) return NIL;
    return v->as.pair.cdr;
}

void setcar(Value* v, Value* new_car) {
    if (v->type == T_PAIR) v->as.pair.car = new_car;
}

void setcdr(Value* v, Value* new_cdr) {
    if (v->type == T_PAIR) v->as.pair.cdr = new_cdr;
}

int is_nil(Value* v)    { return v == NIL || v->type == T_NIL; }
int is_pair(Value* v)   { return v->type == T_PAIR; }
int is_symbol(Value* v) { return v->type == T_SYMBOL; }
int is_int(Value* v)    { return v->type == T_INTEGER; }
int is_str(Value* v)    { return v->type == T_STRING; }

int is_list(Value* v) {
    if (is_nil(v)) return 1;
    return is_pair(v) && is_list(cdr(v));
}

Value* cplist(Value* ls) {
    if (is_nil(ls)) return NIL;
    
    Value* head = cons(car(ls), NIL);
    Value* prev = head;
    ls = cdr(ls);
    
    while (!is_nil(ls)) {
        Value* new_node = cons(car(ls), NIL);
        setcdr(prev, new_node);
        prev = new_node;
        ls = cdr(ls);
    }
    return head;
}

void print_value(Value* v) {
    if (is_nil(v)) {
        return;
    }

    switch (v->type) {
        case T_INTEGER:
            printf("%d", v->as.ival);
            break;
        case T_SYMBOL:
            printf("%s", v->as.sval);
            break;
        case T_STRING:
            printf("\"%s\"", v->as.sval);
            break;
        case T_BUILTIN:
            printf("#<Builtin>");
            break;
        case T_CLOSURE:
            printf("#<Closure>");
            break;
        case T_MACRO:
            printf("#<Macro>");
            break;
        case T_PAIR:
            printf("(");
            print_value(car(v));
            
            Value* tail = cdr(v);
            while (!is_nil(tail)) {
                if (is_pair(tail)) {
                    printf(" ");
                    print_value(car(tail));
                    tail = cdr(tail);
                } else {
                    printf(" . ");
                    print_value(tail);
                    break;
                }
            }
            printf(")");
            break;
        default:
            printf("?");
    }
}
