#!/usr/bin/env python3
"""Verify pinned nano files, then patch a disposable build copy only."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VENDOR = ROOT / "third_party" / "nano"
PATCH = ROOT / "64bit/nano64/patches/0001-freestanding-boundaries.patch"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(source: Path, destination: Path, patch: Path) -> None:
    """Check all inputs before replacing any existing generated source tree."""
    manifest = json.loads((source / "source.json").read_text())
    for name, expected in manifest["files"].items():
        relative = Path(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"unsafe source manifest path: {name}")
        if digest(source / relative) != expected:
            raise ValueError(f"nano source hash mismatch: {name}")
    signature = {"manifest": digest(source / "source.json"), "patch": digest(patch),
                 "preparer": digest(Path(__file__))}
    marker = destination / ".prepared.json"
    if marker.exists() and json.loads(marker.read_text()) == signature:
        marker.touch()
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nano-stage64-", dir=destination.parent) as temporary:
        stage = Path(temporary) / "source"
        stage.mkdir()
        for name in manifest["files"]:
            target = stage / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / name, target)
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch.resolve())],
                       cwd=stage, check=True, timeout=30)
        (stage / ".prepared.json").write_text(json.dumps(signature, indent=2) + "\n")
        if destination.exists():
            shutil.rmtree(destination)
        stage.rename(destination)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    destination = args.output.resolve()
    if destination == ROOT / "build64" or not destination.is_relative_to(ROOT / "build64"):
        parser.error("output must be a disposable directory under build64")
    prepare(VENDOR, destination, PATCH)


if __name__ == "__main__":
    main()
