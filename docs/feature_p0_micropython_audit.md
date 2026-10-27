# P0 MicroPython 활성 오류·출처 수명 감사

후속 7차 [함수 identity 보존 경계](feature_p0_mp_boundary.md)에서는 vendor 무수정
오브젝트 alias 대안으로 생성자·emergency 재사용을 호스트에서 통과했다. 현재 생성 경계의
가능 후보이며 전체 문구/production 출처 메모리·REPL 검증은 계속 남아 있다.

2026-10-27 후속 [무할당 표시·약한 출처 수명 시제품](feature_p0_mp_lifetime.md)에서
256개 GC 수명·실제 free=0 본문 표시는 검증했다. 사용자 type slot의 wrapper 우회와
함수 주소 native 검사 불일치가 남아, 다음은 identity를 보존하는 빌드 경계 대안이다.
아래는 2026-10-26 감사 당시의 결과·다음 조건이며 P0 전체는 계속 미완료다.

2026-10-26, 기준 `8abf78e` 위의 미커밋 조사 도구 변경.
**이번 하위 작업은 완료했다. 기존 번역 시제품은 전체 요구를 충족하지 못하며
P0 전체는 미완료다.** 커널 번역 기능을 활성화하거나 vendor 소스를 수정하지 않았다.

## 재현과 산출물

```sh
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_probe64.py --only micropython
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_mp_audit64.py \
  --check docs/feature_p0_mp_catalog.json
```

호스트 C compiler, 양 cross-toolchain과 기존 빌드 환경을 사용한다. 조사 도구는
네트워크를 사용하지 않는다. 결과는 무시되는 `build64/feature-p0/`에 남는다.
`report.json`, `mp-{run,boundaries,retention,overflow,oom}.log`와
`mp-audit/catalog.json`을 확인한다. `mp-overflow.log`의 assert는 의도한 음성
검증이다. 도구는 **정확히 65번째 등록의 SIGABRT와 해당 assert 문구**를 요구하고
이 child의 core dump를 끈다. 다른 실패를 성공으로 처리하지 않는다.

이번 `check64`는 style 295개·host 105개·양 OS/앱·private nano ELF를 통과했다.
빌드 규칙·생성 헤더·링커·커널 동작은 변경하지 않아 incremental gate를 사용했다.
QEMU IDE/AHCI와 Pi 5는 이번 작업에서 실행하지 않았다. 기존 nano 검증 결과는
[저장 경계 기록](feature_p0_nano_storage.md)에 있으며 이번 결과와 구분한다.

## 개별 활성 문구 목록

[JSON catalog](feature_p0_mp_catalog.json)는 일반 문자열 검색 결과가 아니다.
Make의 실제 공용 source/object inventory와 양 아키텍처의 vendor/port compiler
flags를 읽고, `py/misc.h`를 먼저 include한 조사 TU에서 `MP_ERROR_TEXT`만 표시
marker로 바꿔 **전처리**한다. 원본 파일이나 빌드 오브젝트는 바꾸지 않는다.
활성 조건부 분기와 `SIZE_FMT` 등의 인접 문자열 확장을 반영하고 원래 파일·행을
보존한다. 알 수 없는 비문자열 표현은 누락시키지 않고 실패한다.

- 두 아키텍처에서 각각 **154개 고유 문구**, 합계 **376개 문구/위치/아키텍처
  조합**을 확인했다. 같은 문구의 여러 위치를 합치되 위치 정보는 보존한다.
- 실제 전처리 대상은 x86_64 142개, AArch64 141개 TU다. 비활성 기능의 빈 TU도
  inventory에 포함한다. AArch64에서는 x86 전용 `libc/math.c`를 제외한다.
- 각 항목은 안정적인 문구 ID, 원문 format, 내장 오류 분류, 인수 보존·표시 전용
  번역 정책, `translation=pending`, 경로·행·아키텍처를 갖는다.
- 양쪽 실제 개별 오브젝트에서 예외 관련 import/definition을 읽는다. 각각 41개
  오브젝트가 해당 심볼을 갖는다. 이는 호출 횟수나 실행 coverage가 아니다.
- `--check`는 재생성 결과가 저장된 catalog와 다르면 실패한다. 설정·원본·도구를
  변경한 후 검토하며 갱신한다. 전체 도구 실행은 `check64`의 자동 단계가 아니다.

**경계:** 이 목록은 전처리상 활성인 `MP_ERROR_TEXT` 전체다. 최적화로 제거되는
코드·문구 테이블도 포함하며, 모든 항목의 실제 도달 가능성을 주장하지 않는다.
`MP_ERROR_TEXT`를 쓰지 않는 qstr/errno/동적 문구·타입만 있는 예외·일반 안내는
아래 수동 경계 표와 [전체 출력 inventory](feature_p0_outputs.md)로 따로 추적한다.
따라서 이 154개를 OS 전체 출력 catalog의 완료 수로 사용하면 안 된다.

## 생성·표시 경계 감사

| 경계 | 실제 활성 경로 | 기존 시제품 상태/필요 작업 |
| --- | --- | --- |
| `mp_obj_new_exception_msg` | parse 오류, VM/runtime 등의 고정 문구 | wrapper 진입 가능. catalog의 일부만 번역 |
| `_msg_varg` / `_msg_vlist` | NameError·TypeError·수량/타입 format | 외부 호출은 포착. 내부 같은 TU 호출과 final link 순서 구분 필요 |
| `_args` | 명시적 인수 배열 생성자 | EIO 한 항목만 처리. 모든 내장/사용자 경로의 구분은 아님 |
| `mp_raise_msg[_varg]` | runtime와 대부분 py 코어 | runtime 안의 직접 호출은 raise wrapper만으로 포착할 수 없음 |
| `mp_obj_new_exception_arg1` | `mp_raise_type_arg`, KeyError 등 | inline이 직접 `mp_obj_exception_make_new`를 호출; `_args` wrapper 우회 |
| `mp_raise_OSError` | port 파일 읽기 EIO, import 등 | 별도 상위 wrapper의 EIO만 등록 |
| `mp_raise_OSError_with_filename` | 활성 runtime 함수, errno+`can't open %s` | 실제 호출 시 미등록. 파일명은 원본 인수로 유지해야 함 |
| `mp_raise_recursion_depth` | stackctrl/cstack 및 깊은 호출 | qstr `maximum recursion depth exceeded`; 실제 호출 시 미등록 |
| `mp_obj_new_exception` / 타입만 있는 raise | 제어 흐름·타입만 있는 예외 | 설명문 유무 구분. 타입 식별자는 번역하지 않음 |
| `mp_obj_exception_make_new` | 위 내부 생성자 + Python `Exception(...)` | 같은 함수가 사용자 인수도 받음. 모두 내장 오류로 등록하면 오역 |
| `mp_make_raise_obj`, 사용자 subclass | Python `raise`, 다시 던지기 | 사용자 객체·args·`str(exc)`와 평가 결과 유지 필요 |
| errno printer | `objexcept.c` → `mp_errno_to_str` | 현재 OS는 errno 모듈 비활성. 활성화하면 errno 이름/설명 별도 정책 필요 |
| `mp_obj_print_exception` | port reporter, pyexec 파일/friendly/raw | 기존 3경로 호스트 실험은 유지. traceback 고정 문구만 분리 가능 |
| 직접 `mp_obj_print_helper` | 사용자 `print`, `str`, `repr` | 일반 출력 번역 금지 |
| 사용자 `sys.print_exception` | `modsys.c` → `mp_obj_print_exception` | top-level reporter와 같은 경계. P5에서 명시적 사용자 출력의 정책/문맥도 구분 필요 |

`mp_raise_OSError_with_filename`은 현재 포트의 파일 loader가 사용하는 경로는
아니다. 다만 설정상 빌드된 runtime 함수이며 host에서 **실제로 호출하여** 기존
시제품의 빈틈을 검증했다. 이를 현재 OS 사용자 명령에서 도달한 결과로 표시하지 않는다.

현재 AArch64 최종 kernel은 **개별 `A64_MPY_LINK_OBJS`를 직접 링크**한다.
`micropython-foundation.o`는 별도 진단 산출물이다. 그 `ld -r` 결과가 현재 kernel
입력이라고 해석하지 않는다. 향후 합친 오브젝트를 실제 입력으로 바꾸면 내부 심볼이
먼저 해결되어 `--wrap` 경계가 줄어들 수 있으므로 다시 검증해야 한다.

## 수명·포화·실제 OOM 결과

기존 2MiB 실제 MicroPython GC heap과 production x86 코어 오브젝트를 쓴다.
VM·allocator·예외 생성자를 mock하지 않는다. 보조 I/O와 채움 블록의 GC root만
호스트 fixture가 제공한다. 각각 새 프로세스/VM에서 실행한다.

| 실험 | 독립 관찰/기대값 | 결과 |
| --- | --- | --- |
| baseline | 실제 file/friendly/raw, 다섯 예외, 사용자 문자열·객체·args 유지 | 기존 호스트 PASS 유지 |
| 생성 경계 | explicit `_args`와 wrapped EIO는 등록, 파일명 EIO·재귀 깊이는 미등록 | 누락 재현 PASS |
| 수명 | 사용자 root 없는 64개 원본과 64개 surrogate가 GC 후에도 `gc_nbytes != 0` | 무기한 보존 재현 PASS |
| 포화 | 위 64개 후 65번째 생성 | 지정 assert/SIGABRT 재현 PASS |
| 진짜 OOM | `gc_alloc(1)`로 모든 블록을 채우고 유지, `gc_info.free == 0` 후 `m_malloc(1)` | MemoryError 실제 발생 PASS |
| OOM surrogate | 원본·번역본 주소가 모두 VM의 `mp_emergency_exception_obj` | 별도 객체라는 전제 파괴 재현 PASS |
| OOM 표시 | args 길이 0, 실제 출력 정확히 `MemoryError: \n` | 한국어 설명 손실 재현 PASS |
| 비상 객체 재사용 | 같은 heap에서 사용자 `OSError(5)`와 같은 `make_new` 호출 | 주소·args가 이전 등록과 일치, 타입은 OSError로 변경 PASS |

현재 설정의 `MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF`는 **0**이다. 비상 객체는
항상 존재하지만 추가 인수 저장 버퍼가 없어 OOM에서 빈 tuple로 떨어진다. 크기가
큰 요청 실패와 실제 빈 공간 0은 다르다. 기존 3MiB 요청 실패 성공으로 이번 문제를
해결했다고 판단할 수 없다. 비상 버퍼를 켜더라도 같은 메모리 재사용·인수 절단·수명
문제는 별도 검증해야 하며, 설정 변경만으로 출처 수명이 해결된다고 가정하지 않는다.

재사용 실험은 Python 생성자와 동일한 실제 C 함수에 같은 인수를 전달한다.
메모리 고갈 뒤 Python 소스 파싱/컴파일을 추가 실행한 것이 아니다. 원본 args와 주소만
비교하는 현재 레지스트리로는 이 사용자 생성과 오래된 내장 등록을 구분할 수 없다.
이 결과는 잘못된 한국어 문장이 실제 사용자에게 출력됐다는 주장이 아니라,
**출처 판별 조건 자체가 충돌한다는 재현**이다.

## 판정과 다음 작업

**현재 wrapper + 64개 rooted surrogate 방식은 채택할 수 없다.** 전체 문구 누락,
장기 실행 포화와 추가 할당, 비상 객체 재사용을 P5 영어 fallback으로 덮지 않는다.
반면 이 실험만으로 모든 vendor 무수정 해법이 불가능하다고 결론 내릴 수도 없다.
이번 작업에서는 vendor 정책이나 확정 요구를 바꾸지 않았다.

다음 P0 하위 작업은 **추가 할당 없는 표시와 GC/생성자에 연동한 출처 수명 시제품**이다.
다음 조건을 통과해야 vendor 무수정 가능 여부를 최종 판정할 수 있다.

1. 메시지 ID·원래 format 인수의 소유권을 보존하며 원본 예외·args와 사용자 `str`을
   바꾸지 않는다. 진짜 OOM에서 surrogate를 만들지 않고 출력할 수 있어야 한다.
2. 사용자 생성·내부 생성·재던지기를 구분하고, 모든 생성자에서 비상 객체의 세대를
   갱신한다. 정상 GC 회수/주소 재사용을 추적하며 살아 있는 예외를 무단 퇴출하지 않는다.
3. 한 VM에서 64회를 넘는 반복 오류, 많은 살아 있는 예외, GC/재진입/힙 잠금/완전 고갈,
   비상 객체 재사용, 한국어/영문 재출력과 전체 활성 메시지의 누락 검출을 확인한다.
4. build/port 경계만으로 충분하지 않다는 재현이 남으면, 원본 hash 검증 후 작업 복사본에
   최소 생성/수명 hook 패치를 적용하는 대안을 구체화한다. 이는 현 vendor 무수정 계약의
   변경이므로 그때 사용자와 정책을 결정한다. 지금은 해당 변경을 적용하지 않는다.

OS 소유 콘솔의 언어 조회는 P2, 실제 번역 적용은 P5다. 커널·앱·nano·머꼬의 나머지
개별 출력 확장과 Pi 5 실기도 남아 있다. 이번 조사만으로 P0를 완료하거나 P1로 넘어가지 않는다.
