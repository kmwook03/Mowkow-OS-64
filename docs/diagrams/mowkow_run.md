# 머꼬 실행환경

64비트 머꼬는 콘솔 태스크에서 커널에 링크된 MicroPython VM을 통해 실행된다.
`머꼬`는 대화형 REPL, `머꼬 add.mk`는 파일 실행으로 진입한다.
x86_64와 AArch64는 아래 실행 경로를 공유하며, 저장 장치와 키 입력의
플랫폼별 차이는 커널 인터페이스 뒤에서 처리한다.

MicroPython은 머꼬를 구현한 Python 모듈을 바이트코드로 실행하고,
머꼬의 `_parse.py`와 `_eval.py`가 머꼬 표현식을 읽고 평가한다.
`mowio`와 MicroPython HAL은 콘솔·파일시스템으로 이어지는 C 구현이다.

## 세션 진입과 환경 수명

VM의 첫 초기화와 머꼬 기본 환경의 첫 생성은 별도 조건이다.
앞서 `py` 명령으로 VM이 초기화되었어도, 머꼬를 처음 실행하면
`library_kor.scm`을 평가하여 기본 환경을 만든다.

```mermaid
sequenceDiagram
    autonumber
    participant C as 콘솔 태스크
    participant P as mpport_main.c
    participant V as MicroPython VM / GC
    participant M as 머꼬 Python 모듈
    participant I as mowio / 파일 포트
    participant S as FAT32 / 블록 장치

    C->>C: console_dispatch_command64() — 머꼬 또는 머꼬 FILE
    C->>P: mpport_run_mowkow(NULL 또는 파일명)
    P->>P: mpport_session64(MPPORT_MOWKOW64, arg)
    P->>P: mpport_claim64() — IRQ-save로 단일 VM 점유 확인
    alt 다른 Python·머꼬 세션이 실행 중
        P->>C: 사용 중 메시지 출력, 요청 거절
        Note over P,V: 기존 세션의 VM·인수·점유 상태 유지
        P-->>C: 명령 반환
    else 점유 성공
        P->>P: 호출자의 thread 상태 저장, GC 스택 anchor 확보, NLR 설정
        P->>P: mpport_init64() — 현재 태스크의 스택 범위 검증·등록
        opt VM이 아직 준비되지 않음
            P->>P: memman64에서 4 MiB GC 힙 확보·재사용
            P->>V: gc_init(), mp_init()
            P->>P: mp_init 성공 후 mp_ready = 1
        end
        Note over P,V: 준비된 VM은 다시 초기화하지 않음<br/>초기화 실패·예외는 실행을 건너뛰고 공통 정리로 이동

        opt VM 초기화·재사용 성공
            P->>P: mpport_execute64() — 인수를 전용 버퍼에 복사
            P->>V: 시작 코드의 lexer 생성
            Note over P,V: 시작 코드: import mowkow → mowkow.main()
            P->>V: mp_parse() → mp_compile() → mp_call_function_0()
            opt import 대상 Python 모듈이 아직 캐시되지 않음
                V->>P: mp_import_stat() — 루트의 모듈 파일 확인
                P->>S: fd64_open()
                S-->>P: 파일 존재 여부
                V->>I: mp_lexer_new_from_file() — 모듈 소스 요청
                I->>S: mpport_load_file() — fd64_open(), 정확한 크기로 read
                S-->>I: Python 소스 바이트
                I-->>V: 소스 버퍼를 소유하는 lexer / reader
                V->>V: 모듈 컴파일·실행, import 캐시에 보관
                Note over V,M: mowkow.py와 _data·_parse·_eval·_error·_compat<br/>mowio는 커널에 등록된 내장 모듈
            end

            V->>M: mowkow.main()
            M->>M: base_env()
            opt _base가 아직 없음
                M->>M: mkenv(nil), 내장 함수와 #참 등록
                M->>M: load_file(env, library_kor.scm)
                M->>I: YY_reader.readfile() → mowio.readfile()
                I->>S: mpport_file_text64() → mpport_load_file()
                S-->>I: 라이브러리 파일 전체 바이트
                I-->>M: Python 문자열, 임시 소스 버퍼 해제
                M->>M: 표현식별 read_expr() → do_eval(expr, env)
                M->>M: 완성된 기본 환경을 _base에 보관
            end
            M->>M: env = mkenv(base_env()) — 세션 전용 환경 생성
            M->>I: mowio.argv()
            I->>P: mpport_argv()
            P-->>I: None 또는 복사해 둔 파일명
            I-->>M: None 또는 Python 문자열
            Note over M,S: REPL 또는 파일의 표현식 평가 — 다음 다이어그램
            M-->>V: main() 반환 또는 미처리 예외
            V-->>P: 실행 완료 또는 NLR 예외
            opt 미처리 예외가 있음
                P->>P: mpport_report64() — 점유를 유지한 채 오류 보고
                P->>C: 예외 출력, KeyboardInterrupt·SystemExit는 줄바꿈만 출력
            end
        end

        P->>P: mpport_finish64() — 인수 상태 정리
        Note over P,V: 성공한 VM·힙·모듈·기본 환경 유지<br/>첫 VM 초기화 실패 시 thread·VM·GC 복구, 힙 해제 시도
        P->>P: mpport_release64() — 정리가 끝난 뒤 점유 해제
        P-->>C: 명령 반환
    end
    C->>C: console_prompt64() — OS 명령 입력 재개
```

`mp_busy`는 실행·예외 출력·정리 전체를 직렬화한다. IRQ를 끈 상태는
점유 플래그를 변경하는 짧은 구간에 한정되며, 다른 태스크와 GUI는 계속 동작한다.
현재 세션의 환경은 기본 환경을 부모로 참조한다. 다음 실행은 새 환경을 만들므로
이전 세션에서 추가한 정의를 그대로 이어받지 않는다.

첫 VM 초기화 실패 시에는 부분 초기화 상태를 폐기한다. GC 힙 해제가 실패하면
소유권을 유지하여 재시도 때 재사용한다. 성공한 VM에서 발생한 실행 예외는
세션을 정리한 뒤 VM을 유지한다.

## REPL·파일 입력과 표현식 평가

아래 다이어그램은 `main()`이 세션 환경을 만든 뒤의 흐름이다.
`load_file()`은 기본 라이브러리 적재와 사용자 파일 실행에서 함께 사용한다.

```mermaid
sequenceDiagram
    autonumber
    participant M as mowkow.py
    participant R as _parse.py / YY_reader
    participant E as _eval.py / 환경
    participant I as mowio / 파일 포트
    participant S as FAT32
    participant V as MicroPython / 출력 HAL
    participant C as 호출 태스크의 콘솔

    alt 인수 없음 — 대화형 REPL
        M->>M: eval_print_loop(env)
        loop 빈 입력·EOF·세션 종료 예외 전까지
            M->>R: resetpos(), read()
            R->>I: _compat.readline() → mowio.readline(프롬프트)
            I->>C: 프롬프트 출력, console64_read_line()
            C->>C: 키 FIFO 대기·줄 편집·한글 조합
            C-->>I: 입력 바이트 또는 Ctrl-C / Ctrl-D 상태
            I-->>R: Python 문자열 또는 KeyboardInterrupt / EOFError
            R-->>M: 입력 문자열 — Reader.read()의 EOF는 빈 문자열로 변환
            alt 빈 문자열 또는 EOFError
                M->>V: eprint() — 종료 안내
                V->>C: mp_hal_stdout_tx_strn() → console64_write()
                Note over M,C: REPL 루프를 끝내고 공통 세션 정리로 복귀
            else 평가할 입력이 있음
                M->>R: next_token(), read_expr()
                opt 열린 괄호 등으로 추가 입력이 필요
                    R->>I: read2() → mowio.readline(이어쓰기 프롬프트)
                    I->>C: console64_read_line()
                    C-->>I: 다음 입력 줄
                    I-->>R: Python 문자열
                end
                R-->>M: 머꼬 Data 표현식
                M->>E: do_eval(expr, env)
                E->>E: 환경 조회·정의, 특수 형식 처리, 함수·클로저·매크로 적용
                E-->>M: 값 또는 ErrLisp / SyntaxError
                alt 정상 평가
                    opt 결과가 None이 아님
                        M->>V: print(val)
                        V->>C: mp_hal_stdout_tx_strn() → console64_write()
                    end
                else 처리 가능한 머꼬·구문 오류
                    M->>V: report_error() 또는 report_syntax()
                    V->>C: 오류 출력 후 다음 입력으로 진행
                end
            end
        end
    else 파일 인수 있음 — 예: add.mk
        M->>M: load_file(env, path)
        M->>R: YY_reader.readfile(path)
        R->>I: slurp() → mowio.readfile(path)
        I->>I: mpport_file_text64() → mpport_load_file()
        I->>S: fd64_open(), 파일 크기만큼 fd64_read()
        S-->>I: 파일 전체 바이트
        I->>V: mp_obj_new_str() — 문자열 복사
        V-->>I: Python 문자열
        I->>I: 임시 소스 버퍼 해제
        I-->>R: 파일 내용 문자열
        R->>R: 입력 내용 설정, 위치 초기화
        M->>R: next_token()
        loop YY_reader.remains()에 내용이 남아 있음
            M->>R: read_expr()
            R-->>M: 머꼬 Data 표현식
            M->>E: do_eval(expr, env)
            E-->>M: 값 또는 ErrLisp / SyntaxError
            alt 정상 평가
                opt 결과가 None이 아님
                    M->>V: eprint(result)
                    V->>C: HAL → console64_write()
                end
            else 처리 가능한 머꼬·구문 오류
                M->>V: report_error(err, path, YY_reader.line())
                V->>C: 파일·행 정보와 오류 출력
            end
        end
        M->>M: load_file() 반환
    end
    Note over M,V: 미처리 예외와 KeyboardInterrupt·SystemExit는<br/>세션 NLR 처리 및 공통 정리로 이동
```

머꼬 REPL의 입력은 `mowio.readline()`으로 콘솔 줄 편집기를 호출한다.
`py` 명령의 MicroPython REPL 입력 큐와는 별도 경로이며, 한글 조합·편집은
호출한 콘솔의 상태를 따른다. 일반 `print()`와 `_compat.eprint()` 출력은
MicroPython HAL을 거쳐 같은 콘솔로 전달된다.

파일 로딩은 전체 크기를 정확히 읽은 뒤 문자열이나 lexer에 넘긴다.
파일 열기 실패는 `ENOENT`, 읽기 크기 불일치는 `EIO` 예외로 전달하며,
문자열·lexer 생성 도중 예외가 발생해도 임시 소스와 reader를 정리한다.

## 구현 위치

| 역할 | 소스 |
| --- | --- |
| 명령 분기·콘솔 줄 편집 | [console_command64.c](../../64bit/src64/kernel/console_command64.c), [console_editor64.c](../../64bit/src64/kernel/console_editor64.c) |
| 단일 VM 점유·초기화·실행·정리 | [mpport_main.c](../../64bit/src64/mpport/mpport_main.c) |
| 파일 버퍼·lexer·문자열 소유권 | [mpport_file64.c](../../64bit/src64/mpport/mpport_file64.c) |
| 콘솔·파일·인수 연결, 표준 출력 HAL | [modmowio.c](../../64bit/src64/mpport/modmowio.c), [mphalport.c](../../64bit/src64/mpport/mphalport.c) |
| 머꼬 진입점·기본 환경·REPL·파일 실행 | [mowkow.py](../../64bit/py64/머꼬/mowkow.py) |
| 표현식 파싱·평가·데이터·오류 | [_parse.py](../../64bit/py64/머꼬/_parse.py), [_eval.py](../../64bit/py64/머꼬/_eval.py), [_data.py](../../64bit/py64/머꼬/_data.py), [_error.py](../../64bit/py64/머꼬/_error.py) |
| MicroPython 호환 함수·머꼬 기본 라이브러리 | [_compat.py](../../64bit/py64/머꼬/_compat.py), [library_kor.scm](../../64bit/py64/머꼬/library_kor.scm) |
| 플랫폼별 Python·머꼬 파일 배치 | [x86_64.mk](../../64bit/mk/x86_64.mk), [aarch64.mk](../../64bit/mk/aarch64.mk) |

실제 실행 모듈은 `64bit/py64/머꼬/`의 사본이다. 빌드는 이 파일들을
FAT32 루트에 평평하게 배치하며, `upstream/`은 동작 비교용 참조로 유지한다.
