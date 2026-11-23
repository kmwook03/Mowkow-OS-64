# Mowkow mlibc adapter

고정 mlibc 6.3.1의 Mowkow 정적 링크 adapter다. Linux sysdeps와 legacy CRT를
사용하지 않는다. 소스 archive와 SHA256은
[`sources.json`](../../experiments/unix_p0/sources.json)을 따른다.
다운로드 방법은 [P0 실험](../../experiments/unix_p0/README.md)에 있다.

Meson, Ninja, Python 3.12+, `x86_64-elf-{gcc,g++,ar,strip}` 및
`aarch64-none-elf-{gcc,g++,ar,strip}`가 PATH에 있어야 한다. 외부 도구·원본은
저장소에 포함하지 않는다. 빌드는 오프라인이며 기존 work 디렉터리를 덮어쓰지 않는다.

```sh
PYTHONDONTWRITEBYTECODE=1 make unix64-mlibc \
  UNIX64_ARCHIVES=/tmp/mowkow-a1-archives \
  UNIX64_WORK=/tmp/mowkow-mlibc-x86 UNIX64_MLIBC_ARCH=x86_64
PYTHONDONTWRITEBYTECODE=1 make unix64-mlibc \
  UNIX64_ARCHIVES=/tmp/mowkow-a1-archives \
  UNIX64_WORK=/tmp/mowkow-mlibc-a64 UNIX64_MLIBC_ARCH=aarch64
PYTHONDONTWRITEBYTECODE=1 make test64-unix-libc \
  UNIX64_LIBC_SYSROOT=/tmp/mowkow-mlibc-x86/sysroot \
  UNIX64_A64_LIBC_SYSROOT=/tmp/mowkow-mlibc-a64/sysroot \
  UNIX_LIBC_LOG64=/tmp/mowkow-libc-ide.log
PYTHONDONTWRITEBYTECODE=1 make test64-unix-libc-ahci \
  UNIX64_LIBC_SYSROOT=/tmp/mowkow-mlibc-x86/sysroot \
  UNIX64_A64_LIBC_SYSROOT=/tmp/mowkow-mlibc-a64/sysroot \
  UNIX_LIBC_LOG64=/tmp/mowkow-libc-ahci.log
```

`build.log`에는 도구 버전과 실제 command/output을 남긴다. source와 build,
완성 sysroot는 지정한 work 아래에만 생성한다. `make check64`는 seed를 빌드하고,
이 선택적 외부 libc 빌드를 자동 실행하지 않는다. libc probe도 disposable image에만
설치한다. AArch64 probe는 cross-link만 검사하며 Pi 5 실행 결과를 뜻하지 않는다.

두 패치는 disposable upstream copy에만 적용한다. P0의 puts 패치는 newline
오류 판정을 고친다. `0001-bare-metal-fast-types.patch`는 `__mowkow__`에서
bare-metal GCC의 fast integer types와 libc 정의를 맞춘다. 버전·패치를 임의로
갱신하거나 최신 mlibc sysdeps 인터페이스를 이 6.3.1 포트에 섞지 않는다.

지원 범위는 단일 실행 흐름의 startup/exit, TLS/errno, allocation, console stdio,
read-only FAT root file, exec, PID, private futex와 monotonic clock이다. syscall
실패는 positive errno와 성공 out parameter라는 mlibc 계약으로 변환한다.
`sigaction=ENOSYS`로 upstream의 cancellation opt-out을 선택한다. pthread,
signal delivery/return, fork/wait, mmap/munmap, 일반 VFS를 제공하지 않는다.
8-MiB arena는 image 전체 수명에 매핑되며 free는 재사용을 뜻한다.
