#include "parse.h"
#include "error.h"
#include "../include/mylib.h"
#include <stdio.h>

Reader YY_reader;

int is_whitespace(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

int is_delimiter(char c) {
    return (is_whitespace(c) || c == '(' || c == ')' || c == '"' || c == '\'' || c == '`' || c == ',' || c == ';');
}

void init_reader(void) {
    YY_reader.pos = 0;
    YY_reader.depth = 0;
    YY_reader.buf[0] = '\0';
    YY_reader.token[0] = '\0';
}

void reader_fill_buffer(void) {
    if (YY_reader.depth > 0) {
        printf("..     "); 
    } else {
        printf(">  ");
    }

    char temp[BUFFER_SIZE];
    gets(temp); 

    strcpy(YY_reader.buf, temp);
    
    // gets는 개행 문자를 제거하고 줄 수 있으므로, 파서 동작을 위해 공백 추가
    int len = strlen(YY_reader.buf);
    
    // 버퍼가 꽉 찼을 경우 안전장치
    if (len >= BUFFER_SIZE - 2) len = BUFFER_SIZE - 3;

    YY_reader.buf[len] = ' '; 
    YY_reader.buf[len+1] = '\0';
    
    YY_reader.pos = 0;
}

char* next_token(void) {
    char *s = YY_reader.buf;
    int *i = &YY_reader.pos;
    int tok_idx = 0;

    /* 버퍼 끝 처리 */
    while (s[*i] == '\0') {
        // 괄호 깊이가 0이면 더 이상 입력을 요구하지 않음 (REPL 반응성)
        if (YY_reader.depth == 0) {
            YY_reader.token[0] = '\0';
            return YY_reader.token;
        }
        reader_fill_buffer();
        if (s[0] == '\0') return ""; // EOF 처리
        *i = 0;
    }

    /* 공백 스킵 */
    while (is_whitespace(s[*i])) {
        (*i)++;
        if (s[*i] == '\0') {
            if (YY_reader.depth == 0) {
                YY_reader.token[0] = '\0';
                return YY_reader.token;
            }
            reader_fill_buffer();
            *i = 0;
        }
    }

    /* 주석 처리 */
    if (s[*i] == ';') {
        while (s[*i] != '\0' && s[*i] != '\n') {
            (*i)++;
        }
        // 재귀 호출 대신 루프로 처리하거나 다시 next_token 호출
        return next_token();
    }

    char c = s[*i];
    
    /* ,@ 처리 */
    if (c == ',' && s[*i+1] == '@') {
        strcpy(YY_reader.token, ",@");
        (*i) += 2;
        return YY_reader.token;
    }

    /* 단일 문자 토큰 */
    if (c == '(' || c == ')' || c == '\'' || c == '`' || c == ',' || c == '.') {
        YY_reader.token[0] = c;
        YY_reader.token[1] = '\0';
        (*i)++;
        return YY_reader.token;
    }

    /* 문자열 리터럴 */
    if (c == '"') {
        (*i)++;
        YY_reader.token[tok_idx++] = '"';
        
        while (s[*i] != '"' && s[*i] != '\0') {
            if (s[*i] == '\\' && s[*i+1] == '"') {
                YY_reader.token[tok_idx++] = '"';
                (*i) += 2;
            } else {
                YY_reader.token[tok_idx++] = s[*i];
                (*i)++;
            }
        }
        
        if (s[*i] == '"') {
             (*i)++;
        } else {
            // eprint가 없으면 printf로 대체
            printf("[Error] 어휘 오류: 따옴표가 닫히지 않았습니다.\n");
        }
        YY_reader.token[tok_idx] = '\0';
        return YY_reader.token;
    }

    /* 일반 아톰 */
    while (s[*i] != '\0' && !is_delimiter(s[*i])) {
        YY_reader.token[tok_idx++] = s[*i];
        (*i)++;
    }
    YY_reader.token[tok_idx] = '\0';

    return YY_reader.token;
}

char* get_current_token(void) {
    return YY_reader.token;
}

/* read_atom 등 파싱 로직에서 쓰임 */
Value* read_atom(char* s) {
    // isdigit은 mylib.h에 있다고 가정 (없으면 여기서 구현 필요)
    if (isdigit(s[0]) || (s[0] == '-' && isdigit(s[1]))) {
        return mkint(atoi(s));
    }
    if (strcmp(s, "공") == 0) return NIL;
    if (s[0] == '"') return mkstr(s + 1); 
    return mksym(s);
}

void match(char* target) {
    if (strcmp(YY_reader.token, target) != 0) {
        printf("[Error] Expected '%s' but got '%s'\n", target, YY_reader.token);
        raise_syntax_error();
    }
}

Value* read_expr(void) {
    char* la = get_current_token();
    if (la[0] == '\0') return NIL;

    if (strcmp(la, "(") == 0) return read_list();
    if (strcmp(la, ")") == 0) {
        raise_syntax_error();
        return NIL;
    }
    
    // Quote (')
    if (strcmp(la, "'") == 0) {
        Value* qlst = cons(mksym("인용"), cons(NIL, NIL));
        next_token();
        Value* data = read_expr();
        // qdta = cdr(qlst) -> setcar(qdta, data)
        setcar(cdr(qlst), data); 
        return qlst;
    }
    // Quasiquote (`)
    if (strcmp(la, "`") == 0) {
        Value* qlst = cons(mksym("특이인용"), cons(NIL, NIL));
        next_token();
        setcar(cdr(qlst), read_expr());
        return qlst;
    }
    // Unquote splicing (,@)
    if (strcmp(la, ",@") == 0) {
        Value* qlst = cons(mksym("비인용연결"), cons(NIL, NIL));
        next_token();
        setcar(cdr(qlst), read_expr());
        return qlst;
    }
    // Unquote (,)
    if (strcmp(la, ",") == 0) {
        Value* qlst = cons(mksym("비인용"), cons(NIL, NIL));
        next_token();
        setcar(cdr(qlst), read_expr());
        return qlst;
    }
    
    Value* val = read_atom(la);
    next_token();
    return val;
}

Value* read_list(void) {
    YY_reader.depth++;
    next_token(); // consume '('
    
    Value* result = NIL;
    if (strcmp(get_current_token(), ")") == 0) {
        YY_reader.depth--;
        match(")");
        next_token();
        return result;
    }

    Value* head = cons(read_expr(), NIL);
    Value* curr = head;

    while (strcmp(get_current_token(), ")") != 0 && strcmp(get_current_token(), ".") != 0) {
        if (get_current_token()[0] == '\0') { 
             reader_fill_buffer();
             next_token();
             continue;
        }
        Value* new_node = cons(read_expr(), NIL);
        setcdr(curr, new_node);
        curr = new_node;
    }

    if (strcmp(get_current_token(), ".") == 0) {
        match(".");
        next_token();
        setcdr(curr, read_expr());
    }

    YY_reader.depth--;
    match(")");
    next_token(); // consume ')'
    return head;
}
