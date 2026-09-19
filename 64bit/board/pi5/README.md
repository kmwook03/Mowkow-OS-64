# Raspberry Pi 5 boot files

M1은 아직 SD 카드 이미지를 만들지 않는다. FAT32 부트 파티션에 다음 두 파일을
복사한다.

- `config.txt`: 이 디렉터리의 파일
- `kernel_2712.img`: `make aarch64`가 만든
  `build64/aarch64/kernel_2712.img`

전원을 넣으면 정상 부팅 시 녹색 ACT LED가 0.5초 간격으로 계속 켜지고 꺼진다.
초기 exception level이 예상과 다르면 panic code 1(0.15초 한 번 점멸 후 1.2초
정지)을 반복한다. M1에는 화면이나 시리얼 출력이 없다.

M2 이미지는 MMU를 켠 뒤 mailbox property channel로 1920x1080 32bpp
framebuffer를 요청한다. 성공하면 HDMI에 다음 문구가 나타나며 ACT LED의 M1
heartbeat도 계속된다.

```text
Mowkow OS
Raspberry Pi 5 / AArch64
M2: MMU + mailbox framebuffer OK

한글 화면 출력 성공
```

M2 조기 실패는 ACT LED panic code로 구분한다.

- 2회 점멸: mailbox 전송 timeout
- 3회 점멸: framebuffer 응답 또는 응답 값 검증 실패
- 아무 패턴도 없음: MMU 활성화 전후의 예외 가능성

## Verification status

- 2026-09-19: Raspberry Pi 5 실기에서 M1 부팅 검증 완료.
- FAT32 부트 파티션의 `config.txt`와 `kernel_2712.img`로 부팅했다.
- 녹색 ACT LED가 0.5초 켜짐/0.5초 꺼짐을 지속해서 반복하는 정상
  heartbeat를 확인했다.
- 이 결과로 펌웨어의 flat image 로드, `0x80000` 진입, EL2에서 EL1으로의
  전환, 스택/BSS 초기화, 시스템 카운터 및 GIO_AON LED 제어 경로를 확인했다.

- 2026-09-19: Raspberry Pi 5 실기에서 M2 framebuffer bring-up 검증 완료.
- HDMI에 청록색 배경과 M2 상태 문구, `한글 화면 출력 성공` 문구가
  정상 표시되는 것을 확인했다.
- 화면 출력 중에도 녹색 ACT LED의 0.5초 heartbeat가 지속되는 것을 확인했다.
- 이 결과로 39-bit identity map, EL1 MMU/캐시 활성화, BCM2712 MMIO Device
  매핑, mailbox property channel 8, 1920x1080 32bpp framebuffer 할당,
  framebuffer 캐시 정리 및 내장 영문/한글 글꼴 렌더링 경로를 확인했다.
