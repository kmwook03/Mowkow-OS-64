# 애플리케이션 소스 코드 디렉토리

```
├── 📂app               # 애플리케이션 소스 코드
│   ├── 📄common.mk         # 애플리케이션 공통 빌드 규칙
│   ├── 📁api               # API(.nas) 소스 코드
│   ├── 📁include           # 애플리케이션 개발에 사용되는 라이브러리 및 api 헤더파일
│   ├── 📁timer             # 타이머
│   ├── 📁계산              # 계산기(CLI)
│   ├── 📁나노              # GNU Nano
│   ├── 📁머꼬              # 머꼬 인터프리터
│   └── 📁사진              # 이미지 뷰어(JPEG)
```

애플리케이션을 구현하는 소스 코드가 저장된 디렉토리입니다.

## 애플리케이션 추가

각 애플리케이션은 별도의 폴더에 개별 Makefile과 소스 코드를 함께 보관해야합니다.
```
# 예시
.
.
├── 📂계산                 # 애플리케이션 이름
│   │   ├── 📄Makefile       # 빌드 규칙
│   │   └── 📄calc.c         # 계산기 애플리케이션 소스 코드 
.   . 
.   .
```

개별 Makefile에는 다음과 같은 내용이 작성되어야 합니다.
```Makefile
APP = [앱 이름]        # 폴더 이름과 동일하게 작성할 것
STACK = [할당할 스택 크기]     # 선택적
MALLOC = [할당할 힙 크기]      # 선택적

include ../common.mk
```

## api 목록

```C
// 1. 콘솔에 문자 하나 출력
void api_putchar(int c);
// 2. 콘솔에 문자열 출력
void api_putstr(char *s);
// 3. 콘솔에 문자열 출력 (길이 지정)
void api_putstr_len(char *s, int l);
// 4. 애플리케이션 종료
void api_end(void);
// 5. 창 열기
int api_openwin(char *buf, int xsiz, int ysiz, int col_inv, char *title);
// 6. 창에 문자열 출력
void api_putstrwin(int win, int x, int y, int col, int len, char *str);
// 7. 윈도우 칠하기
void api_boxfilwin(int win, int x0, int y0, int x1, int y1, int col);
// 8. malloc 초기화
void api_initmalloc(void);
// 9. 동적 메모리 할당
char *api_malloc(int size);
// 10. 동적 메모리 해제
void api_free(char *addr, int size);
// 11. 창에 점 찍기
void api_point(int win, int x, int y, int col);
// 12. 창 새로고침
void api_refreshwin(int win, int x0, int y0, int x1, int y1);
// 13. 창에 선 긋기
void api_linewin(int win, int x0, int y0, int x1, int y1, int col);
// 14. 창 닫기
void api_closewin(int win);
// 15. 키보드 입력 받기
int api_getkey(int mode);
// 16. 타이머 할당
int api_alloctimer(void);
// 17. 타이머 초기화
void api_inittimer(int timer, int data);
// 18. 타이머 설정
void api_settimer(int timer, int time);
// 19. 할당된 타이머 해제
void api_freetimer(int timer);
// 20. 소리 재생
void api_beep(int tone);
// 21. 파일 열기                                                    
int api_fopen(char *fname, int flag);
// 22. 파일 닫기
void api_fclose(int fhandle);
// 23. 파일 찾기
void api_fseek(int fhandle, int offset, int mode);
// 24. 파일 크기 받기
int api_fsize(int fhandle, int mode);
// 25. 파일 읽기
int api_fread(char *buf, int maxsize, int fhandle);
// 26. 명령줄 받기
int api_cmdline(char *buf, int maxsize);
// 27. 언어 모드 받기
int api_getlang(void);                                                      
// 28. 파일 쓰기
void api_fwrite(char *buf, int maxsize, int fhandle);
```