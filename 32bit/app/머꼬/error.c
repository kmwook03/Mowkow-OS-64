
/* error.c */
#include <stdio.h>
#include "error.h"

int g_has_error = 0;
ErrorType g_err_type = ERR_NONE;

void reset_error(void) {
    g_has_error = 0;
    g_err_type = ERR_NONE;
}

static void set_error(ErrorType type) {
    g_has_error = 1;
    g_err_type = type;
}

void raise_syntax_error(void) {
    set_error(ERR_SYNTAX);
    printf("[Error] 구문 오류\n");
}

void raise_unbound_error(char* sym) {
    set_error(ERR_UNBOUND);
    printf("[Error] 이름 '%s'을(를) 찾을 수 없습니다.\n", sym);
}

void raise_args_error(char* fun) {
    set_error(ERR_ARGS);
    printf("[Error] 함수 %s: 인수 개수 오류입니다.\n", fun);
}

void raise_type_error(char* fun) {
    set_error(ERR_TYPE);
    printf("[Error] 함수 %s: 타입 오류입니다.\n", fun);
}
