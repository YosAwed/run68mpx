#!/usr/bin/env python3
"""Run an X-file corpus with both run68mpx CPU backends."""

from __future__ import annotations

import argparse
import difflib
import hashlib
import shutil
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Result:
    backend: str
    returncode: int | None
    timed_out: bool
    elapsed: float
    stdout: bytes
    stderr: bytes

    @property
    def outcome(self) -> str:
        return "TIMEOUT" if self.timed_out else f"exit {self.returncode}"


def run_sample(run68: Path, sample: Path, backend: str, timeout: float) -> Result:
    # Human68k paths are limited to 88 characters. macOS's default temporary
    # directory under /var/folders is often already too long for run68.
    short_temp_root = "/tmp" if Path("/tmp").is_dir() else None
    with tempfile.TemporaryDirectory(
        prefix=f"run68mpx-{backend}-", dir=short_temp_root
    ) as temp:
        workdir = Path(temp)
        local_sample = workdir / sample.name
        shutil.copy2(sample, local_sample)
        command = [str(run68), f"--cpu={backend}", local_sample.name]
        started = time.monotonic()
        try:
            completed = subprocess.run(
                command,
                cwd=workdir,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=timeout,
                check=False,
            )
            return Result(
                backend,
                completed.returncode,
                False,
                time.monotonic() - started,
                completed.stdout,
                completed.stderr,
            )
        except subprocess.TimeoutExpired as error:
            return Result(
                backend,
                None,
                True,
                time.monotonic() - started,
                error.stdout or b"",
                error.stderr or b"",
            )


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()[:12]


def text(data: bytes) -> list[str]:
    return data.decode("utf-8", errors="backslashreplace").splitlines(keepends=True)


def show_diff(name: str, legacy: bytes, musashi: bytes) -> None:
    if legacy == musashi:
        return
    print(f"  {name} differs:")
    diff = difflib.unified_diff(
        text(legacy), text(musashi), fromfile="legacy", tofile="musashi", n=2
    )
    for index, line in enumerate(diff):
        if index == 40:
            print("    ... diff truncated ...")
            break
        print("    " + line.rstrip("\n"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run68", type=Path, required=True)
    parser.add_argument("--samples", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    run68 = args.run68.resolve()
    samples = sorted(args.samples.resolve().rglob("*.x"))
    if not run68.is_file():
        parser.error(f"run68 executable not found: {run68}")
    if not samples:
        parser.error(f"no .x files found under: {args.samples}")

    mismatches = 0
    print(f"run68: {run68}")
    print(f"samples: {len(samples)}, timeout: {args.timeout:.1f}s")
    for sample in samples:
        legacy = run_sample(run68, sample, "legacy", args.timeout)
        musashi = run_sample(run68, sample, "musashi", args.timeout)
        same = (
            legacy.returncode == musashi.returncode
            and legacy.timed_out == musashi.timed_out
            and legacy.stdout == musashi.stdout
            and legacy.stderr == musashi.stderr
        )
        status = "MATCH" if same else "DIFF"
        mismatches += not same
        relative = sample.relative_to(args.samples.resolve())
        print(f"\n[{status}] {relative}")
        for result in (legacy, musashi):
            print(
                f"  {result.backend:7} {result.outcome:8} {result.elapsed:5.2f}s "
                f"out={len(result.stdout):6}/{digest(result.stdout)} "
                f"err={len(result.stderr):6}/{digest(result.stderr)}"
            )
        if not same:
            show_diff("stdout", legacy.stdout, musashi.stdout)
            show_diff("stderr", legacy.stderr, musashi.stderr)

    print(f"\nsummary: {len(samples) - mismatches} match, {mismatches} differ")
    return 1 if mismatches else 0


if __name__ == "__main__":
    raise SystemExit(main())
