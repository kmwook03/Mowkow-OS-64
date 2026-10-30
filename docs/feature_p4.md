# P4 GNU nano 이식과 한글·커서 연동

검증일: 2026-10-30. 기준 소스: `9c8eb5f8a1e5f5fc0622e90668cbb8a8d891b585`
위 미커밋 작업. Pi 5의 USB·SD·화면 실기 검증은 P6에 남긴다.

## 배포 경로와 원본

기존 자체 편집기와 그 내부함수 fixture를 제거하고 GNU nano 9.3을 기존 `나노`
이름으로 설치한다. x86 FAT32의 `나노`와 Pi staging의 `나노.ELF`는 각각
`build64/nano-app/{x86_64,aarch64}/nano.elf`와 같은 바이트다.
보조 소스는 앱 자동 탐색 밖의 `64bit/nano64/`에 있고, 앱 디렉터리에는 설명만 남는다.
다른 앱의 자동 탐색·단일 C 파일 계약과 CRT·syscall 번호는 유지한다.

`third_party/nano/source.json`이 버전·배포 URL·원본 SHA-256을 고정한다.
GPL-3.0-or-later 원본과 COPYING/COPYING.DOC/AUTHORS 등은 수정하지 않았다.
`prepare_nano64.py`가 원본 전부를 검증한 뒤 build 복사본에
`0001-freestanding-boundaries.patch`를 fuzz 없이 적용한다. 실패하면 이전 stage를 보존한다.
재적용은 `make clean64`, `make nano64-apps`로 수행한다.

P0의 파일·미지원 OS 경계 패치에 다음 경계만 추가했다.

- gettext·상태줄·치명 오류를 private catalog에 연결한다. 편집 알고리즘은 upstream을 쓴다.
- 화면 할당·resize 실패를 진단하고 종료하며, 표준 입력 파일 `-`는 명시적으로 거부한다.
- 메모리 고갈 때 미지원 긴급 저장을 재시도하지 않고 미저장 내용 손실을 알린다.
- 제어 문자의 표시 폭을 OS의 한 칸 U+FFFD 정책에 맞춘다. 탭은 탭 폭으로 표시한다.
- gettext 표시가 빠진 `Refresh` 태그 두 곳을 표시한다.

`common64.mk`가 chars/cut/files/global/help/move/nano/prompt/search/text/utils/winio와
port 소스를 양 아키텍처에 제공한다. configure나 타깃 ELF의 호스트 실행은 필요 없다.
vendor는 기존 별도 경고 lane, 자체 포트와 모든 private 헤더는 strict `-Werror` lane이다.
catalog `.def`도 스타일 검사에 포함하며 변경은 양 아키텍처 의존성에 반영된다.

## 실제 지원 범위

| 경계 | 지원과 제약 |
| --- | --- |
| 편집 | 두벌식 한글/UTF-8, 열기·새 파일·저장·종료, 이동·삭제·선택·잘라내기·붙이기, literal 검색/바꾸기, undo/redo |
| 화면 | OS 글꼴 폭의 curses 셀·속성·클리핑·스크롤, 창/전체화면, RESIZE와 generation, 본문·모든 입력 프롬프트의 PREEDIT |
| 기본 키 | Ctrl-O/X/W/역슬래시/K/U, Alt-U/E, 방향·Home/End·PageUp/Down·Delete·Backspace·Shift 선택·Ctrl 단어/문서 이동 |
| 메모리 | CRT 위 private malloc/realloc/free; realloc 실패는 원본 보존. 일반 upstream nmalloc 고갈은 진단 후 종료하며 무수정 저장 상태를 성공으로 바꾸지 않음 |
| UTF | bounded 변환, malformed 1바이트 진행·U+FFFD, 원래 저장 바이트 보존. 한글/호환 자모 두 칸, 그 외 OS fallback 한 칸; 전체 Unicode locale/case-fold 아님 |
| 파일 | FAT root 이름·`./이름`·`/이름`, checked 메타데이터/오류/읽기/sync, 64KiB 사전 검사 |
| 속성 | FAT 읽기 전용·디렉터리·크기·수정 일시·identity. uid/gid·access time·Unix 권한/소유자 변경 미지원 |
| 저장 | 전체 버퍼/선택 영역 overwrite, 부분 write 반복, sync·close 성공 뒤에만 전체 버퍼의 modified/last_saved 갱신. 선택 저장은 전체 버퍼 수정 상태를 유지 |
| 미지원 변경 | append/prepend·백업·긴급 저장·삭제·rename·임시 파일·잠금. 임시/백업 저장은 truncate 전에 거부 |
| 다른 OS 기능 | 프로세스·외부 명령·suspend·signal·regex·디렉터리 탐색 미지원, ENOSYS 또는 명시적 화면 오류 |
| 시간 | 100Hz boot ticks 기반 monotonic 초. RTC/시간대 서비스 아님 |
| 설정 | UTF-8과 non-tiny undo/redo 유지. 색상 구문 강조·nanorc·다중 버퍼·브라우저·spell/lint/formatter·히스토리·자동 hard wrapping·긴 Ctrl-G 도움말은 비활성 |

Ctrl-G는 하단 단축키 표시를 전환한다. 키 설명은 한국어이고 Y/N/A 응답 키와 명령·옵션
토큰은 그대로다. 전체 ncurses ABI나 무시한 시스템 호출의 가짜 성공을 제공하지 않는다.
실제 매체 고갈·하드웨어 오류를 모두 복구하는 편집기는 아니며, 치명적 OOM에서는 프로세스가
종료되고 미저장 내용이 소실될 수 있다. 파일 열기 snapshot 할당 실패와 저장 실패는
기존 편집 버퍼를 유지한다. 프로세스 종료는 파일·heap·raw 소유권의 최종 정리 경계다.

## 커서·조합·출력

curses는 편집/프롬프트의 마지막 위치를 커널 TTY_MOVE로 게시하고 P2의 TTY_CURSOR로
밑줄 표시와 1/2칸 폭을 제어한다. curses의 두 visible 스타일 모두 밑줄로 매핑한다.
본문을 제출할 때는 커서를 숨기고 마지막 위치에서 다시 표시한다. 500ms 깜빡임과
포커스·renderer 참조·resize/close 수명은 기존 커널 경로가 소유한다.

PREEDIT는 화면 제출에만 합성한다. window 셀·편집 줄·undo에는 들어가지 않는다.
조합이 옆 한글의 첫 칸을 덮으면 남은 반쪽을 공백으로 그리고 다음 갱신에서 복원한다.
출력은 바뀐 행만 256바이트 이하 속성별 UTF-8 묶음으로 제출한다. 상태줄 전체가 시리얼
그리기 추적에 남아 기존 도구가 판정 문구를 읽을 수 있다. 비차단 입력은 private UTF/Meta
바이트만 비우고, 실제 입력 대기는 TTY_READKEY에서 잠든다.

raw 방향키는 이전 위치의 조합을 먼저 확정한다. 한글 모드에서도 Ctrl/Alt 앱 단축키는
조합기를 우회한다. 단축키가 확정한 앞선 한글에는 Meta prefix를 붙이지 않는다.
resize와 실제 키가 함께 발견되면 키를 보류했다가 화면을 재구성한 뒤 전달한다.

## 안내·오류 언어

`messages64.def`의 고정 원문/한국어 쌍을 쓴다. 일반 안내는 입력 모드와 관계없이 한국어다.
오류/경고 `statusline(importance >= AHEM)`와 `die`는 인수 평가 전에 소유 콘솔의 모드를
캡처한다. errno 설명과 nested gettext도 같은 snapshot을 쓴다. 초기 TTY 조회 실패는
영문을 사용하며, 번역 조회는 할당하지 않고 errno를 보존한다.

이미 출력된 메시지는 모드 전환으로 재번역하지 않는다. 파일명·검색어·본문은 번역하지 않고
format 인수로 보존한다. 키 토큰·브랜딩·빌드 진단은 별도다. 양 타깃 전처리기의 활성
gettext/N_ 문구와 catalog 포맷·개행을 호스트 검사로 대조한다.
OS·다른 앱·머꼬·MicroPython 전체 catalog는 P5의 별도 범위다.

## 저장 호환성과 실패

읽기는 최대 65,536바이트의 정확한 snapshot과 EOF·메타데이터를 확인한 후 upstream에
게시한다. 중간 오류·조기 EOF·추가 바이트·조회 변경·할당 실패에서 prefix를 삽입하지 않는다.
동일 크기의 동시 외부 쓰기에 대한 원자적 snapshot이나 파일 잠금을 보장하지 않는다.

저장은 실제 LF/CRLF와 모든 줄을 합산해 64KiB 이내인지 truncate 전에 확인한다.
부분 쓰기를 반복하며 write/sync/close 실패는 modified와 last_saved를 유지한다.
저장 중 I/O 실패는 이미 자르거나 쓴 디스크 prefix를 남길 수 있다.
원자적 저장·journal·device cache flush·전원 차단 내구성을 추가하지 않았다.

기존 자체 나노와의 차이도 검사한다. GNU 기본값은 마지막 줄의 개행을 자동으로 추가하고,
`-L`은 마지막 개행이 없는 파일을 그대로 저장한다. 빈 파일은 0바이트이고 DOS 입력은
CRLF를 유지한다. 이전 자체 나노의 매 줄 LF 저장과 구분한다. malformed/control/embedded
NUL은 화면 표현과 저장 바이트를 분리하며, 저장 때 원래 바이트를 보존한다.

## 검증과 이전 fixture 대응

| 이전 자체 편집기 검사 | 새 production 경로 검사 |
| --- | --- |
| bounded UTF·앞뒤 경계·손상 입력 | 실제 upstream mbtowide/char_length/step_left/right, 잘린 UTF 표본·65,025 byte 쌍·10,000 random, ASan/UBSan |
| 제어/손상 표시·저장 보존 | 실제 display_string의 U+FFFD·폭, embedded NUL/control/malformed의 read→write 전체 바이트 |
| 이동·삭제·줄/버퍼 수정 | upstream delete/backspace·cut/paste·undo/redo·선택 저장 후 원본 복원과 대화형 한글 검색/바꾸기 |
| 증가/재할당 실패 | private realloc 원본 보존·화면 두 할당 실패·snapshot 고갈·실제 초기 OOM 종료 |
| open/read/write/close·64KiB | 실제 upstream open_file/write_file·부분 읽기/EOF/메타데이터/부분 write/sync/close 실패·LF/CRLF 한도 |
| 클리핑·좁은 화면 | 실제 curses 반쪽 한글·오른쪽 끝·스크롤·PREEDIT 복원, upstream 1×1·RESIZE·raw 입력 실패 종료 |
| set_status/append_dec helper | helper 제거, bounded format의 signed/폭/잘림·정수 한계와 catalog format 검사 |

공통 bounded-copy/GUI/process guard-page 검사는 유지하며 삭제된 나노 helper에 대한 참조만
제거했다. 기존 console-state fixture에 raw 조합 확정·Alt 단축키 순서 관찰을 추가했다.
별도 GUI lifecycle runner를 만들지 않았다.

## 최종 결과와 재현

```sh
make clean64
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
make test64-nano-storage
make test64-nano-storage-ahci
make test64-qemu
make test64-qemu-ahci
```

- clean/최종 check64: host 104개·style 290개, 양 OS/앱/private port/ELF/staging PASS.
- ASan/UBSan: 실제 upstream 편집·UTF·화면·조합·저장 실패 주입, 초기 OOM·입력 오류와
  1×1 종료 PASS. 동적 resize는 실제 upstream 80×25 → 60×20과 기존 GUI fixture에서 확인했다.
- IDE/AHCI nano 저장: 새/기존 한글 파일·긴 이름 alias 충돌·LF/CRLF·빈 파일·`-L` 무개행·
  정확히 64KiB·초과 열기/저장 거부·raw 복구 PASS. 재부팅 후 전체 바이트·크기·EOF도 일치했다.
- IDE/AHCI UI 각각 15개 상태: 12곳의 정확한 8/16픽셀 밑줄만 약 500ms 주기로 변화하고
  나머지 픽셀은 동일했다. 본문·검색·바꾸기 양쪽·한글 파일명, 한글 모드 undo/redo,
  잘라내기/붙이기, 전체화면/창, 비포커스 커서 숨김·복귀, F11의 포커스 유지,
  콘솔별 한/영 표시와 영어/한국어 외부 명령 오류·이미 그린 오류 보존 PASS.
- QEMU GUI는 실행 전 창 모드를 전환했다. 보조 콘솔에서 실행 중인 주 콘솔을 resize하는
  명령은 기존 정책이 거부하므로 그 경로를 QEMU resize PASS로 세지 않았다.
- IDE/AHCI 기존 회귀: 머꼬 parity 각 12개, ktest 정상/실패 종료와 raw 복구,
  mtest 재진입, wtest HUGE·재부팅 후 정확한 내용 PASS.
- ELF ET_EXEC machine 62/183·실행 PT_LOAD 안의 entry·segment 범위·동적 loader/undefined
  symbol 부재, 이미지/staging 설치본과 ELF 전체 바이트 동일 PASS.
- 기존 이미지 대비 FAT root 파일명 23개와 short entry 24개(볼륨 레이블 포함)는 그대로다. catalog runtime 파일은
  추가하지 않았다. AArch64 linker의 기존 RWX LOAD 경고는 남는다.

| 산출물 | 바이트 | SHA-256 |
| --- | ---: | --- |
| x86_64 nano ELF | 202,000 | `45b17a75464a8c0e6962c8f562c1c051815582c670c5f4195deb01a3254c5a97` |
| AArch64 nano ELF | 265,136 | `4b1d526fcd8d1bba793b7c7fbc55f58831921342f70b6877d643dca703bbd52c` |
| x86 FAT32 image | 67,108,864 | `7e47285534e1c109a50be67c4e7bd310d2b2044bf7f193a95827da8279f8c599` |

증거는 `/tmp/mowkow-p4-report.json`, `mowkow-p4-check64-{clean-final,final}.log`,
`mowkow-p4-host-final.log`, `mowkow-p4-{storage,regression,ui}-{ide,ahci}.log`에 있다.
GUI 도구는 `/tmp/mowkow_p4_ui_runtime.py`이고 P2/P3의 같은 폴더 helper를 사용한다.
시리얼은 `/tmp/mowkow-p4-ui-{ide,ahci}-serial.log`, 화면은
`/tmp/mowkow-p4-{ide,ahci}-*.ppm`과 `mowkow-p4-ide-window.png`다.
기존 회귀의 serial prefix는 IDE `mowkow-regression-ide-0_1wg9rn/serial`,
AHCI `mowkow-regression-ahci-en7f77ns/serial`이다.

초기 검사에서는 삭제된 editor helper를 포함하던 string-copy fixture 참조와 테스트의
버퍼/메타데이터 초기화를 보완했다. GUI 도구의 이중 프롬프트·마우스 가림·미지원 resize
요청도 고친 뒤 위 최종 성공을 기록했다. 최종 주석 정리 전후 ELF·OS image의 hash는 같다.
`32bit/`, vendor 원본, 머꼬 reference는 변경하지 않았다. 생성 build/image/화면은 커밋하지 않는다.

Pi 5 실행·USB·SD·실제 화면은 미실행이며 양 아키텍처 실행의 최종 확정은 P6에서 수행한다.
다음 작업은 P5의 OS·CRT·앱·머꼬·MicroPython 전체 메시지와 예외 번역이다.
