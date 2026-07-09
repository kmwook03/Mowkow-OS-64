#include "data.h"
#include "parse.h"
#include "eval.h"
#include "error.h"
#include <stdio.h>
#include <string.h>
#include "../include/apilib.h" 

#define CMDLINE_MAX 128
#define FILENAME_MAX 12

void init_all() {
    init_mowkow(); // 메모리 풀 초기화
    init_reader(); // 입력 버퍼 초기화
    init_eval();   // 환경 및 내장함수 초기화
}

void load_file(Value* env, char* path) {
    // 1. 파일 열기
    // printf("디버그: [1] '%s' 파일 여는 중...\n", path);
    int fh = api_fopen(path, 0);
    if (fh == 0) {
        printf("[Error] 파일 '%s'을(를) 열 수 없습니다.\n", path);
        return;
    }

    // printf("파일 열기 성공!, 파일 크기 확인 중...\n");
    // 2. 파일 크기 확인 및 버퍼에 읽기
    int filesize = api_fsize(fh, 0);
    // printf("디버그: [2] 파일 크기: '%d' bytes\n", filesize);
    if (filesize >= BUFFER_SIZE - 1) { // NULL 문자를 고려하여 버퍼와 크기 비교
        printf("[Error] 파일 '%s'이(가) 너무 큽니다.\n", path);
        api_fclose(fh);
        return;
    }
    api_fclose(fh);

    // printf("파일 다시 여는 중 (Rewind 효과)...\n");
    fh = api_fopen(path, 0);

    if (fh == 0) {
        printf("[Error] 파일 재오픈 실패.\n");
        return;
    }

    // printf("파일 읽는 중...\n");
    // 파일 내용을 파서 버퍼로 읽음
    api_fread(YY_reader.buf, filesize, fh);
    YY_reader.buf[filesize] = '\0'; // NULL 문자 종료
    api_fclose(fh);

    // 파서 상태 초기화
    YY_reader.pos = 0;
    YY_reader.depth = 0;

    // 3. 첫 토큰 읽기
    next_token();
    // printf("디버그: [4] 첫 토큰: [%s]\n", get_current_token());

    int loop_cnt = 0;

    // 4. 파일 읽고 실행하기
    while (1) {
        loop_cnt++;
        // printf("디버그: [루프 %d] 시작. 현재 토큰: [%s]\n", loop_cnt, get_current_token());
        // 에러 상태면 중단
        if (g_has_error) {
            printf("오류 발생으로 로딩을 중단합니다.\n");
            reset_error();
            break;
        }

        // EOF 확인 (토큰이 없으면 종료)
        // parse.c 로직상 depth가 0일 때 버퍼 끝에 도달하면 "" 토큰을 반환하고 종료함
        if (get_current_token()[0] == '\0') {
            // printf("디버그: EOF. 루프 탈출\n");
            break;
        }

        // 수식 읽기
        // printf("디버그: 수식 읽는 중...\n");
        Value* expr = read_expr();
        
        // 파싱 중 에러 체크
        if (g_has_error) {
            printf("오류 발생으로 수식 읽기를 중단합니다.\n");
            break;
        }

        // 실행 (Eval)
        if (expr != NIL) {
            // printf("파일 실행 중...\n");
            Value* res = mk_eval(expr, env);
            
            // 실행 중 에러 체크
            if (g_has_error) break;

            // 결과 출력 (파이썬 코드와 동일하게 NIL이 아니면 출력)
            if (!is_nil(res)) {
                print_value(res);
                printf("\n");
            }
        }
    }

    // 6. REPL 복귀를 위한 버퍼 초기화
    YY_reader.buf[0] = '\0';
    YY_reader.pos = 0;
    // printf("파일 종료!\n");
}

void run_repl() {
    printf("머꼬 인터프리터\n");
    while (1) {
        reset_error();
        
        // 프롬프트 출력 및 입력 대기
        if (get_current_token()[0] == '\0') {
            reader_fill_buffer();
            next_token();
        }
        // 입력 없으면 루프 탈출
        if (get_current_token()[0] == '\0') break; // Empty line

        Value* expr = read_expr();

        if (g_has_error) {
            YY_reader.pos = 0; 
            YY_reader.buf[0] = '\0'; 
            YY_reader.token[0] = '\0';
            continue;
        }

        if (expr != NIL) {
            Value* res = mk_eval(expr, global_env);
            if (!g_has_error && res) {
                print_value(res);
                printf("\n");
            }
        }
    }
}

void HariMain(void) {
    char cmdline[CMDLINE_MAX];
    char filename[FILENAME_MAX];
    char *p;
    int i = 0;

    // 시스템 초기화
    init_all();

    // 기본 라이브러리 로드
    int lib_fh = api_fopen("library.scm", 0);
    if (lib_fh != 0) { // 파일 존재 확인
        api_fclose(lib_fh);
        load_file(global_env, "library.scm");
    }

    // 인자 파싱 (예: 머꼬 gcd.mk)
    api_cmdline(cmdline, CMDLINE_MAX);

    p = cmdline;

    while ((unsigned char)*p > ' ') p++;    // 명령어 건너뛰고 공백까지 이동
    while (*p == ' ') p++;                  // 공백 건너뛰기

    if (*p != '\0') {
        while ((unsigned char)*p  > ' ' && i < FILENAME_MAX - 1) {
            filename[i++] = *p++;
        }
        filename[i] = '\0'; // 문자열 종료

        // printf("파일 '%s'을(를) 로드합니다.\n", filename);
        load_file(global_env, filename);
    } else {
        run_repl();
    }

    api_end();
}
