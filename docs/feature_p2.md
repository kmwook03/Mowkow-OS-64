# P2 콘솔·TTY 커서와 입력 모드 API

검증일: 2026-10-30. 기준 커밋: `443a962dab2ce902895ea215291094df0e77763f` 위
미커밋 작업. P2 완료이며 다음 단계는 P3다. Pi 5 실기기 검증은 P6에서 수행한다.

## 동작과 소유권

콘솔마다 출력 위치와 raw 앱의 마지막 `TTY_MOVE` 위치, 희망 표시 여부, 폭,
깜빡임 위상, 최근 활동 시각, 포커스와 원본 밑줄 픽셀을 소유한다.
본문을 그리는 가장 바깥 renderer 진입에서 원본을 복원하고, 마지막 참조가
해제될 때 새 위치의 맨 아래 한 줄을 저장한 뒤 팔레트 하위 비트를 반전한다.
원본은 최대 16바이트이며 표시·숨김에 할당이나 대기를 사용하지 않는다.
선택 영역과 한글 조합 위에서도 숨김 시 원래 픽셀을 그대로 돌려놓는다.
문자색·배경색도 콘솔별로 보관하여 다른 앱의 선택 색과 종료 복구가 섞이지 않는다.

키 입력·이동·포커스 복귀 직후 위상을 표시로 초기화한다.
100Hz 시각의 unsigned 차이를 50틱으로 나누어 약 500ms마다 전환하며 wrap과
늦은 갱신을 처리한다. x86의 타이머 이벤트 소비 경로와 AArch64 live loop가
`console64_cursor_update()`를 호출한다. IRQ에서 그리거나 새 반복 대기를 만들지 않는다.
선점된 renderer가 있으면 tick·포커스는 상태만 갱신하고 픽셀 접근은 마지막 renderer가
완료할 때까지 미룬다. resize·close는 기존 renderer 참조와 GUI irq-save 거래를 따른다.

OS 명령 실행 전에는 커서를 숨기고 프롬프트에서 복구한다.
일반 read·MicroPython friendly REPL의 문자 대기·머꼬/readline의 줄 대기에서도
대기 중에만 표시한다. cooked 한글 조합 밑줄은 예약된 두 칸에 표시하고,
조합 확정·취소 후에는 다음 입력 위치의 한 칸으로 돌아간다.
오른쪽 끝에서는 다음 행을 미리 가리키며, 마지막 행에서는 실제 출력이 스크롤할 때까지
마지막 칸에 머문다. 커서 갱신 자체는 스크롤이나 시리얼 줄바꿈을 만들지 않는다.

raw 진입은 표시/한 칸을 기본값으로 사용한다. 출력은 raw 커서 요청 위치를
전진시키지 않는다. 요청 위치에 한글을 그리면 두 칸 폭을 사용한다.
`TTY_MOVE`는 새 위치를 지정하고 한 칸 폭으로 시작하며 `TTY_CURSOR`로 폭을 명시할 수 있다.
미완료 raw dirty 영역이 있으면 표시를 미루고 `TTY_FLUSH`에서 본문과 함께 올린다.
resize는 기존 밑줄을 제거하고 raw 위치를 무효화하므로 앱이 새 `TTY_MOVE`와 flush를
완료할 때까지 숨긴다. 앱 종료는 자기 콘솔의 raw·조합·색·커서를 정리하며 다음
cooked 입력 대기에서 커서를 복구한다.

## 추가 TTY 계약

`SYS_TTY=10`, 기존 연산 0~6, 두 아키텍처의 호출 레지스터와 기존 반환 규약을 유지한다.

| 연산 | 인수·반환 |
| --- | --- |
| `TTY_CURSOR=7` | 표시 희망 0/1, 폭 1/2칸. 위치를 바꾸지 않는다. 성공 0, 잘못된 인수·대상은 -22. |
| `TTY_INPUT_MODE=8` | 인수 없음. bit 0은 한글 여부, bits 1~32는 uint32 generation. 실패 -22. |
| CRT | `long tty_cursor(int visible, int cells)`, `long tty_input_mode(void)` |
| 반환값 해석 | 성공 여부를 먼저 확인하고 `TTY_INPUT_HANGUL(v)`, `TTY_INPUT_GEN(v)` 사용. |

모드 반환값은 generation이 UINT32_MAX여도 음수가 되지 않는다.
조회·표시 제어는 syscall의 `process->console`을 직접 사용하므로 GUI 포커스와 무관하다.
모든 한/영 전환은 하나의 irq-save 변경 경로를 통해 콘솔별 generation을 증가시킨다.
cooked·raw·줄 입력·friendly REPL·일반 read의 Shift+Space가 같은 상태를 사용한다.
기존 raw CHAR/PREEDIT/KEY/RESIZE 이벤트 형식은 유지하며 별도 모드 이벤트는 추가하지 않았다.
일반 read와 friendly REPL의 기존 문자 입력 범위는 유지하고, Shift+Space는 이제 모드 전환으로
소비한다. 한국어 상태 표시 영역과 전체화면 TTY geometry 예약은 P3에서 구현한다.

## 검증

| 경로 | 결과 |
| --- | --- |
| `make clean64` 후 `make check64` | PASS: style 290개 파일, 호스트 104개, 양 아키텍처·private nano 빌드 |
| 기존 console-state fixture | PASS: ASan/UBSan, 기존 수명 실패 주입과 실제 editor/keyboard를 함께 실행 |
| ABI/CRT fixture | PASS: 커널·CRT 연산 정의 일치, signed 오류, 큰 인수·폭·NULL 거부, generation 최대값 |
| x86/AArch64 보드 fixture | PASS: 생산 이벤트/live loop가 task 문맥에서 현재 틱으로 갱신 호출 |
| IDE `make test64-qemu` | PASS: parity 12개, ktest/raw 복구·mtest 재진입·wtest 내용/크기/재부팅 지속성 |
| AHCI `make test64-qemu-ahci` | PASS: 동일 회귀 |
| IDE/AHCI 화면 | PASS: 경로별 15개 상태, 무입력·한글 조합·창/전체화면·앱 종료·마우스 포커스 전이 |
| Pi 5 실기기 | 미실행: P6 잔여 검증 |

console-state 검사는 본문 전체와 stride padding을 독립 패턴으로 채워 원본과 비교한다.
49/50틱, 시각 wrap, 지연된 위상, 입력 직후 표시, 두 콘솔 포커스, raw 출력 위치 분리,
선택 색, 두 칸 한글·조합 확정/취소, 오른쪽 끝·줄바꿈·스크롤, raw resize·앱 종료를 확인한다.
일반 read·friendly REPL·readline·raw의 입력 모드 전환과 generation wrap도 검사한다.
별도 C runner를 만들지 않고 기존 fixture를 확장했다.

화면 검사는 일회용 IDE/AHCI 이미지에서 QMP 키·마우스 입력과 `screendump`를 사용한다.
각 상태에서 약 110ms 간격으로 16장을 수집하여 위상이 두 개뿐인지, 차이가 한 줄의
8픽셀 또는 16픽셀뿐인지 비교한다. raw 실행 중 resize와 close 충돌·실패 주입은
호스트 fixture에서 검증하며, QEMU에서는 창/전체화면의 앱 실행과 종료를 확인한다.
F11은 기존처럼 창만 앞으로 올린다. 키보드 포커스 복귀는 실제 마우스 클릭으로 검증한다.
업스트림 nano 교체와 Pi 5의 무입력 깜빡임·USB 입력·화면 관찰은 각각 P4·P6의 범위다.

재현 명령:

```sh
PYTHONDONTWRITEBYTECODE=1 make clean64
PYTHONDONTWRITEBYTECODE=1 make check64
PYTHONDONTWRITEBYTECODE=1 make test64-qemu
PYTHONDONTWRITEBYTECODE=1 make test64-qemu-ahci
```

수동 화면 재현은 OS 프롬프트 → `가` 조합 → MicroPython `py` → 머꼬 줄 입력 →
기존 `나노`의 빈 화면/한글 조합/종료 순서로 확인한다. `xwindow` 전후에 나노를
다시 실행하고, `new`로 두 번째 콘솔을 만든 뒤 F11과 마우스로 창·포커스를 전환한다.
각 입력 대기에서 키를 누르지 않고 두 위상을 관찰한다.

이번 실행의 로그는 `/tmp/mowkow-feature-p2-check64.log`,
`/tmp/mowkow-feature-p2-regression-{ide,ahci}.log`,
`/tmp/mowkow-feature-p2-cursor-{ide,ahci}.log`와 대응 시리얼·PPM 캡처에 있다.
화면 관찰용 임시 스크립트는 `/tmp/mowkow_p2_cursor_runtime.py`이며 저장소에는 추가하지 않았다.
