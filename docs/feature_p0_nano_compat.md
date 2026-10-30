# P0 nano 파일 스트림·UTF 기반 계층

후속: [파일 경계 연결](feature_p0_nano_storage.md)에서 checked read/metadata/sync와
실제 upstream 열기·저장을 연결했다. 아래 legacy EOF 제한은 최초 기반 계층의 이력이다.

후속 3차: [전체 nano ELF 링크](feature_p0_nano_link.md)를 완료했다. 아래는 기반 계층의
2차 완료 당시 기록이며, 일반 libc/POSIX/curses 연결 진행은 후속 문서를 따른다.

2026-10-26, 기준 `b1ecb537f321f79e4db9899fc1aa5f2ad47e0b2a` 위의 미커밋 작업.
**이번 하위 작업은 완료했다. P0 전체와 업스트림 nano 앱 이식은 미완료다.**
기존 2026-10-25의 미커밋 P0 시제품·vendor 및 `.gitignore` 변경을 보존했다.

## 구현 범위

`64bit/nano64/`는 앱 자동 탐색 대상 밖의 전용 포트 디렉터리다. 현재 앱의 이름,
실행 파일, syscall ABI와 CRT 구현을 변경하지 않았다. 원본 nano/gnulib를 이 archive에
넣지 않았으며 hosted `FILE`, libc, locale, curses 라이브러리도 링크하지 않는다.
일반 libc 헤더에서 upstream 호출을 연결하는 adapter는 다음 작업이다.

| 구현 | 계약 |
| --- | --- |
| `nano_fopen64` / `nano_fdopen64` | `r/rb/w/wb`, stream 확보 후 열기, fd 중복 소유 거부 |
| `nano_fread64` / `nano_getc64` | 512바이트 read cache, 짧은 읽기 재시도, unsigned byte·EOF 구분 |
| `nano_fwrite64` / `nano_putc64` | 양수 부분 쓰기 재시도, 0/음수/과도한 count 오류, 완료 element 수 반환 |
| `nano_ferror64` / `nano_feof64` / `nano_clearerr64` | 독립 EOF와 sticky 오류, 오류 해제 전 추가 I/O 차단 |
| `nano_fclose64` | close 실패 시 소유권 유지, 성공 시 반환, 기존 I/O 오류 보존 |
| `nano_fflush64` / `nano_fileno64` | 비버퍼 출력의 오류 확인, descriptor 조회; 내구성 보장 없음 |
| `nano_mbtowc64` / `nano_wctomb64` | 공용 bounded UTF 재사용, scalar 보존, 잘린 입력·surrogate·초과값 거부 |
| `nano_wcwidth64` | 현재 콘솔과 같은 한글 두 칸/기타 fallback 한 칸; 제어·잘못된 scalar 거부 |

8개 고정 stream slot을 사용하며 힙 할당은 없다. OS가 허용하는 실제 열린 파일 수는
별도 한계다. 단일 앱 실행 문맥을 전제로 하며, descriptor를 넘긴 뒤에는 stream이
독점한다. pool 주소를 재사용하므로 닫은 포인터의 stale 사용은 검출할 수 없다.
요청의 곱셈·signed syscall count·포인터 endpoint는 버퍼 접근 전에 검사한다.

오류는 private `nano_errno64`에 양수로 저장한다. 현재 `SYS_OPEN`의 -1/-2/-3 등은
표준 errno와 일치하지 않으므로 syscall의 음수 결과는 EIO로 변환한다. ENOENT나
ENOSPC로 추측하지 않는다. 로컬 검증만 EINVAL/EBADF/EOVERFLOW 등을 구분한다.
이 모듈의 EOF/element-count 형식은 향후 libc adapter용이며 CRT의 signed 결과는 유지한다.

## 다음 연결 전에 해결할 계약

- append/update/exclusive 모드, 표준 입출력 stream, printf/formatter, stat/access,
  metadata, fsync, 잠금, 임시 파일, rename, signal/process, regex, NLS는 미구현이다.
  미지원 모드 요청은 ENOSYS이며 파일을 열거나 지우지 않는다. 성공 stub은 없다.
- `fflush`는 앱에 보류된 출력이 없다는 뜻이다. upstream nano `files.c`의
  `fflush → fsync → fclose`를 만족했다고 주장하지 않는다. 현재 `SYS_CLOSE`도 cache를
  sync하지 않는다. 실제 저장의 내구성 계약은 별도 설계/검증이 필요하다.
- 현재 `fd64_read`가 storage 오류를 0 또는 짧은 count로 전달할 수 있어, private
  stream만으로 EOF와 모든 디스크 실패를 구별할 수 없다. 실제 nano의 성공 판정에
  연결하기 전에 메타데이터 기반 exact read나 명시적 오류 경로를 정해야 한다.
- 64KiB 제한, 저장 길이 사전 검사와 truncate 순서, 마지막 개행/CRLF 보존 정책은
  편집기 adapter의 책임으로 남는다. stream에 상한을 두고 이미 truncate한 뒤 실패하는
  방식으로 대체하지 않는다. 일반 in-place 쓰기는 부분 실패 후 prefix를 남길 수 있다.
- wchar ABI와 Unicode 문자 분류는 미구현이다. private API는 `uint32_t` scalar를
  사용하고 향후 `wchar_t` 변환은 경계에서 수행한다. 결합 문자/CJK/emoji 전체 폭을
  Unicode locale 기준으로 계산하지 않으며, 지금은 OS fallback 폭에 맞춘다.
- PREEDIT·undo 분리·렌더링·검색·파일명 조합·TTY 입력은 이번 단위 검증 범위 밖이다.

## 빌드·검증

공용 inventory는 `64bit/mk/common64.mk`의 `NANO64_COMPAT_SRCS`, 규칙은
`64bit/mk/nano64.mk`에 있다. 두 플랫폼 모두 동일 소스와 공용 UTF를 빌드한다.
각 private header는 `headers64.c`에서 직접 포함해 strict `-Werror` 정책으로
감사한다. `style64`는 새 디렉터리도 검사한다.

```sh
make nano64-compat
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s 64bit/tests -p test_nano_compat64.py -v
make clean64
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
```

2026-10-26 결과:

- clean64/check64 PASS: style 256개 파일, 호스트 98개 테스트, 기존 x86_64 이미지와
  AArch64 커널·앱·staging, 새 compatibility archive 두 개.
- 새 호스트 runner는 실제 CRT `syscall.c`를 사용하고 native gate만 대체한다.
  부분 쓰기의 모든 64개 분할, binary/UTF/NUL/0xff/CRLF 읽기, cache 경계,
  부분 element와 실패 후 정확한 prefix, EOF/오류 복구, stream 고갈 시 truncate
  미호출, 중복 fd, close 실패·재시도, 산술/주소 overflow를 ASan·UBSan으로 검증했다.
- UTF 각 길이의 잘린 버퍼를 ASan redzone에 붙여 검사했다. 정상 바이트 기대값은
  상수이며 동일 encoder로 만들지 않았다. LSan은 기존 host runner와 같이 끈다.
- 두 archive를 각각 `ld -r --whole-archive`로 합쳐 `nm -u` 확인:
  남은 외부 심볼은 `close, memcpy, memset, open, read, write`뿐이며 모두 기존 CRT다.
  `headers64.o`는 양 아키텍처 모두 runtime 심볼을 정의하지 않는다.
- **새 nano ELF 링크, QEMU IDE/AHCI, Pi 5 실행은 하지 않았다.**

증거는 `/tmp/mowkow-p0-file-utf64/`의 `clean64.log`, `check64.log`,
`x86_64-symbols.log`, `aarch64-symbols.log`, `report.json`에 보존했다. report에는
source commit·미커밋 상태·소스/archive/image SHA-256·미실행 runtime 항목을 기록했다.
clean64 전에 이전 호스트 시제품의 JSON/log를 같은 디렉터리의
`previous-host-evidence/`로 복사했다. 생성 결과는 커밋하지 않는다.

다음 작업은 **실제 upstream nano가 include하는 libc/POSIX adapter와 기능 제외
경계, 제한된 curses를 연결해 양 아키텍처 전체 ELF 링크를 통과시키는 것**이다.
그 뒤 일회용 IDE/AHCI 이미지에서 실제 TTY 한글 조합·저장·종료를 검증한다.
