# P0 업스트림 nano의 freestanding ELF 링크

후속: 실제 열기·저장은 [파일 경계 연결 기록](feature_p0_nano_storage.md)에서 활성화했다.
아래 결과와 storage-ready 거부 설명은 3차 링크 작업 당시의 이력이다.

2026-10-26, 기준 `f7e54bf` 위의 미커밋 작업. **x86_64/AArch64 전체 ELF 링크
하위 작업은 완료했다. 실제 OS에서 파일을 여는 편집기로 전환하는 작업과 P0 전체는
미완료다.** 기존 `나노` 앱과 이미지 payload는 유지한다.

## 재현과 산출물

필요한 도구는 기존 양 아키텍처 cross toolchain, Python 3, GNU patch다. 호스트
테스트에는 C 컴파일러와 ASan/UBSan이 필요하다. configure 실행, 호스트 libc 헤더,
gnulib, ncurses 라이브러리, 실행 가능한 타깃 바이너리의 호스트 probe는 필요 없다.

```sh
make nano64-apps
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s 64bit/tests -p test_nano_upstream64.py -v
make clean64
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
```

결과는 `build64/nano-app/{x86_64,aarch64}/nano.elf`와 같은 위치의 `nano.map`이다.
두 파일은 static ELF64 ET_EXEC이며 기존 앱 CRT와 `app64.ld`를 사용한다. 일반 앱
자동 탐색·FAT32 이미지·Pi staging에는 추가하지 않았다. P4의 기존 앱 교체와 구분한다.

## 소스·설정·패치

원본은 `third_party/nano/source.json`에 고정한 9.3이다. `prepare_nano64.py`는
manifest의 모든 파일 SHA-256을 확인한 뒤 `build64/nano-upstream/`에 복사하고,
`64bit/nano64/patches/0001-freestanding-boundaries.patch`를 fuzz 없이 적용한다.
원본 vendor와 라이선스는 편집하지 않는다. COPYING/COPYING.DOC/AUTHORS/ChangeLog/NEWS도
작업 복사본에 보존한다. 검증이나 패치 실패는 기존 작업 복사본을 교체하기 전에 실패한다.

패치 재적용은 `make clean64`, `make nano64-apps`로 재현한다. 버전을 올리면 원본
manifest·config·patch를 함께 검토하고 같은 host/양 아키텍처 검증을 다시 수행한다.
준비 결과 marker에는 manifest·patch·준비 도구의 hash를 기록한다.

`config.h`는 명시적인 타깃 설정이다. tiny를 켜지 않아 undo/redo를 유지하며 UTF-8을
활성화한다. 색상 구문 강조·마우스·nanorc·다중 버퍼·브라우저·spell/lint/formatter·
히스토리·자동 wrapping 등은 비활성이다. 긴 Ctrl-G 도움말도 비활성이며 기본 하단
단축키는 남는다. NLS/catalog는 P5 전까지 미구현이다.

공용 `NANO64_VENDOR_NAMES`는 chars/cut/files/global/help/move/nano/prompt/search/
text/utils/winio를 양 아키텍처에 동일하게 제공한다. vendor만 작은 경고 정책을 쓰고,
새 libc/POSIX/curses와 모든 private header는 기존 first-party strict `-Werror`
정책으로 컴파일한다. `check64`는 이제 `nano64-apps`도 포함한다.

패치의 동작 변경은 다음 경계에 한정한다.

- 외부 명령과 suspend는 화면에 불가 오류를 표시하고 실행하지 않는다.
- 정규식 CLI 요청은 실패하고 검색 프롬프트의 regex 토글은 사용 불가를 표시한다.
  literal 검색·바꾸기는 유지한다. regex API도 오류만 반환하며 compiled 객체를 만들지 않는다.
- ANSI bracketed-paste 제어 문자열과 시작 시 POSIX signal 등록을 제거한다.
  raw/cooked 전환과 resize는 TTY 호출·이벤트를 사용한다.
- timed input은 끈다. blocking 입력 실패는 즉시 종료 경로로 보내 반복 대기를 막는다.
- emergency 이름 탐색은 메타데이터가 준비되기 전에는 고유 이름을 보장할 수 없어 거부한다.
- `open_file`과 `write_file`은 storage-ready 검사에서 현재 명시적으로 거부한다.
  없는 파일로 오인하거나 truncate 후 동기화 실패를 발견하는 경로를 막는다.
  원래 읽기·저장 본체는 ELF에 남아 있으며 다음 연결 작업의 대상이다.

## 호환 계층의 실제 범위

| 영역 | 구현 / 남은 제약 |
| --- | --- |
| 메모리 | CRT malloc/free 위 private 크기·seal·소유 목록, realloc 실패 시 원본 보존 |
| 문자열 | 이동·검색·ASCII 비교·수 변환. unbounded 세 ABI는 vendor 호환 정의/선언만 style 예외 |
| 서식 | 정수·문자·문자열·폭/precision/길이·bounded 결과. `%n`, 부동소수 등 미지원 서식은 오류 |
| FILE | 기존 stream에 닫을 수 없는 console stdout/stderr 추가; 실제 CRT I/O 경계 사용 |
| UTF | 이전 bounded codec·OS 셀 폭, ASCII/한글 단어 분류. 전체 Unicode locale/case-fold는 아님 |
| 옵션 | short/long 옵션과 인수 처리; GNU의 인수 재정렬·긴 옵션 축약은 제공하지 않음 |
| 시간 | 양 플랫폼 100Hz ticks에서 얻은 boot 후 monotonic 초; wall clock은 아님 |
| 화면 | 창·셀·폭·속성·이동·클리핑·스크롤·refresh를 TTY move/attr/write/flush로 제출 |
| 입력 | CHAR UTF 바이트, Alt/Ctrl와 기본 특수 키, pushback, RESIZE, 화면 전용 PREEDIT |
| 미지원 OS 기능 | stat/access/realpath/fsync/metadata/임시 파일/삭제/프로세스/signal 등은 명시적 오류 |

`strcpy/strcat/sprintf`는 원본 nano의 C ABI 호출용이다. first-party 코드에는 호출·alias
예외를 허용하지 않는다. style 테스트가 정확한 선언·정의만 허용하는지 검사한다.
일반 문자열 생성은 bounded helper를 사용한다.

curses는 전체 ncurses 호환 구현이 아니다. nonblocking 조회는 private UTF/pushback
버퍼만 비우고 커널 큐를 비차단 조회하지 않는다. halfdelay/napms/beep/커서 표시 제어는
오류이며, P2의 밑줄 커서 API로 연결하지 않았다. BOLD/REVERSE는 TTY 색에 매핑한다.
오른쪽 끝에서 wrap하지 않고 잘라내며 전체 셀 화면을 다시 제출한다. 성능·좁은 창·
모든 수정 키·포커스·실제 framebuffer 픽셀·커서 깜빡임은 OS 검증이 남아 있다.

PREEDIT는 셀 원본을 바꾸지 않고 제출 시점에만 합성한다. 다음 이벤트에서 본문을 다시
그려 복구한다. 화면 generation 변경과 실제 키가 함께 관찰되면 키를 보관하고 창 재생성
뒤에 전달한다. renderer 참조와 OS 창 수명은 아직 실제 TTY 경로에서 검증하지 않았다.

## 검증과 완료 경계

신규 host runner는 **패치된 실제 업스트림 main/편집 루프와 새 port 전체**를 ASan·UBSan으로
컴파일한다. 호스트 libc 대신 private 헤더·문자열·서식·UTF·curses를 사용하고 실제 CRT
syscall wrapper를 통과한다. native gate와 바닥 malloc/free만 fixture로 대체한다.
따라서 CRT allocator 자체 검증은 기존 별도 runner가 담당한다. LSan은 기존 정책과 같이 끈다.

- 실제 한글 입력, PREEDIT 버퍼/undo 분리, undo/redo, 검색 위치, `한→가` 바꾸기,
  80×25→60×20 resize, Ctrl-X/종료 0과 cooked 복구를 검증한다.
- 실제 `open_file`과 Ctrl-O 저장 경로가 readiness를 조회하고 거부하는지 관찰한다.
  수정 상태·last_saved가 유지되고 파일 open/write syscall이 0회임을 확인한다.
- 두 칸 문자의 반쪽 덮어쓰기, 오른쪽 끝 잘림, malformed UTF, 양방향 스크롤,
  resize 할당 실패 시 기존 geometry 보존, private nodelay/pushback을 검증한다.
- 서식 overflow/금지 서식·정수 경계·realloc 실패와 원본·옵션 파싱·지원하지 않는
  OS 기능의 오류와 출력 미변경을 검증한다. 원본 hash 실패 시 기존 stage도 보존한다.
- 양 아키텍처 ELF의 undefined symbol 부재, ET_EXEC/machine/entry/segment 범위와
  동적 loader 부재를 확인한다. 기존 OS 빌드와 host 회귀도 `check64`에서 확인한다.

실행 결과와 hash는 `/tmp/mowkow-p0-nano-link64/`의 `check64.log`, `clean64.log`,
`report.json`, 아키텍처별 ELF/symbol 로그에 기록한다. 생성 산출물은 커밋하지 않는다.
최종 clean64/check64는 PASS: style 291개 파일, host 101개, 기존 양 OS 빌드와
새 양 아키텍처 ELF 링크. ELF 크기는 x86_64 176,736 bytes, AArch64 243,792 bytes다.
**QEMU IDE/AHCI/Pi 5는 실행하지 않았다. 저장 성공이나 실제 OS TTY 통과로 판정하지 않는다.**

다음 작업은 **파일 메타데이터·읽기 오류·동기화의 명시적 포트 계약과 64KiB 저장 사전 검사를
구현해 storage-ready 거부를 해제하는 것**이다. 기존 syscall 의미를 보존하고 지원 없는
속성·내구성·원자적 저장을 성공으로 가장하지 않아야 한다. 이후 일회용 IDE/AHCI 이미지에서
실제 SYS_TTY 한글 조합·열기·저장·종료와 재부팅 후 정확한 파일 바이트/EOF를 검증한다.
