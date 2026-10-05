from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess
import sys
from datetime import datetime, timezone


TARGETS = {
    "fuzz_hash": (164981, 65536),
    "fuzz_stream": (271828, 65536),
    "fuzz_padding": (314159, 4096),
    "fuzz_tree_combine": (141421, 4096),
}
LENGTHS = (
    0, 1, 3, 54, 55, 56, 63, 64, 65, 118, 119, 120, 127, 128, 129,
    1014, 1015, 1016, 1023, 1024, 1025, 2038, 2039, 2040,
    2047, 2048, 2049, 3071, 3072, 3073, 4095, 4096, 4097,
    8191, 8192, 8193, 16383, 16384, 16385, 32767, 32768, 32769,
    65535, 65536,
)


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def seed_corpus(directory: Path, maximum: int) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    for length in LENGTHS:
        if length > maximum:
            continue
        for pattern in ("zero", "ff", "counter"):
            data = (
                bytes(length) if pattern == "zero" else
                b"\xff" * length if pattern == "ff" else
                bytes(i & 255 for i in range(length))
            )
            (directory / f"seed-{length:05d}-{pattern}").write_bytes(data)


def corpus_manifest(directory: Path) -> list[dict[str, object]]:
    return [
        {"name": path.name, "bytes": path.stat().st_size, "sha256": file_hash(path)}
        for path in sorted(directory.iterdir()) if path.is_file()
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description="Seeded FCH libFuzzer campaigns")
    parser.add_argument("--mode", choices=("smoke", "campaign"), default="smoke")
    parser.add_argument("--build-dir", type=Path, default=Path.cwd())
    parser.add_argument("--runs", type=int, default=1024)
    parser.add_argument("--seconds", type=int, default=0)
    parser.add_argument("--timeout", type=int, default=10)
    parser.add_argument("--compiler", default="clang")
    parser.add_argument("--cflags", default="")
    args = parser.parse_args()
    if args.runs < 0 or args.seconds < 0 or args.timeout <= 0:
        parser.error("runs/seconds must be nonnegative and timeout positive")
    if args.runs == 0 and args.seconds == 0:
        parser.error("a finite run or time budget is required")
    build = args.build_dir.resolve()
    repository = Path(__file__).resolve().parent.parent
    if not build.is_relative_to(repository / "build"):
        parser.error("build-dir must be within the repository build directory")
    binaries = {name: build / name for name in TARGETS}
    if not all(path.is_file() for path in binaries.values()):
        parser.error("build all four existing fuzz targets first")
    root = build / "fuzz" / args.mode
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    results = root / "results" / stamp
    results.mkdir(parents=True)
    document: dict[str, object] = {
        "schema": "fch-fuzz-campaign-v1",
        "started_utc": stamp,
        "revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=repository, text=True
        ).strip(),
        "tracked_changes": subprocess.run(
            ["git", "diff", "--quiet", "HEAD"], cwd=repository
        ).returncode != 0,
        "system": platform.platform(),
        "compiler": subprocess.check_output(
            shlex.split(args.compiler) + ["--version"], text=True
        ).splitlines()[0],
        "cflags": args.cflags,
        "asan_options": os.environ.get("ASAN_OPTIONS", ""),
        "ubsan_options": os.environ.get("UBSAN_OPTIONS", ""),
        "mode": args.mode,
        "runs_per_target": args.runs,
        "seconds_per_target": args.seconds,
        "targets": [],
        "limitations": "Bounded runtime validation; not a cryptographic security proof.",
    }
    report = results / "report.json"
    for name, (seed, maximum) in TARGETS.items():
        corpus = root / name / "corpus"
        artifacts = root / name / "artifacts"
        artifacts.mkdir(parents=True, exist_ok=True)
        seed_corpus(corpus, maximum)
        command = [
            str(binaries[name]), str(corpus), f"-seed={seed}",
            f"-max_len={maximum}", f"-timeout={args.timeout}",
            "-rss_limit_mb=2048", "-print_final_stats=1",
            f"-artifact_prefix={artifacts}/",
        ]
        if args.runs:
            command.append(f"-runs={args.runs}")
        if args.seconds:
            command.append(f"-max_total_time={args.seconds}")
        entry: dict[str, object] = {
            "target": name, "seed": seed, "max_length": maximum,
            "binary_sha256": file_hash(binaries[name]), "command": command,
            "initial_corpus": corpus_manifest(corpus),
        }
        print(f"Running {name}: seeded boundary corpus, seed={seed}", flush=True)
        log = results / f"{name}.log"
        with log.open("w", encoding="utf-8") as output:
            completed = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT)
        contents = log.read_text(encoding="utf-8", errors="replace")
        entry.update({
            "exit_code": completed.returncode,
            "log": str(log.relative_to(build)),
            "final_corpus_files": len(corpus_manifest(corpus)),
            "stats": dict(re.findall(r"^stat::([^:]+):\s*(.+)$", contents, re.M)),
        })
        document["targets"].append(entry)
        report.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
        print(f"{name}: exit={completed.returncode}, stats={entry['stats']}", flush=True)
        if completed.returncode:
            print(contents[-8000:], file=sys.stderr)
            print(f"Failure report: {report}", file=sys.stderr)
            return 1
    print(f"PASS: four targets; report={report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
