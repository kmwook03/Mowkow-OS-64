# P0 이식 조사와 호스트 시제품

2026-10-26 후속: [nano 파일 스트림·UTF 기반 계층](feature_p0_nano_compat.md)을
추가하고 clean64/check64(호스트 98개·기존 양 OS·새 계층 양 아키텍처 컴파일)를
통과했다. 이 기반 archive에는 업스트림 nano 코어가 아직 연결되지 않았다.
아래 2026-10-25의 호스트 시제품 결과와 구분한다.

2026-10-25, 기준 커밋 `b1ecb53` 위의 미커밋 실험.
**P0 진행 중이다. 호스트 실험은 통과했지만 freestanding nano의 양 아키텍처
링크와 OS TTY 입력·저장·종료는 아직 검증하지 않았다.** 기존 앱과 커널의 동작,
머꼬 reference, MicroPython vendor 소스는 변경하지 않았다.

## 재현

필요한 환경은 Linux PTY, Python 3.12 이상, 호스트 C 컴파일러·make·binutils,
호스트 wide curses **헤더**, 기존 x86_64 교차 빌드 도구다. 현재 화면 시제품은
`NCURSES_OPAQUE=1` 헤더에 맞춘 독립 구현이다. ncurses 라이브러리를 링크하거나
ncurses의 WINDOW 메모리를 이용하지 않는다. libc·locale·POSIX·gnulib는 호스트
구현을 이용한다. 따라서 호스트 성공을 OS 이식 완료로 해석하면 안 된다.

```sh
curl -L --fail https://www.nano-editor.org/dist/v9/nano-9.3.tar.xz -o /tmp/nano-9.3.tar.xz
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_probe64.py --archive /tmp/nano-9.3.tar.xz
```

다운로드는 명시적으로 실행하며 도구는 네트워크에 접근하지 않는다. 압축 해제 전
SHA-256을 검사한다. `--only nano`, `--only micropython`으로 분리할 수도 있다.
로그·소스·실행 파일·저장 파일은 무시되는 `build64/feature-p0/` 아래에 둔다.
`report.json`, `nano-symbols.json`, `nano-curses-trace.log`, `mp-run.log`를 확인한다.
각 실행의 `report.json`에는 그 실행에서 선택한 실험만 들어간다.

이번 검증에서 `make check64`(style, 호스트 96개, 기존 양 아키텍처 빌드)와
두 호스트 시제품이 통과했다. 새 화면 모델/공용 UTF 구현을 ASan·UBSan으로
계측한 같은 편집 시나리오도 통과했다(`build64/feature-p0/sanitize/`). 이 추가
검사에서 원본 nano/gnulib 오브젝트 전체를 sanitizer로 재빌드한 것은 아니다.
실제 OS TTY, QEMU IDE/AHCI, Pi 5는 실행하지 않았다.

## nano 고정 후보와 빌드 범위

- 버전: GNU nano **9.3**, 공식 2026-10-21 배포.
- 원본: <https://www.nano-editor.org/dist/v9/nano-9.3.tar.xz>
- SHA-256: `a9affc52c9596ee0682bcdd4ceb3c0b235eb221d83d733dfd9b0dff45897cb0d`
- 이 값은 내려받은 원본의 무결성 고정값이다. 배포 서명의 검증 결과는 아니다.
- 소스 패치: 없음. 시제품은 별도 C 파일과 링크 단계만 추가한다.
- 원본의 `COPYING`(GPLv3+), `COPYING.DOC`, `AUTHORS`, `ChangeLog`, `NEWS`와
  파일별 저작권을 압축 해제한 트리 전체에 보존한다. P4 vendor 반입 때도 포함한다.
- 설정의 실행 가능한 원본은 `feature_probe64.py`의 `NANO_OPTIONS`다.
  `--enable-tiny`를 사용하지 않는다. UTF-8, undo/redo, 검색·바꾸기, 기본 단축키와
  화면 크기 변경은 남긴다. 제외 기능은 각 `--disable-*` 옵션으로 명시한다.
- **`--disable-multibuffer`는 내장 Ctrl-G 긴 도움말까지 자동으로 끈다.**
  `configure.ac:192`에 있는 의존성이다. 하단 단축키 안내는 남는다. P4에서 긴
  도움말이 필요하면 단일 버퍼 도움말 패치를 분리해야 한다.
- 외부 명령은 이 설정에서도 `execute_command()`와 fork/pipe/waitpid가 남는다.
  configure 옵션만으로 제거됐다고 판단하지 않는다. 단순 restricted 모드는
  저장 파일명 변경까지 제한하므로 일반 편집기의 대체 설정으로 채택하지 않았다.
- 이번 설정은 NLS를 끈 **영문 기준선**이다. 한국어 catalog·오류/안내 분리는
  P5의 별도 구현이며 이 실험은 번역된 nano를 제공하지 않는다.

## 실제 편집 루프에서 확인한 것

`nano_curses_probe64.c`를 원본 nano의 모든 `src/*.o`, bundled `libgnu.a`에
링크한다. 원본 `main()` → `process_a_keystroke()` → 편집·프롬프트·저장 경로를
실행한다. 화면 모델 외의 파일 I/O와 프로세스 기능은 호스트의 실제 구현이다.
지원하지 않는 terminfo, 키 정의, 지연 요청은 오류/없음으로 반환한다.

입력은 기존 TTY의 CHAR/PREEDIT/KEY/RESIZE에 대응하는 스크립트다. 별도의
fixture CHECK 이벤트는 실제 nano 버퍼와 커서 위치를 독립 기대값과 비교한다.

| 실행 경로 | 독립 기대값/관찰 | 결과 |
| --- | --- | --- |
| 빈 파일 열기, PREEDIT `ㄱ` → `한` → 취소 | 편집 내용·문자 수·undo 헤드 유지 | PASS |
| CHAR `한`, PREEDIT `글`, CHAR `글` | 원본 버퍼 `한글`, 두 칸 셀과 continuation | PASS |
| Alt-U / Alt-E | 중간 버퍼 `""` / `"한글"`을 각각 직접 비교 | PASS |
| Ctrl-W `한` Enter | 검색 결과의 `current_x=0` | PASS |
| Ctrl-\\ `한` Enter `가` Enter y | 원본 버퍼 `가글` | PASS |
| 80×25 → 60×20 RESIZE | 원본 `regenerate_screen()`, 창 재생성 관찰 | PASS |
| Ctrl-O Enter Ctrl-X | 실제 저장 파일이 정확히 UTF-8 `가글\n`, 종료 0 | PASS |
| curses 창·이동·속성·flush·커서 호출 | 원본 호출에서 증가한 관찰값, reverse 셀 포함 | PASS |

PREEDIT는 화면 모델의 임시 두 셀에만 합성하고 복구한다. 이 결과는 **호스트
모델에서의 버퍼/undo 분리**를 증명한다. OS의 실제 sheet 픽셀 복구, 한글 자동자,
포커스, 렌더러 참조, 저장 오류, 파일명·검색 프롬프트 조합은 아직 증명하지 않았다.
`wcwidth`/`wctomb`/분류 함수도 호스트 locale 구현이며 타깃 구현이 필요하다.
현재 nano 코어에는 기존 자체 편집기의 64KiB 제한이 없다. 이 실험을 앱으로
교체하면 파일 크기 정책을 잃으므로 P4 전에는 교체하지 않는다.

## 의존성 목록과 필요한 TTY 계약

동일 빌드의 모든 nano 오브젝트를 `ld -r`로 합쳐 코어 내부 심볼을 해결한 다음
`nm -u`를 실행한다. 전체 실제 목록은 [nano 심볼 목록](feature_p0_nano_symbols.json)에
보존했다. 이는 **이 호스트 설정**의 목록이며 cross-build의 심볼 목록을 대체하지
않는다. 특히 `__*_chk`, `__isoc23_*`, `rpl_*`는 glibc/gnulib 설정에서 생긴다.

| 영역 | 관찰한 주요 심볼/호출 | 타깃 처리 방향 |
| --- | --- | --- |
| CRT 공통 | malloc/free, realloc, memmove, strcasecmp, strcasestr, strpbrk 등 | nano 전용 구현부터 시작; 실제 공용 필요분만 CRT로 승격 |
| 서식·FILE | fdopen/fopen, fread/fwrite, fflush/ferror/fclose, printf/vsnprintf | signed syscall 오류와 partial I/O를 보존하는 전용 FILE 계층 |
| 파일 메타데이터 | stat, access, realpath, fchmod/fchown/futimens, fsync | syscall이 제공하지 않는 속성·내구성·원자성을 성공으로 가장하지 않음 |
| 파일 생성/복구 | mkstemp/mkstemps, unlink, 백업·락 파일 | 지원 기능을 끄거나 명시적 오류. 저장 경로의 사전 검사와 정책 분리 필요 |
| 프로세스·signal | fork/pipe/dup2/execl/waitpid/kill, sigaction/sigprocmask | 외부 명령·suspend 경로를 비활성화; 지원하지 않는 연산은 오류 |
| 사용자/환경 | getpwuid/getpwent, geteuid/getpid/gethostname, getenv/putenv | 루트 파일 모델과 불필요한 조회의 제거 지점을 명시 |
| 정규식 | rpl_regcomp/regexec/regerror/regfree | 일반 검색과 별개; regex 비활성화 패치 또는 gnulib regex 이식 비교 필요 |
| UTF/locale | wcwidth/wctomb, iswalnum/iswblank/iswpunct/towlower, setlocale | bounded UTF 변환, 한글 두 칸, 제어/결합/손상 문자 정책 필요 |
| curses | newwin/delwin, wmove/waddnstr/waddch, wattr_on/off, wclrtoeol | 창별 셀·색·폭·clip을 전용 계층이 소유 |
| curses 제출 | wnoutrefresh/wrefresh/doupdate, wscrl/wredrawln | dirty 셀을 TTY move/attr/write/flush에 연결 |
| curses 입력 | wgetch/nodelay/ungetch/keypad, halfdelay/napms | UTF-8 바이트 큐, raw 키 매핑, timeout 정책; 무한 drain 금지 |
| terminfo | tigetstr/tgetstr/key_defined/define_key | TTY에 escape terminal이 없으므로 capability 부재와 고정 키 매핑 |
| NLS | 이번 빌드는 gettext/ngettext 외부 심볼 없음 | P5 전용 catalog와 안내/오류 호출 문맥 연결 필요 |

P2 전에 확정할 요구:

1. `tty_size()`의 generation 변경을 raw RESIZE와 일관되게 반영한다.
2. `wgetch`는 한 CHAR를 UTF-8 바이트 큐로 바꾼다. 두/세/네 바이트를 원본 nano에
   그대로 전달하고 PREEDIT는 그 큐에 절대 넣지 않는다.
3. curses의 입력 창 이동도 제출해야 한다. 원본 nano는 `wmove(midwin, ...)` 뒤
   `wnoutrefresh()` 없이 `wgetch(midwin)`에 기대는 경로가 있다.
4. `curs_set(0/1)`은 P2의 희망 표시 상태에 연결하고 마지막으로 요청한 커서
   위치를 보존한다. 본문과 조합 문자를 훼손하지 않는 밑줄은 커널이 소유한다.
5. nodelay 입력 drain과 시간 제한 입력을 실제로 지원할지, 전용 계층의 버퍼만
   비차단 조회하고 nano의 시간 의존 동작을 끌지 결정한다. 현 SYS_TTY는 blocking
   readkey만 있으므로 timed wait의 성공 stub을 둘 수 없다.
6. EOF/프로세스 종료/실패 시 cooked 복구는 기존 계약을 지킨다. 상태 영역 차감은
   P3가 보고하는 사용 가능한 크기로 처리한다.

ncursesw는 더 넓은 curses/terminfo/terminal 제어·locale 의존성을 추가한다.
반면 nano의 관찰된 화면 호출은 현재 모델로 실제 편집까지 연결됐다. **제한된
직접 계층을 계속 검증하는 방향**을 우선한다. 아직 타깃 비용 비교를 마치지
않았으므로 ncursesw보다 작다는 측정 결과나 이식 완료를 주장하지 않는다.

## MicroPython 예외 표시 시제품

원본 `build64/upy/` 코어/REPL 오브젝트를 각각 호스트 실행 파일에 직접 링크한다.
OS/HAL 경계만 호스트 I/O로 바꾼다. 머꼬·Nano의 mock VM은 사용하지 않는다.
업스트림 수정 없이 GNU ld `--wrap`으로 다음 두 경계를 검증했다.

- 생성 경계: `mp_obj_new_exception_msg`, `_msg_varg`, `_msg_vlist`, `_args`,
  `mp_raise_OSError`. 원본 format과 원본 varargs가 아직 분리되어 있을 때 표시용
  예외를 만들고 원본 객체·원본 args 정체성에 연결한다.
- 표시 경계: `mp_obj_print_exception`. 언어를 한 번 읽고 traceback의 고정 문구와
  등록된 내장 설명만 표시용 객체로 출력한다. 원본 객체·`args`·`str(exc)`를
  바꾸거나 stdout에서 문자열을 찾아 번역하지 않는다.

같은 문장의 `raise SyntaxError("invalid syntax")`, 사용자 NameError/TypeError,
`raise OSError(5)`는 Python 생성자 경로이므로 등록되지 않는다. 사용자
`raise ValueError("사용자 문자열")`, `print("invalid syntax")`도 유지된다.
원본 예외를 저장해 번역 출력한 **뒤** `str(saved)`, `saved.args is saved_args`,
`type(saved)`를 다시 검사한다. 파일명 `사용자.py`와 식별자는 형식 인수로 유지한다.

| 예외 | 실제 경로/고정 문구 시제품 | 결과 |
| --- | --- | --- |
| NameError | 미정의 식별자 → 원본 `%q` → 한국어 고정 문구 | PASS |
| TypeError | `1()` → 원본 `%s` 타입명 → 한국어 고정 문구 | PASS |
| SyntaxError | `if :` → 원본 parse 오류 → 한국어 고정 문구 | PASS |
| OSError | 실제 `mp_raise_OSError(EIO)` → errno 보존 | PASS |
| MemoryError | 2MiB 힙에 3MiB 요청, 실제 할당 실패·GC 경로 | PASS |
| 파일 실행 | 실제 `pyexec_file()` + GC 소유 source reader | PASS |
| friendly REPL | 실제 `pyexec_friendly_repl()` 예외 호출 포착 | PASS |
| raw REPL | 실제 `pyexec_raw_repl()` 예외 호출 포착 | PASS |
| 영문 모드 | 원본 printer로 원문 출력 | PASS |
| 사용자 출력/예외 객체/GC | 생성 경계 구분, 재출력·수집 후 원본 보존 | PASS |

OS의 활성 경로는 `mpport_main.c`의 파일 실행과 friendly REPL이다.
Ctrl-A는 `pyexec_mode_kind=RAW`로 바꾸고 friendly 루프를 반환하지만 현재
`mpport_execute64()`는 raw dispatcher를 이어 호출하지 않는다. raw 루프는
링크되어 있으나 현재 OS에서 활성화된 기능이라고 보고하지 않는다.

**판정: 다섯 예외의 생성 출처 구분과 REPL 공통 표시 가로채기는 vendor 수정 없이
가능했다. 전체 내장 예외·수명·메모리 고갈까지 만족하는 생산용 해법의 입증은
아직 끝나지 않았다.** 다음 제약은 P5의 영어 fallback으로 숨기면 안 된다.

- 같은 오브젝트 파일 안의 호출은 `--wrap`을 우회한다. 실제로 `mp_raise_OSError`
  내부의 inline arg1 생성자는 일반 `_args` wrapper를 지나지 않았다. 별도
  상위 경계를 감싸는 이유이며 다른 생산자도 목록화해야 한다.
- 위 이유로 코어를 먼저 `ld -r`로 합치면 생성 경계를 대부분 놓친다. AArch64의
  foundation relocatable 링크를 포함해 최종 링크 전후 순서를 검증해야 한다.
- 레지스트리는 **64개를 GC root로 보존하는 실험용**이다. 초과하면 assert로
  실험을 실패시킨다. 장기 실행의 무한 보존·원본 객체 재사용 문제를 해결하지 않는다.
- 표시용 객체를 만들 메모리가 필요하다. 초대형 요청 실패를 검증했지만 정말
  남은 힙이 없는 경우·emergency 객체 재사용·재진입의 무할당 경로는 미검증이다.
- catalog는 시제품의 고정 문구만 포함한다. 미등록 메시지 검출, 전체 활성
  `MP_ERROR_TEXT` 목록, subclass/정수 errno/동적 설명의 정책과 coverage가 남는다.
- 언어 스위치는 호스트 fixture다. 실제 소유 콘솔의 모드는 P2 API로 연결해야 한다.

현재 결과만으로 vendor 정책 변경을 요청할 근거는 없다. 남은 경계를 실제로
검증해 불가능한 사례가 나오면 객체·코드·재현을 먼저 남기고 정책 변경을 결정한다.

## 다음 P0 작업

1. 파일 스트림·UTF 기반 계층은 2026-10-26 완료했다. 다음은 실제 nano가 include하는
   libc/POSIX 헤더·심볼 연결, UTF 분류, regex·외부 명령 등 기능 비활성화 경계와
   curses 계층이다. 이를 묶어 **x86_64/AArch64 실제 앱 ELF**를 링크한다.
   호스트 libc 심볼을 성공 stub으로 대체하지 않는다. fsync 및 읽기 오류/EOF 구분처럼
   현재 syscall로 보장할 수 없는 계약은 [후속 기록](feature_p0_nano_compat.md)에 명시했다.
2. 제한된 curses 계층을 **실제 SYS_TTY**에 연결하여 일회용 x86 이미지에서 한글
   조합·저장·종료를 재현한다. 현재 호스트 모델의 정확한 관찰을 타깃으로 옮긴다.
3. MicroPython의 활성 생성 경계·전체 메시지 목록과 bounded 수명·고갈 경로를
   검증한다. [출력 목록](feature_p0_outputs.md)을 개별 메시지 catalog로 확장한다.
4. 위 조건이 채워져야 P0를 완료 표시하고 P1 머꼬 `>> ` 작업으로 넘어간다.
   Pi 5 전체 UI/SD/입력 검증은 P6에서 별도로 기록한다.
