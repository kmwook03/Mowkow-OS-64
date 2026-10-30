# P0 활성 format의 typed 인수·예산·실패 정책

2026-10-28, 기준 `e18d682` 위의 미커밋 시제품.
**154개 활성 메시지 전체를 분류했고, 기존 시제품 5개를 제외한 pending 149개를 유지한다.**
이 감사는 번역 추가나 생산 적용이 아니다. 기계 판독 목록은
[feature_p0_mp_types.json](feature_p0_mp_types.json)이다.

## 감사 방법과 독립 검증

`feature_mp_types64.py`는 기존 감사 도구로 양 아키텍처의 실제 공용 inventory·빌드 설정을
다시 전처리한다. 활성 macro 위치를 원본 소스의 유일한 MP_ERROR_TEXT와 대응시키고,
둘러싼 호출 및 뒤따르는 인수 표현식을 추출한다. 이 때문에 문자열 안의 `%`를 전부
varargs로 오해하지 않는다. `%c needs int or char`는 mp_raise_TypeError에 넘기는
**고정 문구**이며 인수를 소비하지 않는다. 동일 텍스트의 literal/formatted 혼용은 거부한다.

현재 활성 부분집합은 `%q/%s/%c/%d/%x/%lu`다. 검토하지 않은 conversion, 인수 개수 변경,
알 수 없는 `%s` 소유권은 감사 실패다. 동적 `%s` 인수는 모두 mp_obj_get_type_str 호출이며,
그 구현이 qstr_str(mp_obj_get_type(o_in)->name)인지도 확인한다. 이 목록의 타입명은 기존
qstr pool 소유다. 임시 문자열을 새로 intern하거나 영구 root로 올리지 않는다.
파일명 등 비매크로 동적 문자열까지 이 결론을 확대하지 않는다.

소비 타입은 현재 mp_vprintf 구현에서 확인했다. 별도로 원본을 변경하지 않는 격리 빌드
사본의 각 활성 표현식에 unevaluated `_Generic`을 넣고 `.p0types` section의 4바이트
식별 레코드를 읽는다. integer promotion을 적용한 **실제 producer C 타입**을 x86_64와
AArch64의 실제 compiler/flags로 모두 확인한다. 표현식은 평가하지 않으며 이 객체는
생산 링크에 들어가지 않는다. 타입 코드가 미지원이거나 레코드가 누락/중복되면 실패한다.
컴파일 명령/출력/객체/section은 ignored build64에 남긴다.

LP64와 qstr 64비트는 각 port의 strict flags 아래 static assertion으로 확인한다.
376개 architecture-tagged 활성 site, 136개 argument 기록을 검사했다. 기록된 producer는
int32 42개, ulong64 44개, long64 4개, const char* 46개다. 두 아키텍처의 중복을 포함한다.
원본 소스와 주요 타입/formatter header의 hash를 목록에 기록한다. `--check`는 실제
inventory와 compiler 결과를 다시 생성하여 문서 목록이 달라지면 실패한다.

## pending 149개 분류

| 저장 범위 | ABI 타입 일치 | ABI 정책 필요 | 합계 |
| --- | ---: | ---: | ---: |
| 인수 없음, ID만 필요 | 112 | 0 | 112 |
| 인수 원시 예산 <=64비트 | 21 | 6 | 27 |
| 인수 원시 예산 >64비트 | 2 | 8 | 10 |
| 합계 | 135 | 14 | 149 |

전체 154개에서는 ID-only 114개, <=64비트 30개, 초과 10개다. `fits_payload64`는
**예산 분류**이며 번역·capture·runtime 통과 표시가 아니다. qstr/type-name lookup은
안전하게 qstr64로 정규화한다고 가정하고, 숫자는 producer와 consumer 중 더 넓은 폭을
보존한 상한을 사용한다. 따라서 소비가 `%d`여도 실제 size_t/ptrdiff_t를 조용히 32비트로
줄여 저장 가능하다고 판정하지 않는다. 149개 translation은 모두 pending이다.

10개 초과 조합은 다음과 같다.

| 텍스트 요약 | 원시 비트 |
| --- | ---: |
| `%q indices ... %s` | 128 |
| `'%s' object has no attribute '%q'` | 128 |
| `can't convert '%q' object to %q implicitly` | 128 |
| `function expected at most %d ..., got %d` | 128 |
| `function takes %d ... but %d were given` | 128 |
| `type object '%q' has no attribute '%q'` | 128 |
| `unknown format code '%c' ... '%s'` | 96 |
| `unsupported format character '%c' (0x%x) at index %d` | 128 |
| `unsupported type for %q: '%s'` | 128 |
| `unsupported types for %q: '%s', '%s'` | 192 |

qstr ID의 실제 범위나 type->name의 16비트 필드, 동일 인수의 반복 등을 이용해 더 줄일
여지는 있다. 하지만 현재 저장소가 모든 조합을 지원한다는 뜻은 아니다. 검증된 범위 증명과
encoding이 필요하다. 모든 슬롯을 단순 192비트 payload로 늘리는 대안은 ID 포함 25바이트:
2MiB heap에서 1,638,481바이트로 후보 heap 30% 예산을 넘는다(출력 root 72바이트 별도).
공유 인수 pool도 최악 live 수·회수·고갈을 증명하기 전에는 무퇴출 해법으로 판정하지 않는다.

15차에서 이 10개의 [검사된 payload64 후보](feature_p0_mp_packing.md)를 검증했다.
type-name Q16, 일반 qstr Q32, signed/positive int32, 동일 인수 반복을 검사해 32~64비트에
저장한다. 원시 타입 예산 분류는 그대로이며 모든 64비트 입력을 수용한다는 뜻은 아니다.
범위 밖 인수는 incomplete이고, 실제 도달 가능성 판정·한국어 reporter 통합은 남는다.

## producer/consumer 불일치와 확정한 실패 정책

14개 ID에서 타입이 다르다. 예를 들어 uint16 type->name/arg.qst는 int로 promote되지만
`%q` 소비자는 qstr(size_t)을 읽는다. 인수 개수의 size_t와 포인터 차이의 ptrdiff_t가 `%d`
소비자의 int와 다른 곳도 있다. `%x`에 promote된 char/int가 전달되는 경우도 포함한다.
원본 upstream의 이러한 호출을 수정하지 않았다. 현재 ABI에서 관찰될 숫자와 C 타입
일치 여부를 혼동하지 않으며, 감사 결과만으로 런타임 출력이 틀렸다고 단정하지 않는다.

| 조건 | 정책 |
| --- | --- |
| 새 format/호출·conversion·소유권·producer type을 인식하지 못함 | 감사 실패, 누락 항목을 지우지 않음 |
| producer와 consumer가 다름 | producer 타입으로 전체 값을 보존; 호스트 대조 범위와 남은 실행/저장 검증은 ABI 문서 참조 |
| payload64를 넘음 또는 압축 범위를 넘음 | truncation·live eviction·강제 intern 금지; 새 encoding/범위 증명 전 미지원 유지 |
| 실행 중 미등록 ID/예산 고갈 | 기존 진단을 보존하되 incomplete로 기록; 영어 fallback을 전체 번역 성공으로 세지 않음 |
| 출력 depth 초과 또는 sink NLR | 기존 scoped-root 후보의 명시적 실패/정리 유지; 전체 지원 판정에 포함하지 않음 |

이 문서의 정책은 P0 설계/감사 계약이다. 생산 kernel에 새로운 오류 ABI나 fallback을
설치한 것이 아니다. 기존 파일·syscall·사용자 예외/print/str(exc) 동작도 바꾸지 않는다.

## 범위 밖 경계와 다음 작업

기존 macro catalog의 제외 범위는 그대로다. 실제 filename·errno/type-only·qstr 예외,
사용자 문자열과 동적 메시지는 별도 구조화 출처가 필요하다. traceback의 고정 문구는
py/obj.c의 mp_obj_print_exception에 있고 파일 실행(port mpport_main.c), REPL(pyexec.c),
명시적 sys.print_exception(modsys.c)이 같은 출력 함수로 들어간다. 이들의 전체 번역/
원본 예외 args·사용자 데이터 보존은 이번 typed 분류로 검증하지 않았다.

14개 불일치 ID/16개 실제 생성 경로의 호스트 formatter·args 대조와 producer 타입별
capture 정책 검증은 [ABI 실험](feature_p0_mp_abi.md)에 기록한다. manifest의 불일치 표시는
실제 C 타입 차이를 나타내므로 유지한다. 영속 저장·번역·AArch64 실행 완료 표시는 아니다.

다음은 **압축 범위 밖 인수의 실제 도달 가능성 판정과 공통 reporter 통합**이다.
검사된 인코딩의 성공을 전체 producer 범위 증명으로 세지 않고 전체 메시지/비매크로/
traceback·REPL 경계로 확장한다. 전체 vendor 무수정 가능 여부·closure 비용·출력 catalog·
생산 적용·QEMU·Pi 5는 여전히 남는다. P0 전체 미완료다.

## 재현

```sh
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_mp_types64.py \
  --check docs/feature_p0_mp_types.json
PYTHONDONTWRITEBYTECODE=1 make -j4 check64
PYTHONDONTWRITEBYTECODE=1 python3 64bit/tools/feature_probe64.py --only micropython
```

통합 probe도 typed 목록의 최신 상태를 검사하고 summary를 report.json에 연결한다.
새 host 테스트는 lexical 문맥·원문 연산자 보존·literal percent·192비트 조합·미지원/혼용 거부,
실제 host compiler의 promotion/const char* section 기록과 미지원 double 검출을 확인한다.
교차 C typing은 실행 검증이나 OS hardware 검증이 아니다.
