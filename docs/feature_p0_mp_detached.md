# P0 이탈 문자열의 조건부 GC 소유권 실험

2026-10-28, 기준 `e18d682` 위의 미커밋 시제품.
이번에는 **원본 args에서 이탈한 문자열 한 개의 확보·조건부 mark·회수 원리**를
검증했다. 전체 출처 저장소 통합·생산 적용·P0 완료는 아니다.

## 실제 생성 경로와 대조군

`mp_raise_OSError_with_filename(5, path)`는 동적 문자열
`can't open transient_file_79271593.py`를 만든 뒤 실제
`mp_obj_exception_make_new()`에 전달한다. free=0에서 실제 GC 블록 4개만 풀어
문자열과 일반 OSError 객체는 생성되지만 args tuple은 비는 상태를 재현한다.
강한 생성자 경계에서 입력 `args[1]`을 지역 변수로 확보하고 원본 호출이 반환한
예외와 함께 시험 관찰 변수에 게시한다. 원본 예외·args·문자열은 변경하지 않는다.
호출자의 path는 덮어쓴다. NLR은 실제 filename raise/catch 경로를 사용한다.

각 경우 독립 프로세스에서 실행한다. stack/register root는 의도적으로 생략하고
명시적 root 하나로만 owner 생존을 결정한다. 관찰 변수 자체는 root로 등록하지 않는다.

| 정책 | owner 유지 | owner 해제 | 의미 |
| --- | --- | --- | --- |
| 0: 문자열 미표시 | 문자열 회수 | 문자열 회수 | owner가 살아 있어도 부가 참조는 추적되지 않음 |
| 1: 문자열 무조건 root | 문자열 보존 | 문자열 보존 | 죽은 owner의 데이터까지 남기는 잘못된 대조군 |
| 2: owner MARK일 때만 문자열 root | 문자열 보존 | 문자열 회수 | 이 한 개의 edge에서 원하는 수명 |

보존된 경우 문자열의 전체 길이와 독립 상수의 바이트를 비교한다. 정책 2의 생존
사례에서는 owner root를 해제한 두 번째 GC로 **owner와 문자열 모두 회수**됨도 검증한다.
모든 mark/sweep 구간에서 gc_alloc 호출 증가가 0이어야 한다. 원본 생성 중 할당은
이 무할당 측정에 포함하지 않으며, 원래 args가 비었다는 assertion은 모든 경우 필수다.

## mark 조회의 제약과 필요한 연결 지점

`gc_nbytes()`는 HEAD의 할당 크기만 반환한다. 수집 도중에는 MARK에도 0을 반환하므로
0이 곧 회수되었음을 뜻하지 않는다. 이번 실험은 고정 버전 `mpstate.h`의 단일 heap
범위·블록 정렬·allocation table 범위를 검사하고 2비트 AT_MARK=3을 직접 관찰한다.
`feature_mp_boundary64.py`는 gc.c와 mpstate.h의 SHA-256을 확인하고 변경 시 거부한다.
이는 공개 API가 아닌 **버전 종속 실험**이다. vendor 원본은 수정하지 않는다.

일반 적용에는 port의 `gc_collect()`에서 일반 root 추적이 완전히 끝난 뒤,
sweep 전에 부가 edge를 mark하는 지점이 필요하다. 현재 upstream은 mark stack overflow
복구를 `gc_collect_end()` 안에서 수행한다. 따라서 단순히 end 직전에 한 번 스캔하면
아직 발견되지 않은 owner를 놓칠 수 있다. 이번 fixture는 전후 overflow=0을 요구한다.
최종 구현은 overflow 복구 뒤의 hook 또는 그에 동등한 완전한 mark closure를 입증해야 한다.
사용자가 vendor 정책을 바꾸도록 요청하거나 승인한 상태는 아니다.

## 저장 비용과 남은 검증

이번 한-edge 관찰 변수는 owner 포인터·arg 포인터·enable bool이다. 기존 589,889바이트
출처 테이블의 payload에 통합하지 않았고, 이 관찰 변수를 전체 인수 저장 예산으로
계산하지 않는다. full-width errno와 동적 객체를 동시에 저장하는 encoding·최대 live 수는
별도 설계가 필요하다. 단순히 슬롯마다 포인터 8바이트를 추가하면 2MiB heap에서
589,889 + 524,296 = 1,114,185바이트(약 53.13%)이므로 기존 30% 후보 예산을
넘는다. 이 계산은 모든 슬롯의 포인터를 추가하는 대안의 비용이며 채택한 구현이 아니다.

9차에서 남긴 후속 범위는 실제 저장소 확장 전 **GC mark closure 연결 경계**
검증이었다. 아래 10차에서 overflow closure와 emergency 세대의 실험 결과를 기록한다. overflow 경로, emergency owner의 비heap 수명과 생성 세대,
주소 재사용·중첩 생성/NLR 실패·재진입을 다뤄야 한다. 생성자 입력 확보가 예외를
잡아버리는 root가 되어서는 안 되며, 해제/재사용 시 stale edge를 반드시 제거해야 한다.
이후 동적 문자열+errno 저장 예산과 무퇴출을 검증하고 reporter에 통합한다.
현재 기존 filename 고갈 실험의 비interned reject는 그대로 남긴다.

## 재현

```sh
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_probe64.py --only micropython
```

통합 probe는 `--compact-detached-{0,1,2}-{0,1}` 여섯 모드를 실행하고 exact marker와
정책 2 해제 marker를 확인한다. 결과는
`build64/feature-p0/mp-boundary/compact/detached-*.log`와 `report.json`의 `detached_gc`에
남는다. 기존 9개 경계/수명 모드와 partial·filename 고갈 실험도 계속 실행한다.
QEMU IDE/AHCI와 Pi 5 실기는 이번 실험 범위에 포함하지 않는다.

## 10차: overflow closure와 emergency 세대

2026-10-28. 앞선 한-edge 실험에 다음 세 모드를 추가했다.

- `--compact-closure-0`: 실제 mark stack 크기보다 8개 많은 자식을 둔 fanout을
  만들고, overflow로 표시만 된 마지막 자식 아래에 일반 OSError owner를 둔다.
  root tracing 뒤 overflow=1이고 owner는 아직 MARK가 아님을 요구한다. 부가 추적 없이
  end를 호출하면 원래 GC는 owner를 살리지만 이탈 문자열은 회수한다.
- `--compact-closure-1`: 같은 그래프에서 모든 MARK allocation의 전체 내용을
  `gc_collect_root()`로 재추적한다. owner가 표시되면 그 부가 문자열도 추적하며,
  MARK 개수가 늘지 않을 때까지 반복한다. 실제 결과는 2회 pass로 문자열 보존이다.
  owner root를 해제한 다음 GC에서는 owner와 문자열 모두 회수된다.
- `--compact-emergency`: 실제 고갈에서 3블록만 풀면 문자열은 생성되지만 owner는
  비heap emergency singleton이다. 현재 세대의 문자열을 closure에서 보존하고 전체
  바이트를 확인한다. 이후 heap lock 중 원래 생성자로 사용자 OSError를 128회 만들며
  같은 singleton 재사용마다 옛 부가 edge가 없음을 검사한다. 다음 GC는 옛 문자열을 회수한다.

생성자는 결과 owner가 기존 owner와 같으면 이전 edge를 먼저 제거한 뒤 새 입력을
게시한다. gc_alloc 성공 주소 재사용에도 edge 제거를 연결했다. **일반 주소 재사용의
별도 fault 시나리오와 중첩 생성/NLR 실패는 아직 미검증**이다. emergency의 현재 세대는
일반 heap owner처럼 root 도달성으로 결정하지 않는다. VM 소유 singleton의 세대가
교체될 때까지 보존하는 후보이며, 오래된 외부 참조로 이전 세대를 구분할 수는 없다.
128회는 사용자 교체 생성 횟수이고 builtin/user 128쌍을 의미하지 않는다.

closure 후보는 vendor 원본·GC bit를 수정하지 않고 기존 public root tracer를 호출한다.
upstream overflow flag도 그대로 두고 마지막 end가 기존 복구·sweep을 실행하게 한다.
단일 heap의 고정 allocation table encoding과 보수적 포인터 추적에 의존하며,
최대 heap block 수 + 1 pass로 제한한다. 각 증가 pass는 새 MARK head를 하나 이상 만든다.
동시 할당/변경이 없다는 fixture 전제가 필요하다. 매 pass가 heap 전체를 스캔하므로
최악 O(block 수²)의 지연을 가진다. **생산 성능이나 전체 port 적용을 승인한 결과가 아니다.**
finaliser·split heap·weakref 확장은 다루지 않았다. 이번 세 모드의 mark/sweep·closure는
추가 할당 없이 동작하며, 관찰용 owner/arg 한 쌍만 있어 전체 저장소 예산은 여전히 미확정이다.

결과는 compact 폴더의 `closure-{0,1}.log`, `emergency.log`, `report.json`의 `closure`에
기록된다. 다음 작업은 전체 부가 인수 저장소의 예산·무퇴출·회수 설계와 주소 재사용/
중첩·NLR·재진입 검증이다. 이어 reporter 통합과 closure의 실행 비용을 검증한다.
기존 filename reject와 149개 pending format은 그대로 남는다.

11차의 전체 슬롯 typed 후보와 재사용/중첩 검증은
[부가 참조 저장소](feature_p0_mp_references.md)에 기록한다.
