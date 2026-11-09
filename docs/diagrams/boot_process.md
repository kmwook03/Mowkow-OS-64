# 64비트 부트 프로세스

`64bit/src64/`의 구현을 기준으로, 펌웨어 진입부터 콘솔 입력을 처리하는
이벤트 루프까지 나타낸다. 참여자는 관련 모듈을 묶은 것이며, 화살표의 함수명은
실제 초기화 진입점을 가리킨다. 별도 태스크의 실행 시점은 스케줄러에 따라 달라진다.

## x86_64 — BIOS / QEMU

BIOS가 부트 섹터를 실행하면 1단계 로더가 2단계 로더를 읽는다.
2단계 로더는 FAT32 예약 영역의 커널 바이너리를 직접 읽고, 보호 모드에서
커널을 1 MiB로 복사한 뒤 롱 모드로 진입한다.

```mermaid
sequenceDiagram
    autonumber
    participant F as BIOS
    participant B as 1단계 boot64.asm
    participant L as 2단계 loader64.asm
    participant K as 커널 진입 / kernel64_main
    participant P as CPU / 타이머 / PS/2
    participant S as 블록 장치 / FAT32
    participant C as GUI / 콘솔

    F->>B: 부트 섹터를 0x7C00에 적재하고 실행
    B->>B: 세그먼트·스택 설정, 부트 드라이브 DL 보관
    B->>F: INT 13h AH=42h — STAGE2_LBA부터 읽기
    F-->>B: 2단계 로더를 0x8000에 적재
    B->>L: 0x0000:0x8000으로 점프, DL 전달

    Note over L,K: 16비트 리얼 모드
    loop KERNEL_SECTORS를 모두 읽을 때까지
        L->>F: INT 13h AH=42h — KERNEL_LBA부터 최대 128섹터씩
        F-->>L: 0x10000부터 임시 커널 버퍼에 적재
    end
    L->>F: 키보드 LED 상태, VBE 정보 조회·그래픽 모드 설정
    F-->>L: 화면 크기·피치·bpp·프레임버퍼 주소
    L->>L: DISPI 지원 시 1280×720×8bpp 설정, 실패 시 VBE 모드 유지
    L->>L: BOOTINFO64 구성, A20 활성화, GDT 적재
    L->>P: CR0.PE 설정, protected_start로 원거리 점프

    Note over L,K: 32비트 보호 모드
    L->>L: copy_kernel() — 0x10000에서 0x100000으로 복사
    L->>L: setup_page_tables() — 하위 4 GiB 아이덴티티 매핑
    L->>P: CR4.PAE, CR3, EFER.LME, CR0.PG 설정
    L->>L: long_mode_start로 원거리 점프

    Note over L,K: 64비트 롱 모드
    L->>K: 0x100000의 _start 호출, RDI = BOOTINFO64 포인터
    K->>K: 커널 스택 설정, SSE 활성화, BSS 초기화
    K->>K: kernel64_main(boot_info)
    K->>K: COM1 진단 시작, BOOTINFO64 검증
    K->>P: init_gdtidt64()
    K->>K: x86_boot_smoke_bss64()
    K->>P: init_fpu64()
    K->>K: x86_boot_smoke_fpu64()
    K->>K: init_memory64()
    K->>S: block64_init()
    S->>S: AHCI probe 성공 시 AHCI, 그 외 ATA PIO 선택
    K->>S: fd64_init() — 섹터 캐시 초기화, FAT32 마운트
    K->>S: x86_boot_smoke_storage64() — 읽기 자체 점검
    opt BOOT_TEST64=1로 빌드
        K->>S: 파괴적 FAT32 쓰기 자체 점검
    end
    K->>K: x86_boot_smoke_gui_input64() — sheet·키보드·한글 점검
    K->>S: x86_boot_load_font64() — H04.FNT 읽기
    S-->>K: 한글 글꼴 데이터
    K->>C: init_palette64(), console64_init(boot_info)
    C->>C: gui64_init(), 콘솔 연결, 안내·프롬프트 표시
    K->>K: task_init64(), 이벤트 FIFO 초기화
    K->>C: console64_start_task(console64_active())
    K->>P: init_pit64(event_fifo), init_pic64()
    K->>P: io_sti() — IRQ 활성화
    K->>P: init_keyboard64(), init_mouse64()
    Note over K,P: PS/2 초기화는 IRQ 활성화 뒤에 실행<br/>마우스 초기화 실패는 진단 후 계속 진행

    loop 최상위 이벤트 루프
        P-->>K: IRQ 처리기가 타이머·키보드·마우스 이벤트를 FIFO에 기록
        alt FIFO에 이벤트가 있음
            K->>C: 커서 갱신 또는 GUI 시스템 이벤트 처리
            opt GUI가 소비하지 않은 키보드 이벤트
                K->>C: 포커스 콘솔의 태스크에 키 전달
            end
            K->>C: console64_reap_closed()
        else FIFO가 비어 있음
            K->>K: task_sleep64() — 다음 이벤트까지 대기
        end
    end
```

BIOS 디스크 읽기 실패는 오류를 출력하고 정지한다. 필수 커널 초기화 실패는
`x86_boot_panic64()`로 단계와 오류를 기록하고 정지한다.
`console64_init()`은 GUI 생성이 실패하면 프레임버퍼 직접 출력으로 진행한다.

## AArch64 — Raspberry Pi 5

Pi 펌웨어가 `kernel_2712.img`를 물리 주소 `0x80000`에 적재한다.
커널은 EL1과 초기 MMU 매핑을 준비한 뒤 스택과 실행 주소를 TTBR1의
high-half 매핑으로 옮겨 보드 초기화를 이어간다.

```mermaid
sequenceDiagram
    autonumber
    participant F as Pi 펌웨어 / mailbox
    participant E as boot64.S
    participant K as board64.c
    participant P as MMU / 예외 / 스케줄러
    participant S as SDHCI / FAT32
    participant U as PCIe / RP1 / USB
    participant C as GUI / 콘솔

    F->>E: kernel_2712.img 적재·실행, X0 = DTB 포인터
    E->>E: DTB 포인터 보관, 코어 ID 확인
    Note over E: 부트 코어만 다음 단계로 진행<br/>보조 코어는 WFE 대기 루프에 머묾
    opt EL2로 진입한 경우
        E->>E: EL1 실행·물리 카운터 접근 설정, ERET로 EL1h 전환
    end
    E->>E: 인터럽트 마스크, FP/SIMD 허용, 스택·BSS 초기화
    E->>E: boot_dtb_ptr에 DTB 포인터 저장
    E->>K: aarch64_main()
    K->>P: arch64_early_init() — ACT LED 준비
    K->>P: arch64_mmu_init()
    P->>P: TTBR0 임시 매핑·TTBR1 high-half 매핑, MMU·캐시 활성화
    K->>E: arch64_enter_high()
    E->>E: SP를 high-half로 전환, 낮은 주소의 C 호출 체인 폐기
    E->>K: high-half의 aarch64_high_main()으로 분기

    Note over K,P: 이후 커널은 high-half 주소에서 실행
    K->>P: arch64_mmu_finish_high() — TTBR0 임시 매핑 제거
    K->>P: arch64_cpu_init() — 예외 벡터 등록
    K->>P: arch64_fb_probe(bootinfo)
    P->>F: property mailbox로 프레임버퍼 요청
    F-->>P: 화면 크기·피치·버퍼 주소·용량 응답
    P-->>K: 응답 검증, 프레임버퍼·BOOTINFO64 준비
    K->>C: init_palette64()
    K->>P: arch64_mmu_self_test(), arch64_irqctl_init()
    K->>K: init_memory64()
    K->>P: arch64_scheduler_init() — 태스크·검증용 worker 준비
    K->>P: arch64_timer_init(NULL) — 100 Hz generic timer 설정
    K->>S: block64_init() — SDHCI probe
    K->>S: fd64_init() — 섹터 캐시 초기화, FAT32 마운트
    opt BOOT_TEST64=1로 빌드
        K->>S: aarch64_boot_smoke_storage64() — 파괴적 쓰기 점검
    end
    K->>S: load_hangul_font64() — H04.FNT 읽기
    S-->>K: 커널 수명 동안 유지하는 한글 글꼴 데이터

    K->>U: pcie64_probe_rp1(), rp164_probe()
    K->>U: RP1 UART 레지스터·루프백 점검, BAR1 주소 확인
    K->>U: xhci64_probe_rp1(), xhci64_reset_rp1()
    K->>U: pcie64_enable_rp1_dma(), xhci64_start_rp1()
    K->>U: 루트 포트 목록 확인, No-op 명령 점검
    K->>U: 키보드 포트 reset → Enable Slot → Address Device
    K->>U: descriptor 조회 → boot keyboard 검색 → endpoint 구성
    K->>U: aarch64_boot_smoke_keyboard64()
    Note over K,U: 키 누름·뗌을 한 번 소비한 뒤 live 입력을 준비
    K->>K: aarch64_boot_smoke_mouse64() — HID 디코더 점검
    K->>U: 키보드와 다른 컨트롤러에서 USB 마우스 열거·구성
    Note over K,U: 장치 초기화 실패 시 해당 포트 격리·슬롯 퇴역 시도<br/>퇴역 성공 시 포트 수 이내에서 재시도, 실패한 입력 장치 없이 계속 진행
    K->>U: 키보드 컨트롤러 선택 복원

    K->>C: sheet·framebuffer·window 자체 점검
    K->>C: init_gui64(bootinfo) — GUI 생성·점검
    K->>P: arch64_timer_set_debug_output(0)
    K->>C: init_console64(bootinfo)
    C->>C: 글꼴·sheet 연결, 프롬프트 표시, 콘솔 태스크 시작
    K->>K: live HID FIFO와 디코더 상태 초기화
    K->>U: 사용 가능한 키보드·마우스 interrupt transfer arm
    K->>U: 키보드 컨트롤러 선택 복원
    K->>K: ACT LED heartbeat용 500 ms deadline 준비
    K->>P: arch64_irq_enable()

    loop 단일 태스크의 USB / GUI 폴링 루프
        K->>U: 컨트롤러 선택, 키보드·마우스 완료 보고서 poll·rearm
        U-->>K: 사용 가능한 장치의 HID 보고서
        K->>C: 키 디코딩·GUI 단축키·포커스 콘솔 입력·마우스 이벤트 전달
        K->>C: 커서 갱신, console64_reap_closed()
        opt 500 ms heartbeat deadline 도달
            K->>P: 스케줄러 heartbeat, FP/SIMD 문맥 자체 점검
            K->>K: ACT LED 토글, 다음 deadline 갱신
        end
        Note over K,P: generic timer IRQ가 태스크 스케줄링을 진행
    end
```

예상하지 않은 진입 EL이나 필수 플랫폼 초기화 실패는 ACT LED panic 점멸로
정지한다. USB 키보드·마우스의 장치별 실패는 해당 입력 경로를 비활성화하고
GUI·heartbeat와 정상 입력 장치의 처리를 이어간다.
타이머 설정과 IRQ 활성화는 서로 다른 단계이며, live USB 처리는 IRQ가 아닌
보드의 단일 태스크에서 폴링한다.

## 구현 위치

| 구간 | 소스 |
| --- | --- |
| x86 BIOS 1·2단계 로더 | [boot64.asm](../../64bit/src64/boot/boot64.asm), [loader64.asm](../../64bit/src64/boot/loader64.asm) |
| x86 커널 진입·초기화·이벤트 루프 | [asmfunc64.asm](../../64bit/src64/kernel/asmfunc64.asm), [kernel64.c](../../64bit/src64/kernel/kernel64.c) |
| x86 진단·부트 자체 점검 | [boot_diag64.c](../../64bit/src64/arch/x86_64/boot_diag64.c), [boot_smoke64.c](../../64bit/src64/arch/x86_64/boot_smoke64.c) |
| Pi 진입·high-half 전환·보드 초기화 | [boot64.S](../../64bit/src64/arch/aarch64/boot64.S), [board64.c](../../64bit/src64/arch/aarch64/board64.c) |
| Pi MMU·스케줄러·타이머·부트 자체 점검 | [mmu64.c](../../64bit/src64/arch/aarch64/mmu64.c), [sched64.c](../../64bit/src64/arch/aarch64/sched64.c), [gtimer64.c](../../64bit/src64/arch/aarch64/gtimer64.c), [boot_smoke64.c](../../64bit/src64/arch/aarch64/boot_smoke64.c) |
| 공통 저장 장치·파일시스템 | [block64.c](../../64bit/src64/drivers/block64.c), [cache64.c](../../64bit/src64/kernel/cache64.c), [fd64.c](../../64bit/src64/kernel/fd64.c) |
| 공통 GUI·콘솔 초기화·태스크 | [gui64.c](../../64bit/src64/kernel/gui64.c), [console64.c](../../64bit/src64/kernel/console64.c) |

파괴적 저장 장치 점검은 두 포트 모두 `BOOT_TEST64=1`
(`MOWKOW64_BOOT_TESTS=1`) 빌드에서만 실행한다.
