# GNU nano

`나노` 실행 파일은 `64bit/nano64/`의 private 포트와 GNU nano 9.3 편집 코어로
빌드한다. `64bit/mk/common64.mk`가 공용 소스를, `nano64.mk`가 양 아키텍처의
검증된 vendor 복사·패치·링크와 기존 앱 이름으로의 설치를 관리한다.

원본·라이선스는 `third_party/nano/`, 지원 범위와 검증은
[`docs/feature_p4.md`](../../../docs/feature_p4.md)에 있다.
