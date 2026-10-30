# P1 머꼬 기본 프롬프트 변경과 검증

검증일: 2026-10-30. 기준 커밋: `5c2f4af23d034091a51da34da9f30c609b3e6c7e` 위
미커밋 작업. P1 완료이며 다음 단계는 P2다.

## 변경 범위와 UI 차이

`64bit/py64/머꼬/_parse.py`의 `Reader._prompt1`만 `>  `에서 `>> `로 변경했다.
기존 CRLF를 보존했으며 32비트 트리·머꼬 upstream·MicroPython vendor는 수정하지 않았다.

| 프롬프트 | 64비트 포팅본 | 호스트 reference |
| --- | --- | --- |
| 머꼬 기본 입력 | `>> ` | `>  ` 유지 |
| 머꼬 연속 입력 | `.. ` + 중첩 깊이만큼 공백 | 동일 |
| OS 명령 입력 | `> ` 유지 | 해당 없음 |
| MicroPython friendly REPL | `>>> ` 유지 | 해당 없음 |

기본 프롬프트만 의도한 UI 차이이며 식 평가와 종료 동작은 동일하다.

## 반복 검증 경로

`64bit/tools/mowkow_parity.py`의 REPL 검사는 `0육ㄱ`, `(+ 1`, `2)`, `'>`, 빈 줄을
입력한다. 정규화 전에 다음 시리얼 원문을 검사하고, 별도로 reference와 평가 결과를 비교한다.
한글 입력은 실제 시리얼의 완성된 되울림을 기대값으로 사용한다.

```text
>> 0육ㄱ
10
>> (+ 1
..  2)
3
>> '>
>
>>
간편한 한글 프로그래밍 언어 '머꼬'를 사용해 주셔서 고맙습니다.
>
```

위 예에서 `.. ` 뒤의 추가 공백 하나는 기존 중첩 들여쓰기다.
빈 줄 입력과 OS 복귀 프롬프트 끝의 공백 한 칸은 위 표시에서 생략했다.
reference 원문은 `>  10\n>  ..  3\n>  >\n>  `로 시작한다.
양쪽의 기본 프롬프트가 실제로 다른지 확인하므로 정규화만으로 변경을 숨길 수 없다.

정규화는 생산자별 정확한 프롬프트와 그 공백만 제거한 뒤 줄을 정리한다.
기존의 `lstrip(">.")`를 제거하여 `>`, `>>`, `>=`, `..` 등의 값이 사라지지 않게 했다.
파일 실행 결과에는 REPL 프롬프트 제거를 적용하지 않아 프롬프트와 같은 모양의 출력도 보존한다.
OS 프롬프트 대기는 줄 시작을 확인하여 `>> `나 `>>> `의 끝을 OS 복귀로 오인하지 않는다.

기존 `test_qemu_console64.py`에 원문 프롬프트 누락·변경 거부, 기호 출력 보존,
OS/해석기 프롬프트 구분 검사를 추가했다. 새 runner는 만들지 않았다.

## 검증 결과

| 경로 | 결과 |
| --- | --- |
| `make check64` | PASS: style 290개 파일, 호스트 104개, 양 아키텍처·private nano 증분 빌드 |
| x86_64 IDE parity | PASS: 12개, 원문 `>> `·`.. `, 반복 입력·빈 줄·Ctrl-C/Ctrl-D·파일 실행 |
| x86_64 AHCI parity | PASS: 동일 12개와 원문 검사 |
| AArch64 staging | PASS: 배치된 `_parse.py`와 포팅본의 바이트 일치 |
| Pi 5 실기기 | 미실행 |

IDE와 AHCI는 별도 일회용 이미지로 실행했다. 두 이미지와 `img64/mowkow64.img`의
SHA-256은 `d2657a98d884e78c5d568f8e649dc2e1ac0281e007c291848e3f4cd5dc4b031f`다.
별도 `test64-qemu` 앱·저장 회귀와 GUI 화면 검증은 실행하지 않았다.
이번 변경에는 빌드 규칙·생성 헤더·링커 변경이 없으며 clean build는 수행하지 않았다.

재현 명령(저장소 루트, 빌드 후 일회용 이미지 준비):

```sh
PYTHONDONTWRITEBYTECODE=1 make check64
cp img64/mowkow64.img /tmp/mowkow-feature-p1-ide.img
cp img64/mowkow64.img /tmp/mowkow-feature-p1-ahci.img
python3 64bit/tools/mowkow_parity.py --image /tmp/mowkow-feature-p1-ide.img \
    --log /tmp/mowkow-feature-p1-ide-serial.log
python3 64bit/tools/mowkow_parity.py --ahci --image /tmp/mowkow-feature-p1-ahci.img \
    --log /tmp/mowkow-feature-p1-ahci-serial.log
```

검증 로그는 `/tmp/mowkow-feature-p1-check64.log`,
`/tmp/mowkow-feature-p1-parity-ide.log`, `/tmp/mowkow-feature-p1-parity-ahci.log`와
위 두 시리얼 로그에 남겼다. 각 시리얼 경로의 `.qemu.log`에는 QEMU 진단을 남겼다.
이미지와 빌드 산출물은 소스 변경에 포함하지 않는다.
