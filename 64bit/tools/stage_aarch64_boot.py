#!/usr/bin/env python3
"""Build an exact Raspberry Pi boot-partition staging directory."""

import argparse
import shutil
from pathlib import Path


def parse_spec(spec):
    if "=" not in spec:
        raise ValueError("expected NAME=SOURCE: " + spec)
    name, source = spec.split("=", 1)
    if not name or Path(name).name != name or name in (".", ".."):
        raise ValueError("destination must be one file name: " + name)
    return name, Path(source)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("files", nargs="+", metavar="NAME=SOURCE")
    args = parser.parse_args()

    output = args.output
    if output.name in ("", ".", ".."):
        parser.error("refusing unsafe output directory")

    entries = []
    names = set()
    try:
        for spec in args.files:
            name, source = parse_spec(spec)
            if name in names:
                raise ValueError("duplicate destination: " + name)
            if not source.is_file():
                raise ValueError("missing source payload: " + str(source))
            entries.append((name, source))
            names.add(name)
    except ValueError as exc:
        parser.error(str(exc))

    staging = output.with_name(output.name + ".tmp")
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    try:
        for name, source in entries:
            shutil.copy2(source, staging / name)

        actual = {path.name for path in staging.iterdir() if path.is_file()}
        missing = names - actual
        extra = actual - names
        if missing or extra:
            details = []
            if missing:
                details.append("missing: " + ", ".join(sorted(missing)))
            if extra:
                details.append("extra: " + ", ".join(sorted(extra)))
            raise RuntimeError("staging manifest mismatch (" + "; ".join(details) + ")")

        if output.exists():
            shutil.rmtree(output)
        staging.replace(output)
    except Exception:
        if staging.exists():
            shutil.rmtree(staging)
        raise

    print("AArch64 boot staging: {} files -> {}".format(len(entries), output))


if __name__ == "__main__":
    main()
