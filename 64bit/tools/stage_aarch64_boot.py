#!/usr/bin/env python3
"""Build an exact Raspberry Pi boot-partition staging directory."""

import argparse
import ctypes
import errno
import hashlib
import os
import shutil
import sys
import tempfile
from pathlib import Path


# Linux renameat2 exchanges two nonempty directories in one commit. A two-step
# backup/rename would leave the public staging path missing on interruption.
AT_FDCWD = -100
RENAME_EXCHANGE = 2


def parse_spec(spec: str) -> tuple[str, Path]:
    if "=" not in spec:
        raise ValueError("expected NAME=SOURCE: " + spec)
    name, source = spec.split("=", 1)
    if not name or Path(name).name != name or name in (".", ".."):
        raise ValueError("destination must be one file name: " + name)
    return name, Path(source)


def payload_digest(path: Path) -> bytes:
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            checksum.update(chunk)
    return checksum.digest()


def validate_staging(staging: Path, expected: dict[str, bytes]) -> None:
    actual = {path.name for path in staging.iterdir()}
    if actual != expected.keys():
        raise ValueError("staging manifest mismatch")
    for name, checksum in expected.items():
        path = staging / name
        if path.is_symlink() or not path.is_file() or payload_digest(path) != checksum:
            raise ValueError("staging payload mismatch: " + name)
        with path.open("rb") as stream:
            os.fsync(stream.fileno())


def exchange_directories(staging: Path, output: Path) -> None:
    """Fail without removing either tree if atomic exchange is unsupported."""
    try:
        renameat2 = ctypes.CDLL(None, use_errno=True).renameat2
    except AttributeError as error:
        raise OSError(errno.ENOSYS, "atomic staging exchange requires renameat2") from error
    renameat2.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int,
                          ctypes.c_char_p, ctypes.c_uint]
    renameat2.restype = ctypes.c_int
    if renameat2(AT_FDCWD, os.fsencode(staging), AT_FDCWD,
                 os.fsencode(output), RENAME_EXCHANGE) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), str(output))


def stage_boot(output: Path, specs: list[str]) -> None:
    if output.name in ("", ".", ".."):
        raise ValueError("refusing unsafe output directory")
    if output.is_symlink() or (output.exists() and not output.is_dir()):
        raise ValueError("output must be a directory, not a symlink or file")
    entries = []
    expected = {}
    for spec in specs:
        name, source = parse_spec(spec)
        if name in expected:
            raise ValueError("duplicate destination: " + name)
        if not source.is_file():
            raise ValueError("missing source payload: " + str(source))
        entries.append((name, source))
        expected[name] = payload_digest(source)

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    try:
        staging = temporary / "tree"
        staging.mkdir()
        for name, source in entries:
            shutil.copy2(source, staging / name)
        validate_staging(staging, expected)
        if output.exists():
            exchange_directories(staging, output)
            # The former output is now staging and can be retired after commit.
        else:
            os.replace(staging, output)
    finally:
        try:
            shutil.rmtree(temporary)
        except OSError as error:
            print(f"staging temporary cleanup failed: {temporary}: {error}", file=sys.stderr)
    print("AArch64 boot staging: {} files -> {}".format(len(entries), output))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("files", nargs="+", metavar="NAME=SOURCE")
    args = parser.parse_args()
    try:
        stage_boot(args.output, args.files)
    except ValueError as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
