# P0 nano 실제 파일 열기·저장 연결

2026-10-26. GNU nano 9.3의 고정 원본은 보존하고, 검증된 build 복사본의 경계 패치와
`64bit/nano64/`를 수정했다. 독립 ELF는 계속 `nano64-apps`로 빌드한다. 기존 나노 앱과
일반 이미지의 앱 목록은 바꾸지 않았다. P0 전체와 P4 제품 교체 완료를 뜻하지 않는다.

## 파일 확장 ABI

기존 syscall 1~10의 번호·시그니처·오류·부분 I/O 의미는 유지한다. 추가된 `SYS_FILE=11`은
`file_call64(operation, arg0, arg1, arg2)`를 통해 명시적 음수 errno를 반환한다.
공유 레이아웃과 상수는 `64bit/src64/include/file_api64.h` 하나로 관리한다.

| 연산 | 인수 | 성공 |
| --- | --- | --- |
| FILE_STAT64 | 이름, FILE_INFO64 출력, 0 | 0 |
| FILE_OPEN64 | 이름, CREATE/TRUNCATE/WRITING 비트, 0 | fd ≥ 3 |
| FILE_READ64 | fd, 버퍼, 크기 | 읽은 바이트; 실제 EOF는 0 |
| FILE_SYNC64 | fd, 0, 0 | filesystem 동기화 완료 0 |
| FILE_FSTAT64 | fd, FILE_INFO64 출력, 0 | 0 |

이름은 kernel-owned snapshot으로 복사하고 UTF를 검사한다. 출력 범위·크기·플래그·fd는
I/O 전에 확인하며 실패 시 메타데이터 출력과 fd 소유권을 게시하지 않는다. 메타데이터는
FAT 파일 크기, read-only/디렉터리/볼륨 read-only 플래그, 실제 FAT 수정 일시와 디렉터리
항목 위치를 제공한다. 디렉터리 항목이 없는 root의 identity는 0으로 예약한다. FAT에 없는 uid/gid나 권한을 디스크 속성으로 꾸미지 않는다.
POSIX view는 FAT read-only를 읽기/쓰기 비트로 투영하며 uid/gid는 UINT_MAX(없음),
access time은 0(없음)이다. 수정 시각은 FAT 로컬 시각의 달력 필드를 초 단위로 옮기며
RTC·시간대 서비스가 제공되는 것은 아니다. 현재 filesystem의 고정 수정 시각 정책도 유지한다.

새 조회는 디렉터리/FAT 읽기 실패와 파일 없음(-2)을 구분한다. 체인·순환·조회 상한 오류는
-5이며 CREATE로 이어지지 않는다. 8.3 별칭은 완전한 철자를 비교해 긴 이름을 잘라
기존 파일과 혼동하지 않으며, checked CREATE는 legacy 기존-file truncate를 거치지 않는다. 조회/생성/축소는 기존 filesystem mutex 안에서
수행한다. 기존 boolean/unsigned API는 유지한다. 새 READ는 요청과 파일의 남은 크기 중
작은 값보다 적게 읽으면 -5다. 오류 전에 버퍼 prefix와 fd 위치가 바뀔 수 있으므로 호출자는
결과를 폐기해야 한다. READ가 정상적인 부분 요청을 허용하는 CRT 의미를 바꾸지는 않는다.

SYNC는 유효한 파일 fd를 확인한 뒤 기존 `fd64_sync()`의 data → FAT → directory 순서와
dirty 보존·재시도·read-only 전환을 사용한다. fflush는 여전히 stream 오류 확인일 뿐이다.
컨트롤러 쓰기 완료와 filesystem cache 비우기를 확인하며, 새 device cache flush 명령이나
전원 차단 내구성·원자적 저장·journaling을 제공하지 않는다.

## nano 저장 계약

- `stat/access/realpath/open/read/fsync`는 실제 파일 확장을 사용한다. 경로는 FAT root의
  이름·`./이름`·`/이름`만 지원한다. 부모/하위 디렉터리를 버려 다른 파일을 고르지 않는다.
  임시 파일·삭제·권한 변경·프로세스·signal·regex는 계속 명시적 미지원이다.
- 열기에서 ENOENT만 새 파일로 취급한다. 다른 메타데이터 오류와 64KiB 초과는 거부한다.
  fdopen 후 최대 65,536바이트를 전부 읽어 private snapshot에 담고 EOF 및 마지막
  메타데이터를 확인한 뒤 upstream `read_file`에 넘긴다. 중간 오류·조기 EOF·초과 바이트·
  할당 실패에서는 stream을 닫고 편집 버퍼/undo에 prefix를 넣지 않는다.
- 마지막 메타데이터 비교는 관찰 가능한 변경을 거부하지만 file version/잠금이 없는 FAT의
  동시 같은 크기 쓰기까지 원자적 snapshot으로 보장하지 않는다. snapshot이 완성된 뒤
  upstream decoder가 읽는 바이트는 별도 소유하므로 원본 디스크 버퍼와 분리된다.
- 저장은 일반 전체-buffer OVERWRITE만 지원한다. APPEND/PREPEND/backup/emergency 및
  외부 stream 저장은 파일 변경 전에 거부한다. 원본 저장 본체의 embedded NUL 변환,
  줄바꿈, 수정 상태와 last_saved 갱신은 유지한다.
- 모든 줄의 바이트와 **실제 출력할 LF/CRLF**를 먼저 합산한다. 65,536 초과 또는 줄 목록
  상한 초과면 fopen/O_TRUNC 전 실패한다. stream 고갈도 truncate 전에 검출한다.
- fwrite는 부분 쓰기를 반복하고, fflush → 실제 filesystem sync → close가 모두 성공해야
  수정 상태를 해제한다. 쓰기·sync·close 실패는 수정 상태와 last_saved를 유지한다.
  in-place 저장이므로 I/O 실패가 나면 디스크에는 잘리거나 일부 쓰인 파일이 남을 수 있다.
- GNU 기본 동작은 비어 있지 않은 마지막 줄에 LF를 붙인다. `-L`은 마지막 개행을 강제하지
  않는다. 빈 파일은 0바이트, DOS 입력은 CRLF 형식을 유지한다. 이는 기존 자체 나노의
  매 줄 LF 정책과 구별되는 명시적 호환성 항목이다.
- 실제 TTY의 Enter는 LF다. nano nonl 입력에는 CR로 전달하며 Ctrl-J 구분은 유지한다.
  이 차이는 QEMU 저장 프롬프트 검증에서 발견해 입력 adapter와 host fixture를 수정했다.

## 재현과 검증 범위

```sh
make clean64
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
make test64-nano-storage
make test64-nano-storage-ahci
make test64-qemu
make test64-qemu-ahci
```

nano 저장 runtime 도구는 QEMU와 mtools의 mcopy가 필요하다. 선택적으로
`NANO_STORAGE_LOG64=/tmp/고유-prefix`로 로그 위치를 지정한다. 도구가 만든 일회용
이미지만 수정하고 삭제하며, serial/QEMU 로그는 보존한다. 기존 default editor를 바꾸지
않고 검증 ELF를 `nano9`라는 이름으로 해당 임시 이미지에만 넣는다.

host fixture는 실제 upstream 편집 코어·포트·CRT wrapper를 ASan/UBSan으로 실행한다.
중간 read 오류/조기 EOF/추가 바이트/메타데이터 변경/할당 실패 후 편집 버퍼 보존,
64KiB 읽기와 정확한 LF/CRLF 저장 한도, 짧은 write 반복/부분 실패/sync/close 실패 후
modified/last_saved 보존, 닫기 소유권·stream 고갈 전 truncate 미호출을 관찰한다.
기존 syscall fixture는 양 아키텍처 dispatcher와 새 ABI의 잘못된 주소·크기·플래그·fd,
unaligned 출력, errno 전파·게시 시점을 검사한다. 실제 FAT fixture에는 read 실패,
EOF/순환/readonly와 ENOENT/EIO 구분을 추가했다. LSan은 기존 host 정책대로 비활성이다.

runtime은 실제 SYS_TTY 키 입력으로 새 한글 파일 작성, 기존 한글 파일 열기·수정·저장,
잘린 8.3 이름 충돌 방지, DOS·빈 파일·`-L` 무개행·정확히 64KiB 저장, 초과 열기/저장 거부, Ctrl-X 종료 0와 cooked
복구를 확인한다. 종료한 QEMU의 파일을 mcopy로 꺼내 전체 바이트·길이·EOF를 비교한다.
새 QEMU로 같은 디스크를 부팅해 다시 열기·저장·cat을 실행한 뒤 모든 파일을 재비교한다.

Pi 5 실기는 이번 작업에서 실행하지 않는다. AArch64 cross-build 성공은 SD/입력/UI
하드웨어 검증을 대신하지 않는다. 전체화면 성능·모든 키·픽셀·포커스·밑줄 커서·실기 resize는
계속 별도 범위이며, MicroPython 예외 전체 생성 경계·수명/고갈·개별 catalog도 남아 있다.

## 최종 결과

기준 commit `f7e54bfb2fdf3fdfc4802bd3b37ecd343b86c6c7` 위 미커밋 P0 변경으로 검증했다.

- clean64 후 check64 PASS: style 293개 파일, host 101개(ASan/UBSan 포함), 기존 양
  OS·앱·staging 및 독립 x86_64/AArch64 GNU nano ELF.
- `test64-nano-storage`, `test64-nano-storage-ahci`: 양쪽 모두 종료 0, 모든 저장/거부와
  실제 TTY 한글 편집·긴 이름 충돌 방지·재부팅 후 전체 파일 바이트/크기/EOF 비교 PASS.
- `test64-qemu`, `test64-qemu-ahci`: 양쪽 모두 기존 머꼬 12개 전부 PASS,
  ktest raw/cooked·오류 종료, mtest 재진입, wtest HUGE와 재부팅 내용 검증 PASS.
- ELF64 ET_EXEC machine 62/183, entry의 executable PT_LOAD 포함, segment 범위,
  동적 loader/undefined symbol 부재와 실제 편집·파일 경계 심볼 존재 PASS.
- x86_64 nano ELF: 185,112 bytes, SHA-256
  `4200354468d9dc4a00aa69d96e7ba43937920353302ed367e3748a9a685aeb51`.
- AArch64 nano ELF: 244,024 bytes, SHA-256
  `2b0087eef87151f5b57e5eb57b8560e7edbb35451e62e15bcd558b313af53691`.
  기존 app linker의 RWX LOAD 경고는 남으며 W^X 해결을 주장하지 않는다.

증거는 `/tmp/mowkow-p0-nano-storage64/`의 `clean64.log`, `check64.log`, `validation.log`,
`report.json`, `62-elf.log`, `183-elf.log`, `ide-complete-{edit,reboot}.log`,
`ahci-complete-{edit,reboot}.log`에 있다. report에는 소스·ELF·이미지 hash와 실제 실행 범위를
기록했다. 기존 smoke serial prefix는 `/tmp/mowkow-regression-ide-0er2nk6x/serial`과
`/tmp/mowkow-regression-ahci-dcvvly57/serial`이다. 생성물은 커밋하지 않는다.

초기 runtime 도구의 프롬프트 문구와 Enter 차이를 수정한 뒤 위 최종 검사를 다시 통과했다.
clean 검사에서 새 CRT fixture가 legacy 검사용 응답을 복구하지 않은 오류도 수정했다.
최종 gate 결과와 초기 실패 로그를 구분해 보존했다.

다음 P0 작업은 **MicroPython 활성 예외 생성 경계·전체 메시지 목록 조사**다. 이어
bounded 출처 수명·진짜 OOM·emergency 객체 재사용과 개별 출력 catalog를 검증한다.
vendor 무수정 정책 변경 여부는 누락 경계의 재현 결과를 먼저 확보한 뒤 판단한다.
