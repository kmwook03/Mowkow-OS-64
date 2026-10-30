# P0 MicroPython producer 타입과 실제 formatter·args 대조

2026-10-28, `e18d682` 위의 미커밋 시제품. **14개 ABI 불일치 ID에 대한 호스트 후보**다.
생산 kernel/vendor/ABI는 변경하지 않았고, 149개 pending 번역도 그대로 유지한다.

## 경계와 정책

기존 격리 strong-symbol 실험에 별도 ABI 실행 파일을 추가했다. 최신 typed manifest의
14개 ID에서 producer signature를 생성하고 실제 `mp_obj_new_exception_msg_vlist` 진입 시
`va_copy`로 인수를 확보한다. 같은 format의 producer signature가 여러 개이거나 미검토
conversion·소유권·인수 개수·문구가 나타나면 생성 검사를 실패시킨다.

원본 va_list는 변경하지 않고 원래 생성자에 한 번 전달한다. 반환한 원본 예외의 단일
문자열 args를 고정 기대 바이트와 비교하고, 생성 전/후 normalized 표시가 같은지도 검사한다.
후자의 재표시 뒤에도 tuple/item identity와 전체 bytes/length가 같아야 한다.
이 시제품은 영어 포맷의 인수 의미를 대조하며 한국어 번역을 추가하지 않는다.

| producer → consumer | capture/표시 정책 |
| --- | --- |
| int32 → `%q` | 실제 전달 타입 int를 읽고 qstr로 확장, 현재 pool 범위 검사 후 표시 |
| qstr/size_t64 → `%q` | unsigned long 전체 보존, 같은 pool 범위 검사 |
| type-name `const char *` → `%s` | 원본 pointer 보존, 강제 intern 없이 기존 qstr의 동일 pointer 확인 후 사용 |
| size_t64/ptrdiff_t64 → `%d` | 원래 unsigned long/long 전체 보존; INT_MIN..INT_MAX만 int로 정규화 |
| int32 → `%x` | signed 값을 보존하고 표시 시 unsigned int로 변환하여 원래 bit pattern 유지 |

`MP_QSTRnull=0`은 현재 special-method table의 실제 빈 연산명 sentinel이다.
잘못된 이름으로 배제하거나 새로운 연산명으로 보정하지 않는다. `~None`은 현재 설정에서
`unsupported type for : 'NoneType'`이며 이를 그대로 대조한다.
`f(a,b=1); f(b=2)`의 누락 인수 진단도 원래 `#0`을 보존한다.

범위 초과 숫자는 snapshot에 원래 64비트 그대로 남기고 **새 표시의 첫 sink 전에 false**다.
원래 formatter의 하위 32비트 결과를 무손실 값으로 오인하지 않으며 원래 args도 고치지 않는다.
생산 환경에서는 이 실패를 incomplete로 집계해야 한다. fallback은 번역 성공이 아니다.
C varargs 타입 불일치는 vendor에 여전히 존재하며, 이번 호스트 관찰은 이 코드를 C 표준상
안전하게 만들거나 AArch64에서도 같은 관찰을 보장하는 증명이 아니다.

## 독립 기대값과 실행 범위

14개 ID의 실제 실행 경로는 다음과 같다. `function takes ...`는 argcheck·bytecode·namedtuple의
세 producer를 각각 실행하므로 총 16개 경로다. 모든 분기 조합을 전수 실행한 것은 아니다.

| 경로 | ID 끝 8자리 |
| --- | --- |
| `[0][2]`, `[0][None]` | c74ddf10, 4ea84756 |
| 실제 mp_arg_parse_all의 required name | cce22d54 |
| `b''.find('x')`, `type(None)()` | b9a30f80, 5d1948b5 |
| `range(1,2,3,4)`, `range()` | 97cc996f, 012e71d2 |
| `f(a,b=1); f(b=2)` | 4001c195 |
| `g(a); g()`, mp_arg_check_num, namedtuple T() | b000e6f6 |
| `class X(type(iter([])))`, `int.missing` | e9175dc5, dafc82ae |
| `'%Z' % 1`, `~None`, `1+None` | 5439615e, 93e911c0, 85e3c899 |

숫자 경계 6개는 실제 원래 생성자/formatter를 실행하고 args도 확인한다.

| producer 값 | 원래 `%d` 출력 값 | 새 표시 |
| --- | ---: | --- |
| unsigned 2147483647 | 2147483647 | 허용 |
| unsigned 2147483648 | -2147483648 | 거부, 전체 값 보존 |
| unsigned 4294967297 | 1 | 거부, 전체 값 보존 |
| ULONG_MAX | -1 | 거부, 전체 값 보존 |
| signed -2147483648 | -2147483648 | 허용 |
| signed -2147483649 | 2147483647 | 거부, 전체 값 보존 |

추가 `%c/%x/%d` 조합에서 int 127과 -1의 원본 byte/hex를 비교한다.
음수 -1은 byte FF와 `ffffffff`다. 별도로 음수 qstr와 기존 타입명 bytes를 복사한
비소유 stack 문자열은 실제 capture에서 거부하고 sink 출력이 0인지 확인한다.
총 거부 6개는 숫자 4개와 잘못된 qstr/비소유 문자열 2개다.

capture/normalized renderer는 heap lock과 gc_alloc wrapper guard 안에서 실행한다.
실제 allocation 시도가 있으면 assertion이 실패한다. **할당 0은 이 두 구간의 결과**이며,
원래 예외 생성·Python 코드 실행까지 무할당이라는 뜻은 아니다.
예외/args가 살아 있는 정상 heap 실험이다. 이 새 snapshot의 고갈·GC/NLR·재진입 수명은
검증하지 않았으며 기존 sidecar/reporter 실험을 대신하지 않는다.

## 재현과 남은 작업

```sh
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_probe64.py --only micropython
```

`build64/feature-p0/mp-boundary/abi/`에 signature include, link/run 로그, report.json을 남긴다.
상위 probe는 14개 ID가 정확히 한 번씩 보고되는지와 최종 exact marker를 검사한다.
새 host 3개는 producer 기반 signature, 혼합 producer/미지원 타입·소유권·format 변경,
누락/중복 ID와 초과 arity의 거부를 검사한다.

값 3개와 qstr 정규화 값은 이 fixture의 stack snapshot이다. 모든 live 예외에 대한 영속
예산·무퇴출·회수 증명이 아니며, 출력 buffer도 시험용 512바이트다. 양 cross compiler의
producer 타입 감사는 계속 실행하지만 AArch64 formatter/GC 실행·QEMU IDE/AHCI·Pi 5는
이번 범위에서 수행하지 않았다. ASan/UBSan 결과도 probe 소스의 계측이며 기존 core
객체 전체를 sanitizer로 다시 빌드한 결과는 아니다.
실행 옵션은 `ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0`이다.
기본 fake-stack 설정에서는 첫 producer 도달 전 중단되어 동일 시험을 이 옵션으로 재실행했다.

10개 조합의 checked encoding·고갈·회수 후보는 [15차 실험](feature_p0_mp_packing.md)에
기록한다. 다음은 압축 범위 밖 입력의 실제 도달 가능성 판정과 공통 reporter 통합이다.
전체 메시지·비매크로/REPL/traceback·closure 비용·생산 적용·출력 catalog·실기 검증은 남는다.
P0 전체 미완료이며 P1로 이동하지 않는다.
