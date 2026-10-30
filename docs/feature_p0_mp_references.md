# P0 부가 참조 저장소의 예산·수명 후보

2026-10-28, `e18d682` 위의 기존 미커밋 시제품 확장.
**동적 객체 하나 + signed errno32**를 기존 9바이트 슬롯에 저장하는 후보를 검증했다.
모든 format 지원·reporter 통합·생산 적용·P0 완료는 아니다.

## 저장 형식과 예산

ID 1바이트와 payload 8바이트 구조는 유지한다. 전용 ID `MESSAGE_REF64`에서만
payload 하위 32비트는 `reference - heap` 바이트 offset, 상위 32비트는 signed errno다.
포인터는 heap 범위·GC block 정렬·32비트 offset을 검사하고, 읽을 때도 범위·정렬과
주소 합산을 검사한다. errno는 INT32_MIN부터 INT32_MAX까지 비트를 보존한다.
잘못된 ID/주소/offset과 drop된 레코드는 오류를 반환하며 이전 저장값/출력을 보존한다.

기존 scalar64/qstr payload와 해석을 섞지 않는다. 저장 비용은 2MiB heap에서
**589,889바이트**, 현재 4MiB에 대한 계산값 **1,179,713바이트**로 유지된다.
각 block + emergency마다 슬롯이 있어 live 레코드를 capacity 때문에 퇴출하지 않는다.
추가 root 배열/할당은 없다. 시험 root·fill·출력 및 재진입 제어 변수는 이 metadata 수치와
별도다. heap offset이 32비트를 넘는 참조는 거부한다. 문자열 하나와 size_t64 또는
여러 동적 인수를 함께 갖는 format에는 이 encoding을 무조건 적용할 수 없다.
149개 pending format의 전체 인수 예산 판정은 여전히 남는다.

## 실제 GC와 생성 경계

opt-in `reference_store64` 실험은 실제 make_new 입력의 OSError 두 번째 동적 문자열과
errno를 지역 변수/원본 입력에서 얻는다. 원본 생성자가 반환한 뒤 해당 owner의 이전
레코드를 지우고 새 typed 값을 게시한다. 원본 예외/args/identity를 수정하지 않는다.
일반 생산 경로에는 켜지지 않으며, 이것만으로 builtin/user 출처 분류를 확장하지 않는다.

closure는 저장소의 참조 ID를 순회해 MARK owner(또는 현재 emergency 세대)의 target만
root tracer에 전달한다. 테이블 전체를 root로 등록하지 않는다. sweep 뒤 죽은 owner의
슬롯을 제거한다. 성공한 gc_alloc과 동일 owner의 생성자 재사용은 이전 레코드를 지운다.
기존 한-edge 관찰 변수는 이 경로에서 게시하지 않아 숨은 우회 root가 되지 않게 한다.

`--compact-reference-loss`는 실제 free=0에서 4블록만 풀고 filename raise를 실행한다.
원래 OSError tuple이 빈 상태를 요구하며, GC 후 저장소만으로 원본 문자열 전체 바이트와
errno=5를 복원한다. 부가 mark 중 추가 gc_alloc=0이다. reporter는 아직 연결하지 않았다.
기존 filename 실험의 비interned reject는 별도 유지해 전체 요구 성공으로 세지 않는다.

`--compact-references`의 검증은 다음과 같다.

- 256개의 실제 empty-args OSError와 별도 동적 문자열을 생성하여 슬롯에 연결한다.
  실제 GC 후 모든 문자열의 독립 기대 바이트 및 errno 양 극값을 확인한다.
  여기서 대량 sidecar는 직접 연결하므로 256개의 실제 tuple 손실을 뜻하지 않는다.
- owner root를 전부 해제한 실제 GC 후 owner와 문자열의 allocation이 모두 0임을
  슬롯 제거 **전에** 확인하고, sweep 정리 뒤 참조 ID가 남지 않음을 확인한다.
- 바깥 make_new가 원본에서 반환한 뒤 게시하기 전, 안쪽 실제 make_new로 재진입한다.
  서로 다른 두 owner와 target·errno=5/-7이 모두 유지되어야 한다.
- keyword를 허용하지 않는 실제 생성자에 keyword=1을 전달해 실제 TypeError NLR을
  발생시킨다. 반환하지 않은 생성자는 레코드를 게시하지 않으며 기존 두 owner는 유지된다.
- 일반 owner를 실제 gc_free하고 같은 크기를 gc_alloc하여 **동일 주소**가 재사용되는
  것을 확인한다. wrapper가 옛 참조 ID를 제거해야 한다.
- 레코드의 로컬 snapshot을 얻은 뒤 underlying 슬롯을 제거해 복사값이 변하지 않음을
  확인한다. 이는 저장소 snapshot 검증이며 reporter 출력 중 GC의 안전성 검증은 아니다.

호스트 저장소 fixture의 `references` 모드는 65,536개 슬롯 + emergency를 모두 채운 뒤
각 target과 signed errno 양 극값을 독립 계산값으로 확인한다. invalid put/get·stale ID는
저장소/출력을 보존한다. 원본 full/invalid 모드도 계속 실행한다.

## 검증과 다음 경계

```sh
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_probe64.py --only micropython
```

통합 probe는 원래 회귀에 `references`와 `reference-loss` exact marker 검사를 추가한다.
증거는 `build64/feature-p0/mp-boundary/compact/{references,reference-loss}.log`다.
저장소 C는 양 cross compiler의 실제 strict port flags로 직접 컴파일한다.
추가 host 단위 테스트와 저장소 ASan/UBSan full/invalid/references도 실행한다.

다음은 **이 typed 후보를 reporter에 연결하고 출력 재진입/GC/NLR의 수명 경계 검증**이다.
특히 출력 sink가 GC하거나 emergency를 교체할 때 snapshot의 숫자 offset만으로는 문자열이
살아 있음을 보장하지 못한다. bounded reporting claim/임시 root의 범위와 예외 정리를
입증해야 한다. 이번 후보를 전체 출력 수명 문제의 해결로 판정하지 않는다.
이후 나머지 typed format·비매크로/REPL/traceback과 closure 비용·실기를 검증한다.
closure의 단일 heap/고정 layout 및 최악 제곱 비용 제약은 유지된다.

12차의 reporter 연결과 출력 중 scoped root/GC/NLR 결과는
[부가 참조 reporter](feature_p0_mp_reporter.md)에 기록한다.
