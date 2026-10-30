# P0 원시 payload64 초과 10개 조합의 검사된 인코딩

이 문서는 15차 결과다. 공통 저장소·한국어 reporter 연결과 범위 초과의 실제 반례는
[16차 결과](feature_p0_mp_pack_reporter.md)에 기록하며 아래의 당시 미완료 범위를 갱신한다.

2026-10-28, `e18d682` 위의 미커밋 시제품. **검사된 범위 안의 무손실 encoding 후보**다.
전체 producer 범위의 수용·한국어 번역·생산 적용 완료는 아니다. 기존 149개 pending과
raw typed manifest를 유지하며, 이 성공을 전체 P0 완료로 세지 않는다.

## 범위와 메모리

기존 슬롯은 ID 1바이트와 payload 8바이트다. `mp_origin_pack64.c`는 schema에 따라 모든
인수를 검사한 뒤 한 단어를 게시한다. 실패는 -22(잘못된 schema/인수), -7(64비트 초과),
-34(표현 범위/반복 불일치)이며 출력값을 변경하지 않는다. 디코더도 모든 필드와 상위
미사용 비트를 검사한 뒤 결과 전체를 게시한다. 부분 결과는 노출하지 않는다.

| 필드 | 의미와 허용 범위 |
| --- | --- |
| Q16 | 기존 qstr ID 0..65535; type->name은 양 strict port recipe에서 sizeof=2 확인 |
| Q32 | 일반 qstr ID 0..UINT32_MAX; 실제 VM에서는 먼저 현재 pool 범위도 검사 |
| I32 | INT_MIN..INT_MAX의 sign-extended uint64 표현; 음수도 정확히 복원 |
| POS32 | 0..INT_MAX의 원래 unsigned producer 값; 큰 size_t를 자르지 않음 |
| REPEAT | 직전 인수와 전체 값이 같아야 함; 비트 없이 그 값을 복원 |

실제 `%s`는 감사된 mp_obj_get_type_str의 기존 qstr pointer다. 문자열 내용을 강제 intern하지
않고 현재 pool에서 찾은 pointer와 동일한지도 확인한다. `%q`의 0은 빈 연산명 sentinel로
보존한다. Q16/Q32 저장은 유효 범위를 확인한 다음 이루어지며 qstr 객체를 새로 만들지 않는다.

| 메시지 요약 | 필드 | 비트 |
| --- | --- | ---: |
| `%q indices ... %s` | Q16, Q16 | 32 |
| `'%s' object has no attribute '%q'` | Q16, Q32 | 48 |
| `can't convert '%q' ... %q implicitly` | Q16, Q16 | 32 |
| `function expected at most %d ..., got %d` | POS32, POS32 | 64 |
| `function takes %d ... but %d were given` | POS32, POS32 | 64 |
| `type object '%q' has no attribute '%q'` | Q16, Q32 | 48 |
| `unknown format code '%c' ... '%s'` | I32, Q16 | 48 |
| `unsupported format character '%c' (0x%x) at index %d` | I32, REPEAT, I32 | 64 |
| `unsupported type for %q: '%s'` | Q16, Q16 | 32 |
| `unsupported types for %q: '%s', '%s'` | Q16, Q16, Q16 | 48 |

`feature_mp_pack64.py`는 최신 typed inventory의 정확한 10개 문구/arity/소유권과 대조한다.
반복 c/x는 producer 표현식도 같아야 하며 runtime에서도 값의 동등성을 다시 검사한다.
별도 생성 include에 이 계획과 실험용 tag 1..10을 기록한다. 이 tag는 생산 catalog ID나
기존 MESSAGE64와 통합된 번호가 아니다. 공통 ID namespace 연결은 reporter 통합에 남는다.

기존 `MP_ORIGIN_STORE64`를 그대로 사용한다. 모든 65,536개 heap-block 슬롯과 emergency에
전용 공간이 있고 공유 pool/동적 할당/퇴출이 없다. 2MiB 후보의 origin metadata는
**589,889바이트**로 변하지 않는다. 이전 reporter의 72바이트를 더하면 589,961바이트다.
새 schema/catalog는 읽기 전용이며, 테스트 root·fill 배열과 계측/ABI snapshot은 이 계산에서
제외한다. 하나의 실행 파일에 함께 연결된 다른 실험들의 전체 BSS 크기를 뜻하지 않는다.

## 실제 생성·GC·고갈

기존 identity-preserving strong-symbol 경계의 별도 `mp-pack-probe64`에서 vlist 인수를
원래 producer 타입으로 확보한다. 첫 sink/원래 생성자 전에 인코딩을 검사하고 성공한
로컬 pending 단어를 원래 생성자 반환 뒤 저장한다. 원본 va_list·예외·args는 교체하지 않는다.
기존 영어 formatter 결과와 고정 기대 바이트, 원본 args/identity, 복원된 인수를 대조한다.

`--pack-live`는 실제 Python 오류 10개로 모든 ID를 실행한다. 새 두 경로는 `[0].missing`과
`'{:s}'.format(1)`이다. 이후 열 가지 typed 생성 결과를 실제 GC에서 유지하고, 고정 기대
인수와 비교한다. 추가로 서로 다른 두 숫자의 예외 256개를 유지한 뒤 fixture root를 지워
owner allocation이 실제로 0이 되고 모든 메타데이터가 지워지는지 확인한다.
GC 격리 구간은 C stack/register를 제외하므로 우연한 stack root를 성공으로 세지 않는다.
출처 테이블은 root로 스캔하지 않는다. qstr은 기존 VM pool의 수명을 사용하므로 이 후보의
payload에는 detached 객체 edge가 없으며 별도 mark closure를 추가하지 않는다.

실제 gc_free 후 동일 크기 gc_alloc이 같은 주소를 반환해야 하며 옛 ID/payload는 0이어야 한다.
fixture는 allocation 반환·명시적 free·성공한 make_new 및 GC 후 sweep에서 세대를 정리한다.
새 gc_free 검증은 명시적 wrapper 경로다. vendor gc.c 내부 재할당의 모든 same-TU free
경로까지 감사한 결과는 아니며 생산 경계 완전성 판정은 남는다.

`--pack-oom`은 모든 빈 block을 실제로 채워 gc_info.free=0을 요구한다. heap lock 상태에서
열 가지 typed 원래 생성자는 모두 emergency 객체와 args=0을 반환한다. 원래 생성 입력에서
확보한 숫자/qstr을 전체 저장소에서 복원하여 고정 기대값과 비교한다. 같은 주소를 사용자
TypeError로 재사용하면 옛 출처가 지워져야 한다. 이후 범위를 넘는 ULONG_MAX 입력도
이전 emergency 세대의 값을 남기지 않아야 한다. 거부를 번역 성공으로 세지 않는다.

capture/encode/게시/normalized 표시와 고갈 시 decode는 gc_alloc guard로 검사한다.
할당 0은 이 처리 구간의 결과다. 원래 생성자·VM 실행·heap 채우기의 할당 시도는 별도다.
일반 표시에서 decode 결과를 한국어 sink에 연결하거나 sink GC/NLR/재진입을 검사한 것은
아니다. reporter 통합 뒤 해당 경계에서 다시 확인해야 한다.

## 독립 host 검증과 재현

기존 origin-store runner를 확장했고 새 중복 runner는 만들지 않았다. 예상 payload는
0x12345678abcd, 0x80000000ffffffff, 0xffff00020001 등의 고정 literal이다.
예상값을 인코더로 생성하지 않는다. UINT16_MAX/UINT32_MAX/INT_MIN/INT_MAX와 범위 밖,
불일치 repeat, 손상 schema·상위 비트를 검사한다. 모든 슬롯을 채운 뒤 끝까지 읽고,
해제·다른 ID/값의 재사용이 다른 슬롯을 퇴출하지 않는지도 확인한다.

```sh
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_probe64.py --only micropython
```

`mp-boundary/packing/`에 두 include, link/live/oom/양 cross compile 로그와 report.json을
남긴다. 통합 probe는 열 ID의 exact 목록과 GC·회수·고갈 marker를 검사한다.
순수 store/encoder 전체와 연결한 시제품 소스는 ASan/UBSan으로 검사했다. 연결된 기존
MicroPython core 객체는 미계측이며, 실행 옵션은
`ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0`이다.

## 남은 판정

이 encoding은 **임의의 두 uint64를 64비트에 넣는 해법이 아니다.** POS32를 넘는 size_t나
Q32를 넘는 qstr는 거부하며, 입력의 전체 producer 범위를 수용했다고 주장하지 않는다.
현재 고정 heap·qstr pool·인수 길이 경로에서 그런 값이 실제 생성될 수 있는지 추가로
감사해야 한다. 도달 가능하다면 저장 방식을 확장해야 하며 fallback으로 필수 범위를
숨기지 않는다. 이전 ABI 실험의 전체 64비트 stack capture와 이번 영속 encoding 범위는 다르다.

다음은 범위 밖 인수의 실제 도달 가능성 판정과 공통 ID/capture/reporter 통합이다.
비매크로 메시지·전체 REPL/traceback·closure 비용·전체 출력 catalog·생산 적용과 QEMU/Pi 5도
남는다. 양 strict cross compile은 AArch64 실행·실기 결과가 아니다. P0 전체 미완료다.
