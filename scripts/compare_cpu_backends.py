#!/usr/bin/env python3
"""Run an X-file corpus with both run68mpx CPU backends."""

from __future__ import annotations

import argparse
import csv
import difflib
import hashlib
import json
import shutil
import subprocess
import tempfile
import time
from dataclasses import asdict, dataclass
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


@dataclass
class SampleReport:
    path: str
    status: str
    category: str
    legacy: dict
    musashi: dict


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


def classify_diff(legacy: Result, musashi: Result) -> str:
    if legacy.timed_out and musashi.timed_out:
        return "both_timeout"
    if legacy.timed_out != musashi.timed_out:
        return "timeout_mismatch"
    if legacy.returncode != musashi.returncode:
        return "exit_code"
    if legacy.stdout != musashi.stdout and legacy.stderr != musashi.stderr:
        return "stdout_stderr"
    if legacy.stdout != musashi.stdout:
        return "stdout"
    if legacy.stderr != musashi.stderr:
        return "stderr"
    return "match"


def result_summary(result: Result) -> dict:
    return {
        "backend": result.backend,
        "returncode": result.returncode,
        "timed_out": result.timed_out,
        "elapsed": round(result.elapsed, 3),
        "stdout_len": len(result.stdout),
        "stderr_len": len(result.stderr),
        "stdout_digest": digest(result.stdout),
        "stderr_digest": digest(result.stderr),
    }


def load_exclude_list(path: Path | None) -> set[str]:
    if path is None:
        return set()
    excluded: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        text_line = line.strip()
        if not text_line or text_line.startswith("#"):
            continue
        excluded.add(text_line)
    return excluded


def collect_samples(root: Path, include_r: bool) -> list[Path]:
    patterns = ["*.x", "*.X"]
    if include_r:
        patterns.extend(["*.r", "*.R"])
    samples: list[Path] = []
    for pattern in patterns:
        samples.extend(root.rglob(pattern))
    return sorted(set(samples))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run68", type=Path, required=True)
    parser.add_argument("--samples", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--include-r", action="store_true",
                        help="Also compare .r files")
    parser.add_argument("--exclude-file", type=Path,
                        help="Text file of relative paths to skip")
    parser.add_argument("--json-out", type=Path,
                        help="Write machine-readable summary JSON")
    parser.add_argument("--csv-out", type=Path,
                        help="Write summary CSV")
    parser.add_argument("--fail-list", type=Path,
                        help="Write relative paths that DIFF/TIMEOUT")
    args = parser.parse_args()

    run68 = args.run68.resolve()
    sample_root = args.samples.resolve()
    samples = collect_samples(sample_root, args.include_r)
    excluded = load_exclude_list(
        args.exclude_file.resolve() if args.exclude_file else None
    )
    if not run68.is_file():
        parser.error(f"run68 executable not found: {run68}")
    if not samples:
        parser.error(f"no sample files found under: {args.samples}")

    reports: list[SampleReport] = []
    mismatches = 0
    skipped = 0
    print(f"run68: {run68}")
    print(
        f"samples: {len(samples)}, timeout: {args.timeout:.1f}s, "
        f"include_r={args.include_r}, excluded={len(excluded)}"
    )
    for sample in samples:
        relative = sample.relative_to(sample_root).as_posix()
        if relative in excluded or sample.name in excluded:
            skipped += 1
            print(f"\n[SKIP] {relative}")
            continue

        legacy = run_sample(run68, sample, "legacy", args.timeout)
        musashi = run_sample(run68, sample, "musashi", args.timeout)
        category = classify_diff(legacy, musashi)
        same = category == "match"
        status = "TIMEOUT" if category == "both_timeout" else (
            "MATCH" if same else "DIFF"
        )
        mismatches += not same
        reports.append(
            SampleReport(
                relative,
                status,
                category,
                result_summary(legacy),
                result_summary(musashi),
            )
        )
        print(f"\n[{status}/{category}] {relative}")
        for result in (legacy, musashi):
            print(
                f"  {result.backend:7} {result.outcome:8} {result.elapsed:5.2f}s "
                f"out={len(result.stdout):6}/{digest(result.stdout)} "
                f"err={len(result.stderr):6}/{digest(result.stderr)}"
            )
        if not same:
            show_diff("stdout", legacy.stdout, musashi.stdout)
            show_diff("stderr", legacy.stderr, musashi.stderr)

    matched = len(reports) - mismatches
    print(
        f"\nsummary: {matched} match, {mismatches} differ, "
        f"{skipped} skipped, {len(reports)} compared"
    )

    payload = {
        "run68": str(run68),
        "samples": str(sample_root),
        "timeout": args.timeout,
        "include_r": args.include_r,
        "matched": matched,
        "differ": mismatches,
        "skipped": skipped,
        "results": [asdict(report) for report in reports],
    }
    if args.json_out:
        args.json_out.write_text(
            json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
    if args.csv_out:
        with args.csv_out.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(
                [
                    "path",
                    "status",
                    "category",
                    "legacy_outcome",
                    "musashi_outcome",
                    "legacy_stdout",
                    "musashi_stdout",
                    "legacy_stderr",
                    "musashi_stderr",
                ]
            )
            for report in reports:
                writer.writerow(
                    [
                        report.path,
                        report.status,
                        report.category,
                        (
                            "TIMEOUT"
                            if report.legacy["timed_out"]
                            else report.legacy["returncode"]
                        ),
                        (
                            "TIMEOUT"
                            if report.musashi["timed_out"]
                            else report.musashi["returncode"]
                        ),
                        report.legacy["stdout_digest"],
                        report.musashi["stdout_digest"],
                        report.legacy["stderr_digest"],
                        report.musashi["stderr_digest"],
                    ]
                )
    if args.fail_list:
        fails = [report.path for report in reports if report.status != "MATCH"]
        args.fail_list.write_text("\n".join(fails) + ("\n" if fails else ""),
                                  encoding="utf-8")

    return 1 if mismatches else 0


if __name__ == "__main__":
    raise SystemExit(main())
