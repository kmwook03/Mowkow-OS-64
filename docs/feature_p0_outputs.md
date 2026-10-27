# P0 사용자 출력 경로 목록

2026-10-27 8차: [출처 저장 축소 기록](feature_p0_mp_compact.md)에서 typed 인수의 보존과
손실을 분리했다. 이탈한 비interned 문자열은 미해결로 표시한다. 활성 154개 문구/149개
pending ID와 translation=pending은 유지하며 메모리 축소를 번역 완료로 세지 않는다.

2026-10-27 7차: [함수 identity 보존 경계](feature_p0_mp_boundary.md)로 errno·filename·
recursion과 사용자 생성 구분을 추가 검증했다. 기존 활성 문구의 149개 pending ID와
translation=pending은 유지한다. 새 생성 경계의 성공을 전체 출력 번역 완료로 세지 않는다.

2026-10-27 확장: [무할당 수명 시제품](feature_p0_mp_lifetime.md)의 실제 format 표를
위 catalog와 대조하여 `build64/feature-p0/mp-lifetime-coverage.json`에 시제품 5개와
149개 pending ID를 기록한다. 생성자·비상 객체 identity 실패는 별도이며 이 문구 수를
번역 완료로 해석하지 않는다. 저장된 catalog의 `translation=pending`은 유지한다.

2026-10-26 확장: MicroPython은 [개별 활성 오류 catalog](feature_p0_mp_catalog.json)에
`MP_ERROR_TEXT` 154개와 양 아키텍처 위치·분류·정책·pending 상태를 기록했다.
[감사 결과](feature_p0_micropython_audit.md)에 비매크로 생성 경계와 실제 OOM/포화
결과를 남겼다. 전처리상 활성 목록이며 실행 coverage나 전체 출력 번역 완료가 아니다.
커널·CRT·앱·nano·머꼬와 동적/비매크로 출력은 아래 1차 경로 목록부터 계속 확장한다.

2026-10-25 조사. 항목의 번역 정책은 **P5 목표**이며 현재 번역 적용 완료 표가 아니다.
현재 목록은 출력 경계·문구군의 1차 inventory다. P5 catalog를 만들기 전 활성
vendor 문구를 개별 항목으로 펼치고 누락 검출을 추가해야 한다. 문자열을 stdout에서
추측해 치환하는 규칙으로 사용하지 않는다.

- 안내: 한국어 고정. 오류: 발생한 콘솔의 입력 모드를 출력 시작 시 고정.
- 데이터: 원본 유지. 진단: 기존 개발/시험 문법 유지.
- 파일명·숫자·명령 이름·format 인수는 안내/오류 안에서도 원본 유지.
- GUI 전 부팅 진단은 영문 유지. 이미 그려진 문장은 모드 전환 시 다시 번역하지 않음.

| 항목 | 호출 경로/소유 위치 | 분류 | 번역 정책 |
| --- | --- | --- | --- |
| 아키텍처별 `머꼬 OS … 콘솔`, Shift+Space 안내 | kernel/console64.c → console64_puts | 안내 | 한국어 유지 |
| `콘솔을 닫습니다.` | console task 종료 | 안내 | 한국어 유지 |
| `줄이 너무 깁니다` | console_editor64.c → console_puts_con64 | 오류 | 한/영 쌍 필요 |
| `commands: …` 양 플랫폼 목록 | console_command64.c/help | 안내 | 설명만 한국어, 명령 토큰 유지 |
| `fullscreen is console 0 only` | console command/xwindow | 오류 | 한/영 쌍 필요 |
| `no free console slot` | console command/new | 오류 | 한/영 쌍 필요 |
| `no files` | console command/ls | 안내 | 한국어 필요 |
| `file not found`, `파일 없음` | console command/type, py 파일 존재 검사 | 오류 | 공통 ID·인수로 통합 후 한/영 |
| `알 수 없는 명령어` | console command 최종 fallback | 오류 | 한/영 쌍 필요 |
| `another program is running` | run_program64/process64_exec_file 오류 | 오류 | 한/영 쌍 필요 |
| `program name/command line too long or too many arguments` | run_program64 | 오류 | 한/영 쌍 필요 |
| `invalid program name, command line, or user stack` | run_program64 | 오류 | 한/영 쌍 필요 |
| `exit N`와 뒤따르는 프롬프트 | run_program64; QEMU regression 판정 | 진단/프로토콜 | 그대로 보존. 사용자 표시를 나누면 생산자·도구 동시 변경 |
| OS `> `, 줄바꿈·공백·0x·KiB | console_render64/command format | 데이터/프로토콜 | 토큰 유지 |
| `ticks`, `free`, `alloc4k`, `switches`, `current-level`, `task`, `lv` | console command ticks/mem/tasks | 개발 진단 | 명령 판정과 함께 검토; 현 프로토콜 보존 |
| ls의 실제 파일명·size | print_file_name64/print_uint64 | 데이터 | 번역 금지 |
| type/read, 사용자 SYS_WRITE 바이트 | console64_write_con → renderer | 데이터 | 번역 금지 |
| 창 제목·작업표시줄 문구 | gui64.c/window64.c → surface drawing | 안내/이름 | 고정 UI만 한국어, 사용자 제목 보존 |
| PREEDIT/키 에코·선택/복사 | console editor/queue/render | 데이터 | 번역 금지 |
| CRT puts의 문자열·stdout newline | app64/crt/string.c, syscall.c | 데이터 | 전달만 수행; CRT stdout 번역 금지 |
| CRT 실패 signed 반환값 | app64/crt syscall/allocator | 오류 코드 | 호출자가 메시지 결정, ABI 유지 |
| `hello from app64` | app64/hello/hello.c | 안내 | 한국어 필요 |
| cat의 파일 내용 | app64/cat/cat.c → write | 데이터 | 번역 금지 |
| cat `open failed` | app64/cat/cat.c → puts | 오류 | P2 모드 API로 한/영 |
| ktest READY/PASS/FAIL/event/geometry/usage | app64/ktest | 시험 프로토콜 | 그대로 보존 |
| mtest PASS/FAIL, `ok`, 단계·수치 | app64/mtest | 시험 프로토콜 | 그대로 보존 |
| wtest PASS/FAIL, 단계·저장/검증 파일 내용 | app64/wtest | 시험 프로토콜/시험 데이터 | 그대로 보존 |
| 자체 나노 `나노 1.0`, `수정됨`, 줄/칸, 하단 단축키 | app64/나노/나노.c/draw_* | 안내 | 현재 한국어. P4 교체 대상, 대규모 선번역 안 함 |
| 자체 나노 `새 파일`, `줄을 읽었습니다`, `저장했습니다` | load_file/save_file/status | 안내 | 현재 한국어, P4 대응 필요 |
| 자체 나노 저장 확인 질문 | prompt_yesno | 안내 | 현재 한국어, 키/선택은 데이터 |
| 자체 나노 크기/읽기/메모리/geometry/상태 잘림 오류 | main/set_status | 오류 | P4 교체 전에 회귀 기대값 목록으로 보존 |
| 자체 나노 이름 없음/열기/쓰기/큰 파일 오류 | save_file/set_status | 오류 | P4에서 한/영 쌍, 실패 시 dirty 유지 |
| GNU nano 제목·dirty·buffer 이름 | nano src/winio.c/titlebar, minibars | 안내+데이터 | 고정 상태 한국어, 실제 파일명 유지 |
| GNU nano 하단 shortcut/기능명 | global.c/shortcut_init → winio.c/bottombars | 안내 | 한국어 고정. Ctrl/Meta 키 토큰 유지 |
| GNU nano 읽은/쓴 행수 등 statusbar | files.c → statusbar | 안내 | 한국어 고정, 수량 인수 유지 |
| GNU nano 찾기/바꾸기/저장 파일명 질문 | search.c/files.c → prompt.c/do_prompt | 안내+데이터 | 질문은 한국어, answer 원문 유지 |
| GNU nano 못 찾음·정규식·잘못된 위치 | search.c → statusline(ALERT/AHEM) | 오류 | 메시지 ID/호출 문맥으로 한/영 선택 |
| GNU nano 파일/권한/저장/변경 감지 오류 | files.c → statusline, strerror | 오류 | 입력 모드 캡처. strerror까지 같은 언어; 경로 유지 |
| GNU nano 메모리 실패·치명적 종료 | utils.c/nmalloc/nrealloc → nano.c/die | 오류 | 할당 없는 한/영 고정 경로 필요 |
| GNU nano 옵션 도움말·version | nano.c/usage/version/print_opt | 안내 | 설명은 한국어, 옵션/저작권/버전/URL 보존 |
| GNU nano 긴 도움말 | help.c/do_help | 비활성 안내 | multibuffer 제외로 현재 후보에서 꺼짐; 활성화 시 catalog 포함 |
| GNU nano nanorc/color/browser/speller/linter/formatter/mouse | 각각 제외된 source feature | 비활성 | 현재 후보 범위 제외. 활성화하면 inventory 추가 |
| GNU nano 원본 텍스트·파일명·search/replace answer·cutbuffer | chars.c/text.c/cut.c/prompt.c | 데이터 | 번역 금지 |
| GNU nano terminal escape/키 시퀀스 | nano.c/terminal_init/finish, winio.c | 프로토콜 | 번역 금지. OS TTY 직접 계층에서 제거/우회 지점 필요 |
| `py already running in another console` | mpport_main.c/mpport_claim64 | 오류 | 호출 콘솔 한/영, 기존 VM 소유자 상태 보존 |
| invalid stack bounds/cannot allocate GC heap/init failed | mpport_main.c/init/session | 오류 | VM 미초기화에도 쓸 수 있는 고정 한/영 경로 |
| cannot print exception | mpport_main.c/mpport_report64 | 오류 | 재귀 번역/할당 없는 한/영 고정 경로 |
| cannot free GC heap; retained for retry | mpport_main.c/finish | 오류+소유권 진단 | 사용자 고정 문구와 개발 진단을 구분; 소유권 의미 유지 |
| fatal uncaught exception, halting | mpport_main.c/nlr_jump_fail | 치명적 진단 | 기존 panic 진단 유지; GUI 이전은 영문 |
| MicroPython 배너/보드·버전, help 안내 | shared/runtime/pyexec.c/friendly_repl | 안내 | 고정 안내 한국어, 버전/보드 식별자 유지 |
| MicroPython `>>> `, `... ` | readline/repl | 프로토콜 | 그대로 보존 |
| raw REPL banner, OK, EOF/CTRL-A/B/D 바이트 | pyexec.c/raw_repl | 비활성 프로토콜 | 연결되면 원격 도구 문법 보존. 현재 OS에서 raw dispatcher 미호출 |
| Traceback…, File…, line…, in… | py/obj.c/mp_obj_print_exception | 오류 고정 문구 | 구조화된 traceback 배열에서 한/영 출력 |
| 예외 타입 이름 | objexcept.c/mp_obj_exception_print | 진단 식별자 | NameError 등 원문 이름 유지 |
| NameError의 고정 문구와 식별자 | runtime.c → new_exception_msg_vlist | 내장 오류+데이터 | 고정 문구만 번역, `%q` 인수 보존; 시제품 검증 |
| TypeError의 고정 문구와 타입명 | runtime/obj*/argcheck → new_exception_msg* | 내장 오류+데이터 | format 단계에서 구분; 시제품은 callable 사례 검증 |
| Syntax/Indentation/f-string 오류 | parse.c/lexer.c/compile.c | 내장 오류 | 시제품 invalid syntax 검증; 나머지 개별 목록화 필요 |
| OSError errno·경로/내장 설명 | runtime.c/mp_raise_OSError[_with_filename] | 내장 오류+데이터 | 실제 생성 경계별 추적, errno·경로 보존; 시제품 EIO 검증 |
| MemoryError 크기/heap locked 설명 | runtime.c/m_malloc_fail, malloc.c/gc.c | 내장 오류 | 시제품 큰 요청 실패 검증; 실제 힙 고갈은 미검증 |
| 기타 MP_ERROR_TEXT 오류 | 활성 py 코어 및 shared runtime | 내장 오류 | 전체 catalog/coverage 필요. 시제품 일부 성공으로 전체 번역을 완료 판정하지 않음 |
| Python print, str(exc), repr, 사용자 exception.args | VM → mp_print/hal stdout | 데이터 | 번역 금지. 표시용 surrogate를 원본에 대입하지 않음 |
| mowio.print/eprint/readfile/사용자 파일 내용 | modmowio.c, mpport_file64.c, _compat.py | 데이터 | 번역 금지. syscall/파일 로딩 오류만 오류 경계에서 처리 |
| 머꼬 기본 `>  `, 연속 `.. ` | _parse.py/Reader | 프로토콜 | P1에서 기본만 `>> `, 연속은 유지 |
| 머꼬 종료 감사 문구 | mowkow.py/KeyboardInterrupt/EOFError | 안내 | 한국어 유지 |
| 머꼬 `오류:`, 파일:행:열 구문 오류 | mowkow.py, _parse.py | 오류+데이터 | 입력 모드별 고정 문구, 사용자 토큰·위치 유지 |
| ErrSyntax/ErrUnbound/ErrArgs/ErrType의 __str__ | _error.py, _eval.py, _data.py | 언어 자체 오류 | 명시적 메시지 ID·인수로 한/영 출력; Python 사용자 예외 일괄 번역과 구분 |
| 머꼬 평가 결과/문자열/print/readline 인수 | mowkow.py, _eval.py/_compat.py | 데이터 | 번역 금지 |
| smoke.py/float_smoke.py 예제·검증 결과 | py64 payloads | 시험/예제 데이터 | 그대로 보존 |
| COM1 boot, poll failure ring, boot self-check, panic | arch/*/boot_diag/smoke/platform, drivers, kernel boot | 개발 진단 | 기존 문자열/레지스터/errno/프로토콜 보존 |

확인할 때에는 `console_puts_con64`/`console64_puts`뿐 아니라 직접 `write`,
`mp_printf`/`mp_print_str`, nano의 `statusbar/statusline/do_prompt/die`와
머꼬의 `__str__`, `raise`, `eprint`를 함께 추적한다. 공통 renderer·CRT·HAL의
출력 바이트를 번역 경계로 사용하면 사용자 데이터도 바뀌므로 금지한다.
