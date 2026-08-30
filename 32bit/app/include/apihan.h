#ifndef _APIHAN_H
#define _APIHAN_H

typedef void (*apihan_output_fn)(const char *str, void *aux);

/* 한글 오토마타 상태 구조체 */
struct HANGUL_STATE {
    int state;      // 0:초기, 1:초성, 2:중성, 3:종성, 4:겹받침
    int cho;        // 초성 인덱스
    int jung;       // 중성 인덱스
    int jong;       // 종성 인덱스
    int view_width; // 현재 화면에 그려진 조합 문자의 너비 (단위: 백스페이스 횟수. 한글=2, 영문=1)

    apihan_output_fn output_func;
    void *aux_data;
};

/* 함수 선언 */
void apihan_init(struct HANGUL_STATE *h, apihan_output_fn func, void *aux);
void apihan_run(struct HANGUL_STATE *h, int key, char *buf, int *pos);
void apihan_backspace(struct HANGUL_STATE *h, char *buf, int *pos);

#endif
