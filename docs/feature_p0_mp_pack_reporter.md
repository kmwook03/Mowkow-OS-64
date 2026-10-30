# P0 압축 인수의 도달 범위와 공통 reporter

> 2026-10-28 범위 조정으로 P0의 추가 구현은 종료했다.
> 아래 남은 구현 항목은 P5로 이관했으며 [검토 결과](feature_p0_feasibility.md)를 따른다.

2026-10-28, `e18d682` 위의 미커밋 16차 시제품.
**10개 압축 format의 허용 범위를 한국어 reporter에 연결했지만, POS32를 넘는 값은
실제 공개 C 생성 경로에서 도달하므로 현재 encoding의 전체 지원은 불가능하다.**
vendor·생산 포트·OS ABI는 수정하지 않았다.

## 범위 판정

`feature_mp_pack64.py`는 감사한 producer·qstr·힙 초기화 소스 8개의 SHA-256을 고정한다.
소스가 달라지면 이전 범위 판정을 재사용하지 않고 실패한다. 실제 양 포트 compiler에서
type name 16비트, 연산명 테이블 원소 8비트, 정적 qstr 수, 단일 힙·추가 ROM pool 없음과
동적 pool의 보수적 상한을 검사한다. 생산 힙은 4MiB, 호스트 시제품 힙은 2MiB다.

| 필드 | 실제 producer에서 확인한 제약 |
| --- | --- |
| Q16 | type->name은 uint16, 연산명 테이블은 byte이며 정적 qstr도 UINT16_MAX 이하 |
| Q32 | 각 동적 pool 원소에 8바이트 pointer slot이 필요하고 이전 pool도 VM에 유지되므로, 고정 4MiB 힙에서 총 ID 수는 정적 개수 + 4MiB/8 미만 |
| I32 문자 | char 또는 byte의 기본 인수 승격이며 INT32 범위 안 |
| I32 위치 | 실제 포맷 문자열 안의 byte 차이이며 정상 힙 문자열의 크기로 제한됨 |
| REPEAT | 같은 byte 표현식을 두 번 전달하며 encoder도 전체 값의 일치를 다시 검사 |
| POS32 개수 | 공개 count validator는 size_t 전체를 받아 INT_MAX 초과 값도 실제 오류 생성자에 전달 |

`mp_arg_check_num_sig()`는 인수 배열을 읽지 않는 공개 개수 검사 함수다.
시제품은 원래 `mp_arg_check_num()`으로 최대 3개와 고정 1개 두 경로를 호출하고,
각각 INT_MAX+1, UINT32_MAX+1, SIZE_MAX를 전달한다. 임의의 format으로 생성자를 직접
호출하는 모형 대신 실제 argcheck.c producer 두 곳을 통과한다.

여섯 경우 모두 producer 타입으로 확보한 전체 size_t는 유지되지만 POS32 게시와
새 reporter는 출력 전에 거부한다. 원래 예외의 args와 하위 32비트 영어 출력은 독립
기대값 -2147483648·0·-1과 일치한다. 이는 원본의 varargs 타입 불일치를 고친 결과가 아니다.
Python 소스만으로 이 개수가 가능한지에 대한 모든 호출 경로의 증명은 하지 않았으며,
공개 C producer 전체를 수용한다는 주장에는 이 여섯 반례만으로도 충분히 제약이 드러난다.
임의 외부 메모리·위조 bytecode·임의 C 확장까지 문자열/개수 상한을 일반화하지 않는다.

## 공통 출처와 표시

`mp_origin_ids64.h`가 기존 MESSAGE64와 열 개의 새 태그를 함께 정의한다.
생성 계획은 각 태그를 기존 manifest의 문자열 ID와 연결하며 중복 ID는 거부한다.
새 태그는 12..21이고, 생산용 catalog ABI를 확정한 것은 아니다.
별도 pack ID/value 배열은 제거했으며 같은 `MP_ORIGIN_STORE64` 한 개에 기존·새 출처를 둔다.
공통 초기화·allocation/make_new/free 세대 정리·GC sweep을 사용한다.

포맷별 producer 타입으로 원래 va_list의 사본을 읽고, 검사한 로컬 pending payload를
원래 생성자 반환 뒤 게시한다. 압축 대상이 아닌 기존 format은 기존 capture로 보낸다.
`lifetime_print64()`가 언어를 한 번 선택하고 ID로 압축 reporter를 호출한다.
영어 모드는 원래 `mp_obj_print_exception()` 경로를 사용한다.
출력 전 payload/schema·현재 qstr pool 범위를 모두 검사한다. 손상 payload·알 수 없는
태그·사용자 예외에는 한국어 prefix도 출력하지 않는다.

첫 sink 호출 전에 예외 타입·복원된 수치/qstr를 모두 로컬에 확보한다.
이 열 schema에는 detached 객체가 없으므로 owner를 root로 유지할 필요가 없으며,
기존 VM qstr pool이 식별자 문자열을 소유한다. owner가 회수되거나 같은 emergency 주소가
사용자 예외로 바뀌어도 시작한 보고는 같은 인수와 언어를 사용한다.
부가 참조 reporter와 동일한 최대 8단 claim을 사용하며, 정상 반환과 NLR에서 정리한다.
9번째 시도는 출력 없이 false이고 생산 환경의 초과 정책은 여전히 미정이다.

2MiB 후보의 출처 metadata는 589,889바이트, 기존 scoped root/depth를 포함하면
589,961바이트로 유지된다. catalog/schema는 읽기 전용이며 계측·fixture root/fill 배열과
C stack snapshot/NLR 비용은 이 예산에 포함하지 않는다.

## 실제 검증

기존 단일 core runner를 확장했고 별도 VM 구현이나 중복 GUI runner를 만들지 않았다.
`live`는 실제 Python 오류 열 경로 각각의 원본 args와 한국어 정확한 바이트를 비교한다.
typed constructor 열 개는 GC 뒤에도 같은 표시를 내며, `oom`에서는 free=0·heap lock·
emergency args=0이어도 같은 인수를 복원해 표시한다. 각각 영어 모드가 원문과 같은지도
확인한다. 기존 256 live/회수·주소 재사용·모든 슬롯 검증도 유지한다.

`catalog`는 같은 저장소의 SyntaxError·NameError·압축 AttributeError를 GC 뒤 함께
표시하고, 내장 문구와 같은 사용자 문자열 및 손상 payload·pool 밖 qstr·미등록 태그를
구분한다. 기대 문자열·기대 payload는 formatter/encoder로 생성하지 않는다.

| sink 모드 | 실제 확인 |
| --- | --- |
| report-0 | 중첩 보고와 언어 변경 뒤 격리 GC가 owner를 회수해도 진행 중 출력 보존 |
| report-1 | free=0에서 같은 emergency 주소를 사용자 예외로 교체해 옛 출처가 사라져도 출력 보존 |
| report-2 | 동적 한국어 속성 qstr을 확보한 뒤 owner 회수 중에도 식별자 바이트 보존 |
| report-3 | sink의 격리 GC 후 실제 NLR이 같은 sentinel을 전달하고 claim 정리 |
| report-4 | 8단 재진입의 정확한 출력, 9번째 출력 없는 거부, 모든 claim 정리 |

격리 GC는 fixture owner/fill·C stack/register를 root로 삼지 않는다.
claim 정리는 각 시나리오 후 실제 공유 claim API의 8개 슬롯을 모두 다시 사용할 수
있는지 확인한다. capture·게시·정상 한국어/영어 표시와 adversarial reporter는 gc_alloc
guard로 검사한다. report-1 sink가 실행하는 원래 사용자 생성자의 할당 시도는 별도다.

```sh
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_probe64.py --only micropython
```

check64의 130개 테스트·style 315개 파일과 통합 probe를 통과했다.
`build64/feature-p0/mp-boundary/packing/`에 live/oom/range/catalog/report-0..4 로그와
범위·소스 hash·태그를 포함한 report.json을 남긴다.
시제품 소스 ASan/UBSan으로 아홉 모드를 통과했으며, 기존 VM core 객체는 미계측이다.
옵션은 `ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0`이며,
로그는 `/tmp/mowkow-p0-pack-report-{check64,probe,sanitize-build,sanitize-run}.log`다.

## 남은 작업

다음은 **실제로 도달하는 POS32 초과 size_t 인수의 무손실 영속 저장**이다.
현재의 거부를 성공으로 세지 않고 슬롯 예산·고갈·주소/세대 재사용·reporter 수명까지
검증해야 한다. 범위가 제한된 열 개의 호스트 표시 성공을 manifest 전체의 pending 149개
지원 완료로 바꾸지 않는다. 비매크로·전체 REPL/traceback·다른 ABI 인수·closure 최악 비용과
생산 적용 판정도 남는다. 이번 check64는 증분 게이트이며, AArch64 reporter 실행·
QEMU IDE/AHCI·Pi 5·생산 적용은 수행하지 않았다. **P0 전체는 미완료다.**
