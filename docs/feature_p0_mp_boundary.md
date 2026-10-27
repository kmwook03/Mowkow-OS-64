# P0 MicroPython 함수 identity를 보존하는 생성 경계

후속 8차 [출처 저장 축소·typed 인수 고갈 검증](feature_p0_mp_compact.md)은 이 경계에
9바이트/슬롯 후보를 연결했다. scalar/qstr/owner-backed 문자열은 검증했으며,
원래 tuple이 사라진 비interned 문자열의 소유권이 다음 작업이다.

2026-10-27, 기준 `8abf78e` 위의 기존 미커밋 감사·수명 시제품에 이은 7차 실험.
**vendor 소스를 수정하지 않고, 현재 구성의 예외 생성자와 비상 객체 재사용을
추적하는 빌드 경계 대안이 호스트 실제 코어에서 통과했다.** 전체 문구 번역이나
커널 적용 완료는 아니다. P0 전체는 미완료다.

## 재현

```sh
make clean64
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_probe64.py --only micropython
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_mp_audit64.py \
  --check docs/feature_p0_mp_catalog.json
```

`feature_probe64.py`가 양 OS의 전제 빌드, 기존 실패 재현, 신규 경계 실험을 순서대로
수행한다. 새 `feature_mp_boundary64.py`는 Make의 실제 compiler/flags를 읽으며,
기존 OS 오브젝트나 링크 입력을 덮어쓰지 않는다. 네트워크·vendor 쓰기는 없다.
호스트 compiler와 binutils, 양 cross-toolchain이 필요하다.

산출물은 무시되는 `build64/feature-p0/mp-boundary/` 아래에 있다.

- `report.json`, `objects.json`, `host-link.log`, `host-link.map`, `host-symbols.log`.
- `identity/raises/users/secondary/live/gc/reuse/lock/oom.log`: 각각 독립 프로세스/VM.
- `{x86_64,aarch64}/`: 원본 빌드 사본·alias 사본·엄격 경고 forwarding fixture·부분 링크,
  compiler 명령·심볼·재배치·검증한 코드 section 목록과 SHA-256.
- 상위 `report.json`의 `identity_boundary_probe`는 신규 성공이다. 기존
  `noalloc_probe`의 FAIL은 단순 `--wrap` 방식의 실패 재현으로 그대로 구분한다.

새 경계의 실행은 각각 30초 timeout으로 제한한다. production assertion halt를
임의로 성공 처리하지 않으며, 종료 0과 실험 내부 assertion을 모두 요구한다.

최종 clean64/check64: style 300개·host 112개·양 OS/앱/private nano archive·ELF PASS.
깨끗한 산출물로 기존+신규 MicroPython probe 전체와 활성 catalog --check도 통과했다.
QEMU IDE/AHCI·Pi 5는 이번 작업에서 실행하지 않았다. 호스트 실행과 cross 정적 검사를
OS 실기 결과로 해석하지 않는다.

## 원본 보존과 링크 방법

두 원본을 컴파일 전에 아래 SHA-256으로 확인한다.

| 원본 | SHA-256 |
| --- | --- |
| `py/objexcept.c` | `8a375dbfb56445f08a1d7cf1d6e5a12c291902a8185bb1a724f0d314319c174e` |
| `py/runtime.c` | `8ee7aded13aa2af2c0254349fd7af6e77f68b980700e6bdebe498d755967fd70` |

원본 submodule은 `e0e9fbb17ed6fd06bb76e266ae554784c9c80804`다. 저작권·라이선스와
원본 파일을 그대로 보존한다. 별도 vendor 복사/패치 배포를 도입하지 않았다.

1. 실제 아키텍처 flags로 두 TU를 **격리된 산출물에 재컴파일**한다. same-TU
   inline/clone/ICF와 sibling-call 결합을 막는 다섯 flag는 도구의 `FLAGS64`에 있다.
   따라서 이 재빌드의 기계어가 production 오브젝트와 같다는 주장은 하지 않는다.
2. objdump에서 각 대상 함수의 실제 section/offset을 확인한다. objcopy로 새
   오브젝트 사본에 `__real_<name>` 원본 entry alias를 추가하고 공개 정의를 weak로
   바꾼다. 누락·중복·undefined·잘못된 binding은 오류다. 같은 파일에 덮어쓰지 않는다.
3. 해당 공개 이름에 strong wrapper를 링크한다. 예외 type의 make_new 슬롯과
   내부/외부 함수 주소 검사가 **같은 공개 wrapper 주소**를 보며, wrapper는 원본
   alias를 호출한다. 원본 생성자가 새 객체/비상 객체를 반환할 때 출처를 초기화한다.
4. builtin msg/vlist/args wrapper는 원본 호출 후 출처를 게시한다. 내부 varg가
   같은 TU의 vlist를 호출하는 경우도 공개 wrapper로 진입한다. 별도의 mutable
   전역 생성 문맥에 의존하지 않는다.
5. `mp_raise_type_arg`와 파일명 포함 OSError에는 실제 원본을 호출하는 NLR 경계를
   둔다. errno·재귀 qstr·살아 있는 원본 파일명 args의 builtin 출처를 게시한 뒤 같은
   예외를 다시 던진다. 원본 작성 중 MemoryError가 발생하면 그 실제 타입의 출처를
   보존한다. Python 생성자는 이 builtin 게시 경계를 통과하지 않는다.

대상은 `make_new`, `_new_exception`, `_args`, `_msg`, `_msg_varg`, `_msg_vlist`와
두 raise 함수다. 공개 ABI·예외 객체·args·`str(exc)`는 바꾸지 않는다. 내부 marker는
생성 경계에서 얻으며 사용자 문자열을 보고 builtin 출처를 추측하지 않는다.

변환 전후 **모든 nonempty CODE section의 dump와 재배치 대상**을 비교한다.
AArch64는 빈 `.text`와 여러 `.text.<function>` section을 사용하므로 그 전체를
확인한다. alias 변환은 이 격리 재빌드의 명령/재배치를 바꾸지 않는다. 원래 공개
함수 이름으로 가는 same-TU 재배치도 요구한다. 최종 public strong 정의와 원본
alias 주소는 다르며, 슬롯/내부 검사는 public 주소로 통일되는 것을 실제 실행한다.

양 아키텍처의 forwarding fixture는 모든 호출을 실제 원본에 전달한다. 그 빌드와
`ld -r`는 ABI·심볼 메커니즘만 검증한다. AArch64 실행 검증이나 전체 커널 링크로
표현하지 않는다. 호스트는 x86 실제 코어 중 위 두 TU만 격리 사본으로 교체한다.

## 실제 코어 결과

| 경로 | 독립 관찰 | 결과 |
| --- | --- | --- |
| identity·생성자 | builtin 6개 타입 슬롯==공개 주소, 원본 alias와 다름. 생성자 6경로의 make_new 증가량, 실제 원본 varg→vlist 호출과 인수 29 | PASS |
| errno·filename·recursion | 실제 `mp_raise_OSError(5)`, 파일명 포함 함수, 재귀 깊이 함수. make_new/상위 경계 증가량·원본 args·정확한 무할당 출력·GC 후 재출력 | PASS |
| 사용자 생성 | 실제 Python SyntaxError/OSError/RuntimeError와 ValueError subclass. 같은 builtin 영어 문구와 사용자 한글 원문 유지 | PASS |
| 재던지기 | Python의 원래 `str/args` assert, GC 뒤 같은 builtin 객체 재던지기. 생성 세대 증가 없이 출처 유지 | PASS |
| 2차 실패 | heap-lock 상태의 filename 작성 실패는 MemoryError로 유지. 이어 locked EIO, unlock 뒤 recursion과 caller NLR 회복 | PASS |
| live/GC/reuse | 앞선 256개 live·회수·같은 VM 재생성·실제 gc_alloc 주소 재사용 실험을 새 경계로 재실행 | PASS |
| 진짜 OOM | 모든 블록을 root로 유지해 free=0에서 실제 m_malloc(1) 실패. 원래 size 1의 무할당 한국어 표시 | PASS |
| emergency 세대 | heap-lock와 free=0 각각 builtin/user 동일 주소/type/빈 args 128회 교대. 사용자 생성마다 출처 제거·사용자 원문 보존 | PASS |
| 재진입·언어 | 실제 VM NameError/TypeError·사용자 문자열 보존과 기존 중첩 reporter/언어 고정 실험을 새 경계로 재실행 | PASS |

OOM 후 사용자 생성은 Python이 사용하는 실제 type slot callback을 호출한다.
소스 파싱/컴파일 전체를 free=0에서 실행했다는 의미는 아니다. recursion은 실제 코어의
raise 함수를 호출했으며 Python 스택을 실제로 소진한 시험은 아니다.

호스트 도구 테스트 4개는 실제 C 오브젝트의 function section·심볼을 변환하고 별도
strong wrapper와 링크/실행하여, 슬롯과 내부 호출 모두가 wrapper를 거치는지 확인한다.
원본 오브젝트 bytes 보존, 누락/중복/undefined/binding 오류, in-place 거부, 빈 `.text`와
function section 처리를 확인한다. 값만 넣은 mock으로 production 호출을 대신하지 않는다.

## 남은 범위와 다음 작업

이 결과로 **현재 구성의 생성 경계 문제를 vendor 정책 변경 없이 해결할 수 있는
구체적인 후보**를 확보했다. 기존 단순 --wrap 실패를 전체 무수정 방법의 불가능성으로
확대하지 않는다. 커널의 production inventory/flags/link에는 아직 적용하지 않았다.
compiler·upstream·LTO/ICF·heap/finaliser 설정이 바뀌면 다시 검증해야 한다.

아직 앞선 정적 1,572,888바이트 출처 테이블을 사용한다. 메시지 format은 기존 5개와
EIO, 이번 errno filename/recursion 경계만 시제품 범위다. 활성 catalog 154개의
나머지 **149개 pending ID**는 유지한다. 새 qstr/파일명 경계를 catalog 전체 완료 수로
더하지 않는다. 전체 format의 typed 인수 수명·추가 GC 설정·전체 REPL/traceback·명시적
sys.print_exception·OS 콘솔 언어 API·실기 검증도 남아 있다.

다음 P0 작업은 **생산용 메모리 예산에 맞는 출처/typed 인수 저장 방식의 설계·고갈 검증**이다.
살아 있는 예외를 무단 퇴출하지 않으면서 gc sweep/주소·emergency 재사용과 결합하고,
원본 args가 생략/절단되는 부분 고갈·완전 고갈에서도 인수 보존 범위를 명확히 해야 한다.
그 뒤 전체 활성/비매크로 메시지와 REPL 경계를 확장해 vendor 무수정 전체 가능 여부를
판정한다. 나머지 OS 출력 catalog와 Pi 5 검증도 계속 남는다.
