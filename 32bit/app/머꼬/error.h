#ifndef _ERROR_H
#define _ERROR_H

typedef enum {
    ERR_NONE,       // 에러 없음
    ERR_SYNTAX,     // 구문 오류
    ERR_UNBOUND,    // 바인딩되지 않은 심볼
    ERR_ARGS,       // 인수 개수 오류
    ERR_TYPE        // 타입 오류
} ErrorType;

/* 전역 변수 선언 (extern) */
extern int g_has_error;
extern ErrorType g_err_type;

/* 함수 선언 */
void reset_error(void);
void raise_syntax_error(void);
void raise_unbound_error(char* sym);
void raise_args_error(char* fun);
void raise_type_error(char* fun);

#endif /* _ERROR_H */
