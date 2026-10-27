# P0 MicroPython 무할당 표시·약한 출처 수명 시제품

후속 8차 [출처 저장 축소 기록](feature_p0_mp_compact.md)에 ID+scalar 9바이트/슬롯 후보,
실제 부분 고갈·원문 절단과 동적 문자열의 소유권 제약을 기록했다. 아래 24바이트 테이블은
비교/실패 재현용이며 compact 후보와 구분한다.

2026-10-27 후속 [함수 identity 보존 경계](feature_p0_mp_boundary.md)는 원본 alias/weak
정의와 strong 공개 wrapper로 아래의 생성자/비상 객체 문제를 호스트에서 통과했다.
아래는 단순 --wrap 시제품 당시의 실패·대안이며, 전체 메시지 번역/P0는 계속 미완료다.

2026-10-27, 기준 `8abf78e` 위의 기존 미커밋 감사와 이번 후속 실험.
**이번 시제품 조사·검증은 완료했다. 현재 `--wrap` 기반 생성자 추적은 불합격이며,
P0 전체와 vendor 무수정 전체 가능 여부의 판정은 계속 미완료다.**
커널에 번역을 적용하거나 MicroPython vendor·빌드 규칙을 바꾸지 않았다.

## 재현과 증거

```sh
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_probe64.py --only micropython
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_mp_audit64.py \
  --check docs/feature_p0_mp_catalog.json
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
```

기존 x86 개별 코어 오브젝트와 실제 VM·GC·예외 생성자를 사용한다. 호스트 I/O와
시험 소유 root만 fixture가 제공한다. mock allocator·임의의 큰 요청 실패를
진짜 메모리 고갈로 대체하지 않는다. 각 모드는 새 프로세스·VM으로 실행한다.

- `build64/feature-p0/report.json`: 기존 실험과 `noalloc_probe`의 분리된 결과.
- `mp-lifetime-{live,gc,reuse,lock,oom,identity}.log`: 신규 실제 코어 실험.
- `mp-lifetime-catalog.log`, `mp-lifetime-coverage.json`: 시제품의 실제 format 표와
  활성 문구 154개 대조. 다루는 format은 5개이며 **149개 pending ID**를 빠짐없이
  기록한다. 이 표가 모두 채워져도 생성 경계·인수·수명 검증 완료를 뜻하지 않는다.
- 기존 `mp-{run,boundaries,retention,overflow,oom}.log`: 앞선 rooted surrogate의
  회귀/실패 재현. 새 무할당 경로의 file/friendly/raw 통합 검증과 혼동하지 않는다.

최종 `check64`는 style 297개·host 108개·양 OS/앱/private nano archive·ELF를 통과했다.
기존 네 개 감사 테스트에 coverage 검사 세 개를 추가했다. 새 coverage 검사는
누락 ID·중복 제거·알 수 없는/빈 format 거부와 format 전체가 있어도 완료를 주장하지
않는 정책을 독립적인 입력으로 확인한다. 새 시제품 C는 수동 probe에서 strict
`-Werror` 경고 정책으로 컴파일된다. `check64`가 수동 실험을 대신하지 않는다.
빌드 규칙·생성 헤더·링커·커널 변경이 없어 이번 gate는 incremental이다.
QEMU IDE/AHCI와 Pi 5는 이번 작업에서 실행하지 않았다.

## 무할당 표시와 수명 실험

`mp_lifetime_probe64.c`를 기존 호스트 harness와 연결했다. 기존 모드는 그대로
surrogate 경로를 사용하며 `--lifetime-*`만 새 경로를 선택한다.

- 번역용 예외·tuple·문자열을 만들지 않는다. syntax/heap-lock 고정 문구,
  NameError의 qstr, MemoryError의 원래 size를 기록하여 출력한다. callable 타입
  이름은 살아 있는 원본 args 문자열의 범위를 빌린다. 고정 길이 복사로 자르지 않는다.
  명시적 `_args`의 EIO도 별도로 시험한다. 실제 `mp_raise_OSError` 전체 포착은 아니다.
- 2MiB 고정 GC heap의 각 32바이트 블록에 한 slot과 비상 객체 slot 하나를 둔다.
  주소별 상한이므로 살아 있는 예외를 64개 한도로 퇴출하지 않는다. 정적 테이블은
  **1,572,888바이트**다. 이는 호스트 실험의 capacity 증명이며 생산용 RAM 예산이 아니다.
- 테이블은 GC root가 아니다. 실제 collection 완료 직후 `gc_nbytes`로 사라진
  객체의 기록을 지우고, 성공한 외부 `gc_alloc`의 주소를 게시하기 전에 옛 기록을
  무효화한다. 현재 단일 VM·단일 heap·finaliser 비활성 설정의 실험이다.
  다른 GC 설정이나 `gc_realloc` 내부 호출 전체 추적을 보장하지 않는다.
- reporter는 출력 전에 출처 정보를 로컬로 복사한다. 출력 중 같은 reporter를
  별도 sink로 재진입하고 다음 출력의 언어를 영문으로 바꿔도 현재 문장은 유지된다.
  각 한국어 report에서 `gc_alloc` 호출 수·실제 `gc_info.used` 불변, args pointer
  불변과 정확한 독립 기대 문자열을 확인한다.

| 실험 | 독립 관찰 | 결과 |
| --- | --- | --- |
| 많은 live 오류 | 실제 SyntaxError 256개를 root로 유지, GC 후 실제 할당 크기와 정확한 한국어 재출력 | PASS |
| VM 오류·사용자 보존 | 실제 bytecode NameError/TypeError, Python `str/args` assert와 같은 객체 재던지기 | PASS |
| 사용자 같은 문구 | 실제 `raise SyntaxError('invalid syntax')`는 출처 없음, 원문 출력 유지 | PASS |
| 재진입·언어 고정 | 한국어 report 중 내부 report 전체를 출력하고 다음 언어를 변경, 바깥 문장은 그대로 | PASS |
| GC 회수·반복 | 시험 root 256개 해제 후 실제 GC used 감소, 출처 0개, 같은 VM에서 256개 재생성 | PASS |
| 주소 재사용 | 실제 `gc_free` 뒤 같은 주소의 `gc_alloc`, 게시 전 출처 없음 | PASS |
| heap lock | 실제 `gc_lock` 뒤 `m_malloc(1)` 실패, args 없는 비상 객체를 추가 할당 없이 한국어 표시 | PASS |
| 완전 고갈 | 모든 GC 블록을 유지해 `free=0`, 실제 `m_malloc(1)` 실패와 1바이트 요청 한국어 표시 | PASS |
| 비상 객체 사용자 재사용 | 실제 Python type slot의 `MemoryError()`가 같은 주소·type·args 재사용, 옛 한국어가 잘못 출력됨 | 설계 실패 재현 |
| 함수 주소 identity | 원본 type slot과 wrapped 외부 함수 주소가 다름, inline native check 조건 불일치 | 설계 실패 재현 |

GC 회수 시험은 시험 소유 root를 전부 놓은 뒤 레지스터/스택의 우연한 과거 주소를
root로 수집하지 않는 한 번의 isolated collection을 사용한다. VM의 기본 root는
`gc_collect_start()`가 그대로 수집한다. 다른 live 객체를 놓고 강제 회수하는 API가
아니며, 정상 live 시험은 기존 register/stack collector로 별도 검증한다.

새 reporter는 **메시지 본문 시제품**이다. traceback의 전체 구조·포트 reporter와
file/friendly/raw 연동은 앞선 시제품에서만 검증했다. 전체 활성 format 인수·qstr/
errno/동적 문구·subclass·명시적 `sys.print_exception` 정책은 아직 남아 있다.
생산용 `str(exc)`·사용자 출력 정책을 변경하지 않았다.

## 현재 링크 경계의 구체적 실패

`mp_obj_exception_make_new`까지 단순 `--wrap`하면 두 문제가 생긴다.

1. `objexcept.o`에 정의된 예외 type의 make_new 슬롯은 **원본 함수**를 가리킨다.
   사용자 `MemoryError()`도 이 슬롯을 호출한다. 외부 wrapper가 호출되지 않아 비상
   객체의 세대를 초기화하지 못한다. 실제 고갈/heap-lock 상태에서 같은 MemoryError
   타입·빈 args까지 일치한다. 원본 출력은 정확히 `MemoryError: \n`인데 시제품은
   이전 내장 한국어를 출력한다. 주소·args 외에 type을 비교해도 해결되지 않는다.
2. 다른 TU에 있는 `mp_obj_new_exception_arg1`의 inline native 검사도 함수 주소를
   비교한다. 해당 TU의 `mp_obj_exception_make_new` 참조만 wrapper로 바뀌므로 원본
   슬롯과 달라진다. 실제 production `mp_raise_type_arg`의 비교 실패 분기는 assertion
   halt다. 임의의 모든 생성자 래핑은 관측만 추가하는 투명한 변경이 아니다.

`--lifetime-identity`는 원본 슬롯/외부 참조의 불일치와, 양성 대조인 외부 직접 호출의
wrapper 진입 증가를 모두 관찰한다. 무한 assertion halt를 acceptance 실험으로
기다리지 않는다. `--lifetime-lock/oom`은 실제 슬롯을 호출하여 wrapper 카운터
불변, 동일 주소/type/args와 잘못된 한국어 출력까지 재현한다. Python 파싱/bytecode
전체를 메모리 고갈 후 실행했다고 주장하지 않는다.

도구는 그래서 두 **독립적인 호스트 실행 파일**을 링크한다. 기존 회귀용은 make_new를
래핑하지 않고, 신규 실험용만 래핑한다. 기존 파일/friendly/raw와 errno 경로가
잘못된 native 검사에 들어가지 않게 하며, 실패를 숨기고 전체 통과로 표시하지 않는다.
`noalloc_probe.status`는 constructor coverage FAIL을 명시한다.

## 다음 P0 작업: identity를 보존하는 빌드 경계 검증

이번 결과는 **현재 GNU ld --wrap 방식의 불충분성**이다. 가능한 모든 vendor 무수정
방법의 불가능성 증명은 아니다. 따라서 vendor 정책 변경을 요청하거나 적용하지 않는다.
다음은 아래 대안을 별도 호스트 산출물에서 비교·검증하는 것이다.

- 원본 hash를 검증하고 **오브젝트 작업 복사본**에 원본 생성자 alias와 weak definition을
  두고, 원래 공개 이름에 강한 포트 wrapper를 제공하는 방안을 조사한다. type 슬롯과
  내부/외부 주소 비교가 모두 같은 공개 이름을 보게 해야 한다. compiler가 같은 TU의
  호출을 inline/직접 결합하지 않는 설정, relocation·symbol 검사와 라이선스/재현 기록이
  필요하다. 현재 오브젝트나 OS 링크 입력은 바꾸지 않는다.
- 실제 default core로 `mp_raise_OSError`·filename·recursion·사용자/subclass·모든 생성자
  경로와 비상 객체 재사용을 검증하고, constructor 실행마다 출처 초기화/게시가 관찰되는지
  확인한다. same-TU 호출·타입 포인터 비교가 하나라도 어긋나면 통과로 판정하지 않는다.
- 메모리 예산·가변 format 인수의 수명·전체 154개/비매크로 경계와 GC/reentry도 이어서
  검증한다. 현재 1.5MiB 테이블은 그대로 채택하지 않는다.
- 빌드 경계 대안도 충분하지 않다는 재현이 남을 때만 작업 복사본의 최소 생성/수명 hook
  대안을 구체화해 사용자와 vendor 정책 변경을 결정한다. 후보 hook은 make_new에서
  새/재사용 객체 identity 초기화, builtin 메시지 ID·typed 인수 게시, GC 회수 통지다.
  Python 생성과 내부 생성의 구분, NLR·재진입과 emergency 세대도 계약에 포함해야 한다.

커널·CRT·앱·nano·머꼬의 나머지 개별 출력 catalog와 Pi 5 검증도 남는다.
P0 전체 또는 P1 완료로 이동하지 않는다.
