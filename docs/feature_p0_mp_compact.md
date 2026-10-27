# P0 출처 메모리 축소·typed 인수 고갈 검증

2026-10-27, 기준 `8abf78e` 위의 8차 시제품.
**주소별 출처를 24바이트에서 9바이트로 줄인 예산 후보와 scalar/qstr/소유된 문자열의
보존 범위를 검증했다.** 원본 tuple이 사라진 비interned 문자열에는 미해결 소유권
문제가 남는다. P0 전체·전체 번역·생산 적용 완료를 뜻하지 않는다.

## 예산과 구조

기존 7차까지의 `WEAK_ORIGIN64`는 args 포인터·scalar·enum/padding으로 슬롯당
24바이트다. 축소 후보는 **ID 1바이트 배열 + 값 8바이트 배열**을 분리해 패딩을 없앤다.
설정 handle은 56바이트다. 각 GC 블록과 emergency에 전용 슬롯이 있어 살아 있는
기록을 capacity 때문에 퇴출하지 않는다. 추가 GC/host heap 할당이나 registry root가 없다.

| 구성 | 출처 메모리 | GC heap 대비 |
| --- | --- | --- |
| 기존 2MiB 시제품 | 1,572,888 bytes | 약 75% |
| 새 2MiB 시제품 | **589,889 bytes** | 약 28.128% |
| 현재 OS 4MiB heap에 적용할 경우의 계산값 | 1,179,713 bytes | 약 28.127% |

2MiB 후보는 상태값 포함 **약 62.496% 감소**다. 마지막 행은 계산값이며 커널에 이
저장소를 배치하지 않았다. 원래 production kernel에 기존 시제품 테이블이 있었다는
의미도 아니다. 수치는 출처 저장소만 세며 시험 root/fill/output 버퍼는 제외한다.

이번 구현 기본값은 **GC heap의 30% 이하를 후보 예산 상한**으로 둔다. 사용자 확정
예산이 아니다. 컴파일 assertion과 실제 실행 보고값으로 이를 검사한다. 더 작은 예산을
채택하려면 전체 인수 범위와 최악의 live 수를 다시 검증해야 한다. 현재 154개 목록은
8비트 ID로 표현 가능하지만 모든 format의 typed payload가 64비트에 들어간다고 아직
판정하지 않는다.

`mp_origin_store64.c/.h`는 MicroPython에 의존하지 않는 시험용 저장소다. 초기화는
heap span·정렬·block·capacity·곱셈/주소 endpoint·metadata/heap/config overlap을
확인한 뒤 게시한다. 실패 시 기존 설정/배열을 보존하며 잘못된 get은 출력도 유지한다.
이후 설정은 불변이고 단일 VM/task가 사용한다. 생성자/성공한 외부 gc_alloc에서 이전
출처를 지우며 실제 sweep 뒤 죽은 owner의 기록을 제거한다.

compact 경로는 함수 identity를 보존한 7차 strong-wrapper 빌드에만 연결할 수 있다.
args 포인터 guard를 제거했으므로 그 생성/회수 계약이 전제다. 기존 실패 재현용 ELF와
24바이트 후보는 그대로 별도 실행하며 새 ELF에서는 그 테이블을 컴파일하지 않는다.
GC finaliser·split heap·LTO/ICF 설정 확장은 여전히 별도 검증이다.

## 인수별 수명 계약

| 인수 | 저장/표시 방법 | 고갈 시 결과 |
| --- | --- | --- |
| 고정 문구 | ID만 사용 | args 없이 표시 |
| `size_t` 수치 | va_copy에서 원래 64비트 값을 복사 | 0·SIZE_MAX, tuple 손실·원문 절단에도 보존 |
| NameError qstr | qstr ID | 이미 VM qstr pool이 소유하며 GC 후에도 표시 |
| callable 타입 이름 `%s` | 기존 qstr의 조회만 수행, 새 intern 할당 없음 | qstr이면 args 없이 보존 |
| 비interned callable 문자열 | 원래 예외 args가 완전한 복사본을 소유하는지 검증 후 그 범위를 빌림 | 불완전/없으면 reject |
| 파일명 | 원래 args의 완전한 문자열을 빌리거나 errno32+기존 qstr32를 한 값으로 보존 | 둘 다 없으면 reject |

문자열 비교는 **이미 builtin 생성 경계에서 얻은 인수의 소유권/완전성**을 검사한다.
사용자 문자열을 보고 builtin 출처를 추측하지 않는다. 원래 예외·tuple·str/args와
사용자 생성/subclass·재던지기의 의미를 바꾸지 않는다.

`qstr_find_strn`은 이미 있는 pool만 조회한다. caller의 임시 char 포인터를 저장하거나
실패 시 intern/메모리 할당을 시도하지 않는다. errno+qstr 압축은 qstr<=UINT32_MAX를
확인한 경우에만 사용한다. 레코드를 읽어 로컬로 복사한 후 출력하여 중첩 reporter가
다음 출력 언어를 바꾸어도 현재 수치/qstr은 유지된다.

## 실제 코어 고갈 실험

7차의 실제 x86 코어/격리 objexcept/runtime 사본을 그대로 사용한다. 메모리 고갈은
모든 실제 GC 블록을 유지한 free=0에서 시작한다. 부분 고갈은 실제 gc_free로 정확히
0~8블록만 다시 풀고, 전후 gc_info를 확인한다. VM·allocator·예외 객체는 mock하지 않는다.
각 경우 새 프로세스/VM이고 매 한국어 report의 gc_alloc 호출 수와 used 불변을 검사한다.

- SIZE_MAX 요청 설명문 생성: free 0/1블록은 emergency/빈 args, 2블록은 **일반 객체/빈
  args**, 5블록도 일반 객체/빈 args, 6~8블록은 원문 args가 남는다. 모두 정확한
  `18446744073709551615`를 표시하고 GC 후 다시 같은 값을 표시한다.
- 5개 free 블록을 singleton/2블록-buffer/singleton/singleton으로 단편화한다. 원본
  formatter는 grow/move를 못 해 **일반 객체의 문자열이 절단**되지만 compact payload는
  전체 수치를 표시한다. 실제 `args=1, truncated=1, emergency=0`을 요구한다.
- heap lock에서 동적 qstr NameError·타입 이름과 수치 0을 표시한다. 원래 args는 비어
  있어야 한다. interned 문자열 조회/보고에서 추가 GC 할당을 하지 않는다.
- 임시 타입명은 원본 args가 완전한 복사본을 소유하는 경우만 허용한다. source 버퍼를
  덮어쓰고 GC한 후에도 원문 전체를 출력한다. heap lock으로 그 복사본이 없으면 명시적으로
  reject한다. 살아 있다고 가정한 raw 포인터를 남기지 않는다.
- 임시 파일명과 interned 파일명을 각각 free 0~8블록에서 실제 filename raise에 넣는다.
  free 0~2는 실제 MemoryError다. 3~4는 **OSError이나 원래 args가 빈 tuple**이다.
  interned 이름은 qstr로 복원하며, 비interned 이름은 reject한다. 5~8은 원래 args로
  보존된다. caller 버퍼를 덮어쓰고 GC한 뒤에도 허용/거부 결과를 유지한다.

`MESSAGE_REJECT64=255`, 값 61은 이번 시험의 **원래 인수 데이터 소유권 부족** 상태다.
실험은 이를 발견해 성공적인 번역과 구분한다. 새 production 오류 ABI가 아니다.
이 상태에서 한국어 표시를 거부하는 것은 요구 충족이 아니며, 영어 fallback으로
전체 번역 성공을 주장하지 않는다.

기존 identity/6개 생성자 경로·OSError/recursion·사용자/subclass·NLR 2차 실패·재던지기,
256개 live/실제 GC/재생성/주소 재사용·중첩 표시/언어·heap-lock/free=0와 128회 emergency
세대 검사를 compact ELF에서도 모두 다시 실행했다. 실패 재현용 기존 ELF도 유지한다.

## 재현과 검증

```sh
make clean64
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_probe64.py --only micropython
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_mp_audit64.py \
  --check docs/feature_p0_mp_catalog.json
```

- clean64/check64: style 304개·host 114개·양 OS/앱/private nano archive·ELF PASS.
- 새로운 host 2개는 실제 C 저장소에서 65,536개 슬롯+emergency의 full-width 값,
  무퇴출·재사용·invalid endpoints/alignment/capacity/overlap과 실패 시 보존을 확인한다.
- 저장소 단위 ASan·UBSan full/invalid PASS. sandbox의 LeakSanitizer 제한 때문에
  사용자 승인 후 같은 실행 파일을 sandbox 밖에서 검증했다.
- 저장소 C를 양 아키텍처 실제 strict port flags로도 직접 컴파일했다.
- QEMU IDE/AHCI·Pi 5는 이번 작업에서 미실행이다. production source/ABI/build/link,
  vendor·32bit·머꼬 upstream은 변경하지 않았다.

전체 결과는 `build64/feature-p0/report.json`의 `identity_boundary_probe.compact_store`와
`mp-boundary/compact/report.json`이다. `link.log/map`, 기존 9개 모드, `typed/owned.log`,
`partial-{0..8}/partial-fragment.log`, `filename-{0,1}-{0..8}.log`가 실제 관측을 남긴다.
Python 도구는 원문 손실/절단·일반 객체·interned 복원·비interned 거부가 **실제로 발생**하지
않으면 실패한다. 단순한 oversized 요청 실패를 고갈로 세지 않는다.

## 판정과 다음 작업

이번 **축소 후보·typed 인수 보존 범위 조사**는 완료했다. 현재 지원한 scalar/qstr/
owner-backed 문자열은 예산 내에서 보존하지만 전체 메시지 번역/P0는 미완료다.
활성 catalog의 149개 pending ID는 유지한다. 전체 format/비매크로·REPL/traceback,
명시적 sys.print_exception·나머지 출력 catalog·실기도 남는다.

다음 P0 작업은 **원래 args에서 이탈한 동적 문자열의 소유권을 GC 수명과 연동해
보존할 수 있는지 검증**하는 것이다. 실제 make_new의 입력 args에서 원본 tuple 손실 전에
데이터를 확보하고, owner가 살아 있을 때만 그 데이터를 유지할 수 있어야 한다. 주소/
emergency 세대 재사용·회수·NLR/재진입·예산을 함께 검증한다. 무조건 registry root로
유지하거나 임시 문자열을 영구 intern하는 방법은 live 수명/메모리 문제를 해결하지 못한다.
이 빌드 경계 후보만으로 부족하면 제약과 최소 hook 대안을 먼저 구체화한다. vendor 정책
변경을 미리 요청/적용하거나 손실을 정상 완료로 표시하지 않는다.
