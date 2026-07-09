## src/kernel

```
│   ├── 📂kernel            # 커널 구성 코드
│   │   ├── 📄bootpack.c        # 커널
│   │   ├── 📄console.c         # 콘솔
│   │   ├── 📄dsctbl.c          # GDT/IDT
│   │   ├── 📄fd.c              # 파일 디스크립터
│   │   ├── 📄memory.c          # 메모리 관리자
│   │   ├── 📄mtask.c           # 멀티태스킹
│   │   ├── 📄naskfunc.nas      # 어셈블리 함수
│   │   ├── 📄sheet.c           # 시트
│   │   └── 📄window.c          # 창 관리자
```

커널의 주요 기능들을 구현하는 소스 코드입니다.

- `bootpack.c` : 시스템 초기화 및 입출력 처리
- `console.c` : 콘솔 태스크 관리
- `dsctbl.c` : GDT/IDT 초기화 및 관리
- `fd.c` : 파일 기술자
- `memory.c` : 메모리 관리자
- `mtask.c` : 멀티 태스킹 지원
- `naskfunc.nas` : 어셈블리로 구현된 함수 (하드웨어 직접 제어에 사용됨)
- `sheet.c` : 창 겹치기 처리를 위한 시트
- `window.c` : 창 관리자
