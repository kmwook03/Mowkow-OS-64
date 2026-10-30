# P3 한/영 표시와 공통 하단 영역

검증일: 2026-10-30. 기준 커밋: `b23fe6a2378da5a5c07663b4561f23052991e180` 위
미커밋 작업. P3 완료이며 다음 단계는 P4다. Pi 5 실기기 검증은 P6에서 수행한다.

## 화면과 geometry 계약

`GUI_CONTEXT64`가 화면 오른쪽 아래의 32×28픽셀 시트와 896바이트 고정 버퍼를 소유한다.
포커스된 콘솔의 모드가 한글이면 `한`, 영문이면 `영`을 표시한다.
입력을 받을 콘솔이 없거나 종료 준비가 끝난 경우 회색 `--`로 비활성화한다.
표시는 상태 전용이며 클릭으로 모드나 포커스를 변경하지 않는다.

2026-10-30 사용자 요구 변경에 따라 창 모드에서만 화면 하단 28픽셀을 공통 영역으로
예약한다. 작업표시줄과 한/영 표시가 이 영역을 공유하고, 전체화면에서는 둘 다 숨긴다.
전체화면 콘솔은 하단까지 화면 전체를 검은 배경으로 덮는다. nano도 현재 창 모드를 따른다.
창 → 작업표시줄 → 한/영 표시 → 마우스 순서로 합성하며, 표시 시트는 앱 내용과 겹치지 않는다.
작업표시줄 버튼의 그리기와 클릭 판정 모두 오른쪽 32픽셀을 제외한다.
그릴 수 없는 마지막 버튼의 남은 조각도 클릭되지 않는다.
새 창의 계단식 위치와 수직 드래그는 예약 영역 위로 제한한다.

전체화면 콘솔 시트의 높이는 `화면 높이`다.
일반 창은 작업 영역 높이의 2/3을 8픽셀 단위로 내림한 높이를 사용하고,
내용 영역은 기존 창 장식 여백을 제외한다. `TTY_SIZE`는 실제 콘솔 내용 크기를
칸 단위로 보고하며 추가 차감을 하지 않는다. 전체화면/창 전환은 기존의 checked
재바인딩·generation·raw RESIZE 경로를 사용한다.

| 검증 화면 | 전체화면 TTY | 창 TTY |
| --- | --- | --- |
| 1280×720 QEMU | 160×45 | 87×26 |
| 320×240 호스트 | 40×15 | 20×6 |
| 64×112 호스트 | 8×7 | 2×1 |

마지막 칸에 포함되지 않는 16픽셀 미만의 여분은 콘솔 내부에 남는다.
Pi GUI 초기화도 실제 콘솔 시트 높이를 사용한다.
syscall 번호·연산·호출 규약과 모드 조회의 소유 콘솔 계약은 유지한다.

## 상태 갱신과 수명

포커스·새 창·닫기·geometry 전환은 GUI irq-save 거래 안에서 표시를 갱신한다.
양 플랫폼의 기존 main-loop reaper는 창 모드에서 입력이 없을 때도 포커스된 콘솔의
모드/generation과 폰트 포인터를 확인한다. 스냅샷이 같으면 다시 그리지 않는다.
전체화면에서는 표시 갱신을 생략하며, 창 모드 복귀 시 숨긴 동안 바뀐 최신 모드를 반영한다.
따라서 cooked·raw·readline·friendly REPL의 같은 모드 변경을 사용하면서 editor를 GUI에
결합하거나 새 대기 루프를 만들지 않는다. 오류 언어는 계속 오류 발생 콘솔에서 조회한다.

F11은 기존처럼 맨 아래 창을 앞으로 올리며 키보드 포커스는 유지한다.
표시는 가려진 창이라도 실제 키보드 소유 콘솔을 따른다. 마우스나 작업표시줄로 포커스를
옮기면 그 콘솔의 모드로 바뀐다. AArch64 USB 입력도 공통 `gui64_handle_key()`를 통해
F11을 먼저 처리한다. PS/2 마우스 디코더를 USB 보드의 링크 의존성으로 추가하지 않는다.

표시 버퍼에는 별도 heap 할당이 없다. 시트 할당·등록 실패는 GUI 초기화를 취소하고
기존 정리/재시도 경로를 사용한다. 작업표시줄의 선택적 할당 실패는 표시와 예약 높이를
바꾸지 않는다. 실패한 free는 기존 소유권을 유지하여 재시도한다.
64×100처럼 장식 창의 최소 내용 크기를 만족하지 못하면 GUI를 게시하지 않는다.
공통 콘솔의 직접 출력 fallback에는 없는 상태 영역을 차감하지 않는다.
Pi 보드의 기존 GUI 초기화 실패 시 부팅 중단 정책은 유지한다.

## 전체화면 요구 변경 검증

2026-10-30, 같은 기준 커밋 위 미커밋 작업에서 다음을 다시 검증했다.

| 경로 | 결과 |
| --- | --- |
| `make check64` | PASS: style 290개 파일, 호스트 104개, x86_64/AArch64·private nano 빌드 |
| 기존 console-state/lifecycle fixture | PASS: ASan/UBSan, 전체화면 표시 숨김·하단 검은 픽셀/합성 소유권·전체 높이·모드 복원·선택적 작업표시줄 실패 |
| Pi 보드 fixture | PASS: 전체 높이 콘솔 초기화·기존 USB/F11 입력 경로 |
| IDE/AHCI QEMU 화면 | PASS: 각 11개 상태, 부팅/cooked/raw/nano의 표시 숨김·검은 하단·창 복귀 표시·TTY 크기 |
| Pi 5 실기기 | 미실행 |

QEMU의 부팅/cooked 및 전체화면 복귀에서는 하단 28픽셀 전체가 검은지 검사했다.
ktest와 nano는 마지막 줄에 앱 도움말을 그리므로 오른쪽 마지막 8픽셀의 검은 배경으로
표시가 없는지 확인했다. 창 모드에서는 기존 32×28픽셀 한/영 raster 전체를 비교했다.
실제 ktest 출력으로 전체화면 160×45칸과 창 87×26칸을 확인했다.
전체화면에서 모드를 바꾼 뒤 창으로 돌아오면 최신 모드가 표시된다.

이번 변경에서는 전체 parity·앱/저장 회귀와 clean build를 다시 실행하지 않았다.
아래 초기 P3 결과와 구분하며 Pi 5 실기기는 계속 P6에 남긴다.

재현 명령:

```sh
PYTHONDONTWRITEBYTECODE=1 make check64
PYTHONDONTWRITEBYTECODE=1 python3 /tmp/mowkow_p3_fullscreen_runtime.py
PYTHONDONTWRITEBYTECODE=1 python3 /tmp/mowkow_p3_fullscreen_runtime.py --ahci
```

로그: `/tmp/mowkow-p3-fullscreen-check64-final.log`,
`/tmp/mowkow-p3-fullscreen-{ide,ahci}.log`와 대응 시리얼·PPM 캡처.
화면 관찰 스크립트는 기존 임시 P3 스크립트를 재사용하며 저장소에 중복 runner를 추가하지 않았다.

## 초기 P3 검증 (전체화면 표시 유지 정책)

아래 기록과 로그는 요구 변경 전 결과다. 전체화면 표시·TTY 크기는 현재 계약과 다르다.

| 경로 | 결과 |
| --- | --- |
| `make clean64` 이후 `make check64` | PASS: style 290개 파일, 호스트 104개, x86_64/AArch64·private nano 빌드 |
| 최종 `make check64` | PASS: 같은 104개와 최종 소스 빌드·staging |
| 기존 console-state fixture | PASS: ASan/UBSan, 한/영 고정 raster·모드·포커스·하단 클릭·드래그·작은 화면 |
| 기존 lifecycle fixture | PASS: 표시 시트 필수 실패, 작업표시줄 선택적 실패, free 재시도·미게시/정리 |
| Pi 보드 fixture | PASS: 줄어든 콘솔 높이, 실제 HID F11/일반 키의 GUI 선처리·콘솔 전달 관찰 |
| IDE `make test64-qemu` | PASS: parity 12개, ktest/raw 복구·mtest 재진입·wtest 전체 내용/크기/재부팅 지속성 |
| AHCI `make test64-qemu-ahci` | PASS: IDE와 동일 회귀 |
| IDE/AHCI 화면 | PASS: 경로별 18개 상태의 실제 한/영 raster·TTY 크기·포커스·앱 종료·비활성 표시 |
| Pi 5 실기기 | 미실행: P6 잔여 검증 |

기존 C fixture와 Python runner를 확장했으며 별도 중복 runner는 만들지 않았다.
한/영 기대값은 H04.FNT의 고정 16×16 raster를 사용한다. 기대 픽셀을 검사 대상
렌더러로 다시 만들지 않는다. renderer 선점 중 GUI 실패의 화면/소유권 불변과
raw resize의 큐·wake·generation 검사는 기존 경로에서 함께 통과한다.

QEMU는 경로마다 일회용 이미지에서 키보드와 실제 마우스 입력을 사용했다.
부팅·cooked·MicroPython friendly REPL·머꼬 줄 입력·나노 raw의 모드 전환,
전체화면/창의 ktest 크기, 새 창·F11·마우스 포커스·주 콘솔 닫기·마지막 콘솔 닫기를
확인했다. 한/영 픽셀은 화면 오른쪽 아래 32×28 전체를 고정 raster와 비교하고,
크기는 실제 `ktest`의 `TTY_SIZE` 출력으로 확인했다. 기존 자체 나노가 대상이며
업스트림 nano 연동은 P4에 남긴다.

재현 명령:

```sh
PYTHONDONTWRITEBYTECODE=1 make clean64
PYTHONDONTWRITEBYTECODE=1 make check64
PYTHONDONTWRITEBYTECODE=1 make test64-qemu
PYTHONDONTWRITEBYTECODE=1 make test64-qemu-ahci
```

현재 수동 재현은 전체화면 부팅과 `나노`에서 하단 한/영 표시 없이 검은 배경이
이어지는지 확인한다. `xwindow` 후에는 Shift+Space → `py` 입력 대기 → 머꼬 줄 입력 →
나노 raw 입력 순서로 표시를 확인한다. 전체화면에서 모드를 바꾸고 창 모드로 돌아왔을 때
최신 표시가 보이는지, `새창`·F11·마우스 포커스·닫기 후에도 소유 콘솔을 따르는지 확인한다.

로그: `/tmp/mowkow-feature-p3-check64-clean.log`, `/tmp/mowkow-feature-p3-check64.log`,
`/tmp/mowkow-feature-p3-regression-{ide,ahci}.log`,
`/tmp/mowkow-feature-p3-status-{ide,ahci}.log`와 대응 시리얼·PPM 캡처.
화면 관찰용 임시 스크립트는 `/tmp/mowkow_p3_status_runtime.py`이며 저장소에는 추가하지 않았다.
