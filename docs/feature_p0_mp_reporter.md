# P0 부가 참조 reporter의 scoped root·재진입·NLR

2026-10-28, `e18d682` 위의 미커밋 시제품 확장.
**객체 하나+errno32의 opt-in reporter 후보**를 실제 MicroPython 출력/GC 경계에 연결했다.
전체 catalog·traceback·생산 적용·P0 완료를 뜻하지 않는다.

## 출력 중 문자열 소유권

`MESSAGE_REF64`는 한국어 보고를 시작할 때 저장소의 target/errno와 예외 type qstr을
캡처한다. 첫 sink 호출 전에 target을 최대 8개의 scoped root 중 하나에 게시한다.
문자열 객체를 root로 두면 nonmoving GC가 문자열 data까지 추적하므로, owner가 회수되거나
emergency의 동일 주소가 새 세대로 바뀌어도 이미 시작한 보고의 바이트는 유효하다.
owner와 전역 출처 테이블 전체를 root로 올리지 않는다. 원본 예외/args는 변경하지 않는다.
입력 모드 선택은 기존 report 시작 경계에서 한 번 이루어지며 sink의 언어 변경은 현재
보고를 다시 번역하지 않는다.

호스트 port의 실제 `gc_collect()`는 일반 root/stack 추적 뒤 `lifetime_trace64()`로
참조 closure를 실행한다. `lifetime_roots64()`는 활성 scoped root만 등록한다.
root 배열 64바이트 + depth 8바이트로 **추가 72바이트**이며, 저장소와 합친
2MiB 후보 metadata는 **589,961바이트**다. 계측 counter·시험 상태는 별도다.
컴파일 assertion으로 크기와 기존 heap 30% 후보 상한을 확인한다. 함수별 NLR buffer와
로컬 snapshot의 C stack 비용은 이 정적 metadata와 별도이며 깊이는 8로 제한한다.

각 프레임은 정상 반환과 sink NLR 모두 자신의 root를 0으로 지우고 이전 depth를 복원한다.
NLR은 받은 동일 값을 바깥 handler로 다시 전달한다. 9번째 재진입은 새 root/출력 없이
명시적으로 false를 반환한다. 시험은 이 실패를 확인하며 번역 성공으로 세지 않는다.
기존 외부 wrapper는 false에서 원문 출력을 시도하므로, 생산 환경의 깊이 초과 정책은
아직 확정되지 않았다. 이 제한을 전체 재진입 무제한 지원으로 주장하지 않는다.

## 실제 시나리오

모두 free=0에서 실제 4블록(일반) 또는 3블록(emergency)만 풀어 filename raise를 실행하고
원래 args=0을 요구한다. 각각 별도 프로세스다. 시작 전 heap lock 상태의 정상 한국어
보고에서 추가 gc_alloc=0과 원본 args 보존도 확인한다.

| 모드 | sink 동작 | 확인 |
| --- | --- | --- |
| report-0 | 실제 mp_obj_print_exception wrapper에서 시작, 중첩 보고, 언어 변경, 실제 gc_collect 호출, owner root 없는 격리 GC | 실제 trace bridge 호출 수 증가, 두 출력의 정확한 바이트, owner 회수 뒤 target 보존 |
| report-1 | heap lock 중 같은 emergency 주소를 사용자 OSError로 교체한 뒤 격리 GC | 옛 출처 제거 뒤에도 진행 중 출력의 파일명/errno 보존 |
| report-2 | 격리 GC 후 미리 생성한 ValueError로 실제 NLR | 동일 예외가 caller에 도달, 모든 claim 해제 |
| report-3 | 같은 예외의 출력을 8단계 재진입하고 각 단계에서 격리 GC | 9번째 시도 1회 거부, 8개 root 정리 |
| report-4 | 2단계 보고 안쪽 sink에서 격리 GC 후 실제 NLR | 안쪽과 바깥쪽 프레임 모두 정리, 동일 예외 전달 |

격리 GC는 C stack/register 및 fixture owner/fill root를 의도적으로 제외하고 실제 scoped
root와 NLR sentinel만 추적한다. 문자열이 우연한 stack root로 살아남은 것을 성공으로
세지 않기 위해서다. 마지막에는 모든 claim이 0임을 확인하고 같은 격리 GC로 이전 target이
실제로 회수되는지 검사한다. report-0/1은 원문 전체와 독립 기대 문자열을 비교한다.
report-0/2/3/4의 adversarial 보고도 gc_alloc 호출 증가가 0이다. report-1의 sink 자체는
원래 생성자를 실행하므로 그 할당 시도까지 무할당이라고 주장하지 않는다.

## 재현·제약·다음 작업

```sh
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_probe64.py --only micropython
```

`compact/report-{0..4}.log`의 exact result와 전체 claim 해제·target 회수를 검사한다.
`compact/report.json`의 reference_store는 깊이/정적 root 비용과 이 후보의 검증 범위를
기록한다. 기존 출처/identity/사용자 문자열/고갈/closure/저장소 회귀도 계속 실행한다.

단일 VM/task·nonmoving GC·고정 layout·기존 closure의 제약을 유지한다. 실제 커널의
파일/REPL/stdout에 설치하지 않았으며 AArch64 실행·QEMU·Pi 5 검증을 대체하지 않는다.
사용자 예외의 출처 분류를 확장하지 않는다. 일반 filename 실험의 비interned reject는
분리하여 유지하고, 이 새 opt-in 모드의 성공으로 전체 메시지 지원을 판정하지 않는다.

다음 작업은 **149개 pending format의 typed 인수 분류·저장 범위와 실패 정책 확정**이다.
여러 동적 객체/64비트 수치가 함께 필요한 format, 비매크로 메시지와 traceback/REPL 경계를
검토해 현재 encoding과 scoped root로 전체 요구를 충족할 수 있는지 판정해야 한다.
closure 최악 실행 비용·깊이 초과·생산 적용 정책과 전체 번역/실기 검증도 남는다.

13차의 전체 typed 분류와 producer/consumer 감사는
[typed 인수·정책](feature_p0_mp_types.md)에 기록한다.
