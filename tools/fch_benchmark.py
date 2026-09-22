from __future__ import annotations

import argparse
import csv
import io
import json
import math
import os
import platform
import shlex
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


SCHEMA = "fch-benchmark-v1"
PROFILE = {
    "name": "baseline-v1",
    "input_seed": "c001d00d",
    "timer": "process_cpu",
    "warmups": 1,
    "trials": 5,
}
CSV_FIELDS = (
    "profile",
    "input_seed",
    "timer",
    "warmups",
    "trials",
    "algorithm",
    "bytes",
    "chunk_bytes",
    "iterations",
    "median_seconds",
    "mb_per_second",
    "peak_heap_bytes",
    "allocations_per_hash",
)
LENGTHS = (64, 1024, 16384, 262144, 1048576, 8388608)
TARGETS = (
    ("fch256-one-shot", 0),
    ("fch512-one-shot", 0),
    ("fch256-stream", 1),
    ("fch256-stream", 64),
    ("fch256-stream", 1024),
    ("fch256-stream", 65536),
    ("fch512-stream", 1024),
    ("fch512-stream", 65536),
)
ITERATIONS = {
    64: 65536,
    1024: 8192,
    16384: 512,
    262144: 32,
    1048576: 8,
    8388608: 2,
}
COMPATIBILITY_FIELDS = (
    "system",
    "machine",
    "cpu",
    "logical_cpus",
    "compiler_version",
    "cflags",
)


class BenchmarkError(RuntimeError):
    pass


def expected_cases() -> dict[tuple[str, int, int], int]:
    return {
        (algorithm, length, chunk): ITERATIONS[length]
        for algorithm, chunk in TARGETS
        for length in LENGTHS
    }


def parse_integer(row: dict[str, str], field: str, line: int) -> int:
    try:
        value = int(row[field], 10)
    except (KeyError, ValueError) as error:
        raise BenchmarkError(f"line {line}: invalid {field}") from error
    if value < 0:
        raise BenchmarkError(f"line {line}: negative {field}")
    return value


def parse_float(row: dict[str, str], field: str, line: int) -> float:
    try:
        value = float(row[field])
    except (KeyError, ValueError) as error:
        raise BenchmarkError(f"line {line}: invalid {field}") from error
    if not math.isfinite(value) or value <= 0.0:
        raise BenchmarkError(f"line {line}: non-positive {field}")
    return value


def validate_profile_row(row: dict[str, str], line: int) -> None:
    expected = {
        "profile": PROFILE["name"],
        "input_seed": PROFILE["input_seed"],
        "timer": PROFILE["timer"],
        "warmups": str(PROFILE["warmups"]),
        "trials": str(PROFILE["trials"]),
    }
    for field, value in expected.items():
        if row.get(field) != value:
            raise BenchmarkError(
                f"line {line}: {field} is {row.get(field)!r}, expected {value!r}"
            )


def parse_profile_csv(text: str) -> list[dict[str, Any]]:
    reader = csv.DictReader(io.StringIO(text))
    if tuple(reader.fieldnames or ()) != CSV_FIELDS:
        raise BenchmarkError("benchmark CSV header does not match baseline-v1")

    expected = expected_cases()
    results: list[dict[str, Any]] = []
    seen: set[tuple[str, int, int]] = set()
    for line, row in enumerate(reader, start=2):
        if None in row or any(value is None for value in row.values()):
            raise BenchmarkError(f"line {line}: malformed benchmark row")
        validate_profile_row(row, line)
        algorithm = row["algorithm"]
        length = parse_integer(row, "bytes", line)
        chunk = parse_integer(row, "chunk_bytes", line)
        iterations = parse_integer(row, "iterations", line)
        seconds = parse_float(row, "median_seconds", line)
        throughput = parse_float(row, "mb_per_second", line)
        peak_heap = parse_integer(row, "peak_heap_bytes", line)
        allocations = parse_integer(row, "allocations_per_hash", line)
        key = (algorithm, length, chunk)
        if key in seen:
            raise BenchmarkError(f"line {line}: duplicate benchmark case {key}")
        if key not in expected:
            raise BenchmarkError(f"line {line}: unexpected benchmark case {key}")
        if iterations != expected[key]:
            raise BenchmarkError(
                f"line {line}: iterations is {iterations}, expected {expected[key]}"
            )
        megabytes = length * iterations / 1000000.0
        lower = megabytes / (seconds + 0.0000005) - 0.0005
        upper_seconds = max(seconds - 0.0000005, sys.float_info.min)
        upper = megabytes / upper_seconds + 0.0005
        if throughput < lower or throughput > upper:
            raise BenchmarkError(
                f"line {line}: throughput does not match time and byte count"
            )
        results.append(
            {
                "algorithm": algorithm,
                "bytes": length,
                "chunk_bytes": chunk,
                "iterations": iterations,
                "median_seconds": seconds,
                "mb_per_second": throughput,
                "peak_heap_bytes": peak_heap,
                "allocations_per_hash": allocations,
            }
        )
        seen.add(key)

    missing = set(expected) - seen
    if missing:
        first = sorted(missing)[0]
        raise BenchmarkError(
            f"benchmark output is missing {len(missing)} case(s), "
            f"beginning with {first}"
        )
    return results


def run_profile(binary: Path) -> list[dict[str, Any]]:
    executable = binary.expanduser().resolve()
    if not executable.is_file():
        raise BenchmarkError(f"benchmark binary not found: {executable}")
    try:
        completed = subprocess.run(
            [str(executable), "--baseline"],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
    except OSError as error:
        raise BenchmarkError(f"could not run benchmark: {error}") from error
    if completed.returncode != 0:
        detail = completed.stderr.strip() or "no diagnostic output"
        raise BenchmarkError(
            f"benchmark exited with status {completed.returncode}: {detail}"
        )
    return parse_profile_csv(completed.stdout)


def cpu_name() -> str:
    linux_cpuinfo = Path("/proc/cpuinfo")
    if linux_cpuinfo.is_file():
        try:
            for line in linux_cpuinfo.read_text(
                encoding="utf-8", errors="replace"
            ).splitlines():
                key, separator, value = line.partition(":")
                if separator and key.strip() in {"model name", "Hardware"}:
                    if value.strip():
                        return value.strip()
        except OSError:
            pass
    return platform.processor().strip() or "unknown"


def compiler_version(command: str) -> str:
    try:
        arguments = shlex.split(command, posix=os.name != "nt")
    except ValueError as error:
        raise BenchmarkError(f"invalid compiler command: {error}") from error
    if not arguments:
        raise BenchmarkError("compiler command is empty")
    try:
        completed = subprocess.run(
            arguments + ["--version"],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
    except OSError as error:
        raise BenchmarkError(f"could not identify compiler: {error}") from error
    if completed.returncode != 0:
        raise BenchmarkError(
            f"compiler version command exited with status {completed.returncode}"
        )
    lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    if not lines:
        raise BenchmarkError("compiler version command returned no text")
    return lines[0]


def environment_record(compiler: str, cflags: str) -> dict[str, Any]:
    return {
        "system": platform.system() or "unknown",
        "release": platform.release() or "unknown",
        "machine": platform.machine() or "unknown",
        "cpu": cpu_name(),
        "logical_cpus": os.cpu_count() or 0,
        "compiler_command": compiler,
        "compiler_version": compiler_version(compiler),
        "cflags": cflags,
        "python": platform.python_version(),
    }


def source_record() -> dict[str, Any]:
    root = Path(__file__).resolve().parent.parent
    try:
        revision = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=root,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        changes = subprocess.run(
            ["git", "diff", "--quiet", "HEAD", "--"],
            cwd=root,
            check=False,
            capture_output=True,
        )
    except OSError:
        return {"revision": "unknown", "dirty": None}
    if revision.returncode != 0 or changes.returncode not in {0, 1}:
        return {"revision": "unknown", "dirty": None}
    return {
        "revision": revision.stdout.strip() or "unknown",
        "dirty": changes.returncode == 1,
    }


def capture_document(
    binary: Path, compiler: str, cflags: str
) -> dict[str, Any]:
    return {
        "schema": SCHEMA,
        "captured_at_utc": datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z"),
        "profile": dict(PROFILE),
        "source": source_record(),
        "environment": environment_record(compiler, cflags),
        "results": run_profile(binary),
    }


def normalize_result(raw: Any, index: int) -> dict[str, Any]:
    if not isinstance(raw, dict):
        raise BenchmarkError(f"result {index} is not an object")
    fields = {
        "algorithm",
        "bytes",
        "chunk_bytes",
        "iterations",
        "median_seconds",
        "mb_per_second",
        "peak_heap_bytes",
        "allocations_per_hash",
    }
    if set(raw) != fields:
        raise BenchmarkError(f"result {index} has an invalid field set")
    try:
        result = {
            "algorithm": str(raw["algorithm"]),
            "bytes": int(raw["bytes"]),
            "chunk_bytes": int(raw["chunk_bytes"]),
            "iterations": int(raw["iterations"]),
            "median_seconds": float(raw["median_seconds"]),
            "mb_per_second": float(raw["mb_per_second"]),
            "peak_heap_bytes": int(raw["peak_heap_bytes"]),
            "allocations_per_hash": int(raw["allocations_per_hash"]),
        }
    except (TypeError, ValueError) as error:
        raise BenchmarkError(f"result {index} contains an invalid value") from error
    if (
        result["bytes"] < 0
        or result["chunk_bytes"] < 0
        or result["iterations"] <= 0
        or result["peak_heap_bytes"] < 0
        or result["allocations_per_hash"] < 0
        or not math.isfinite(result["median_seconds"])
        or result["median_seconds"] <= 0.0
        or not math.isfinite(result["mb_per_second"])
        or result["mb_per_second"] <= 0.0
    ):
        raise BenchmarkError(f"result {index} contains an out-of-range value")
    return result


def validate_document(document: Any) -> dict[str, Any]:
    if not isinstance(document, dict):
        raise BenchmarkError("baseline root is not an object")
    if document.get("schema") != SCHEMA:
        raise BenchmarkError(f"baseline schema must be {SCHEMA}")
    if document.get("profile") != PROFILE:
        raise BenchmarkError("baseline profile does not match baseline-v1")
    environment = document.get("environment")
    if not isinstance(environment, dict):
        raise BenchmarkError("baseline environment is missing")
    required_environment = set(COMPATIBILITY_FIELDS) | {
        "release",
        "compiler_command",
        "python",
    }
    if not required_environment.issubset(environment):
        raise BenchmarkError("baseline environment is incomplete")
    raw_results = document.get("results")
    if not isinstance(raw_results, list):
        raise BenchmarkError("baseline results are missing")
    results = [
        normalize_result(raw, index)
        for index, raw in enumerate(raw_results)
    ]
    expected = expected_cases()
    indexed: dict[tuple[str, int, int], dict[str, Any]] = {}
    for result in results:
        key = (
            result["algorithm"],
            result["bytes"],
            result["chunk_bytes"],
        )
        if key in indexed:
            raise BenchmarkError(f"duplicate baseline result: {key}")
        if key not in expected:
            raise BenchmarkError(f"unexpected baseline result: {key}")
        if result["iterations"] != expected[key]:
            raise BenchmarkError(f"baseline iteration count changed: {key}")
        indexed[key] = result
    if set(indexed) != set(expected):
        raise BenchmarkError("baseline does not contain the complete case matrix")
    normalized = dict(document)
    normalized["results"] = results
    return normalized


def load_document(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            document = json.load(handle)
    except OSError as error:
        raise BenchmarkError(f"could not read {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise BenchmarkError(f"invalid JSON in {path}: {error}") from error
    return validate_document(document)


def write_document(path: Path, document: dict[str, Any]) -> None:
    destination = path.expanduser().resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary_name = ""
    try:
        with tempfile.NamedTemporaryFile(
            "w",
            encoding="utf-8",
            dir=destination.parent,
            prefix=f".{destination.name}.",
            suffix=".tmp",
            delete=False,
        ) as handle:
            temporary_name = handle.name
            json.dump(document, handle, indent=2, sort_keys=True)
            handle.write("\n")
        os.replace(temporary_name, destination)
    except OSError as error:
        if temporary_name:
            try:
                Path(temporary_name).unlink()
            except OSError:
                pass
        raise BenchmarkError(f"could not write {destination}: {error}") from error


def result_index(
    document: dict[str, Any]
) -> dict[tuple[str, int, int], dict[str, Any]]:
    return {
        (result["algorithm"], result["bytes"], result["chunk_bytes"]): result
        for result in document["results"]
    }


def environment_mismatches(
    baseline: dict[str, Any], current: dict[str, Any]
) -> list[tuple[str, Any, Any]]:
    return [
        (
            field,
            baseline["environment"].get(field),
            current["environment"].get(field),
        )
        for field in COMPATIBILITY_FIELDS
        if baseline["environment"].get(field)
        != current["environment"].get(field)
    ]


def compare_results(
    baseline: dict[str, Any],
    current: dict[str, Any],
    max_regression: float,
) -> tuple[list[dict[str, Any]], bool]:
    baseline_results = result_index(baseline)
    current_results = result_index(current)
    rows: list[dict[str, Any]] = []
    failed = False
    for key in sorted(baseline_results):
        old = baseline_results[key]
        new = current_results[key]
        ratio = new["mb_per_second"] / old["mb_per_second"]
        delta = (ratio - 1.0) * 100.0
        issues = []
        if delta < -max_regression:
            issues.append("throughput")
        if new["peak_heap_bytes"] != old["peak_heap_bytes"]:
            issues.append("peak-heap")
        if new["allocations_per_hash"] != old["allocations_per_hash"]:
            issues.append("allocations")
        if issues:
            failed = True
        rows.append(
            {
                "algorithm": key[0],
                "bytes": key[1],
                "chunk_bytes": key[2],
                "baseline": old["mb_per_second"],
                "current": new["mb_per_second"],
                "delta": delta,
                "status": "+".join(issues) if issues else "PASS",
            }
        )
    return rows, failed


def print_comparison(rows: list[dict[str, Any]]) -> None:
    print(
        "algorithm,bytes,chunk_bytes,baseline_mb_per_second,"
        "current_mb_per_second,delta_percent,status"
    )
    for row in rows:
        print(
            f"{row['algorithm']},{row['bytes']},{row['chunk_bytes']},"
            f"{row['baseline']:.3f},{row['current']:.3f},"
            f"{row['delta']:.2f},{row['status']}"
        )


def add_capture_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--binary", type=Path, default=Path("./bench_hash"))
    parser.add_argument("--compiler", default=os.environ.get("CC", "cc"))
    parser.add_argument("--cflags", default=os.environ.get("CFLAGS", ""))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Capture and compare reproducible FCH performance baselines"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    capture = subparsers.add_parser("capture")
    add_capture_options(capture)
    capture.add_argument("--output", type=Path, required=True)

    compare = subparsers.add_parser("compare")
    add_capture_options(compare)
    compare.add_argument("--baseline", type=Path, required=True)
    compare.add_argument("--output-current", type=Path)
    compare.add_argument("--max-regression", type=float, default=20.0)
    compare.add_argument("--allow-environment-mismatch", action="store_true")

    validate = subparsers.add_parser("validate")
    validate.add_argument("--file", type=Path, required=True)
    return parser


def command_capture(args: argparse.Namespace) -> int:
    document = capture_document(args.binary, args.compiler, args.cflags)
    write_document(args.output, document)
    print(
        f"captured {len(document['results'])} cases to "
        f"{args.output.expanduser().resolve()}"
    )
    return 0


def command_compare(args: argparse.Namespace) -> int:
    if not 0.0 <= args.max_regression < 100.0:
        raise BenchmarkError("max regression must be at least 0 and below 100")
    baseline = load_document(args.baseline)
    current = validate_document(
        capture_document(args.binary, args.compiler, args.cflags)
    )
    if args.output_current:
        write_document(args.output_current, current)

    mismatches = environment_mismatches(baseline, current)
    if mismatches:
        for field, old, new in mismatches:
            print(
                f"environment mismatch: {field}: {old!r} != {new!r}",
                file=sys.stderr,
            )
        if not args.allow_environment_mismatch:
            print(
                "comparison stopped; use --allow-environment-mismatch "
                "only for an explicitly cross-environment comparison",
                file=sys.stderr,
            )
            return 2

    rows, failed = compare_results(baseline, current, args.max_regression)
    print_comparison(rows)
    if failed:
        print(
            f"FAIL: regression limit {args.max_regression:.2f}% or resource "
            "profile mismatch exceeded",
            file=sys.stderr,
        )
        return 1
    print(
        f"PASS: {len(rows)} cases stayed within the "
        f"{args.max_regression:.2f}% regression limit"
    )
    return 0


def command_validate(args: argparse.Namespace) -> int:
    document = load_document(args.file)
    print(
        f"valid {document['schema']} baseline with "
        f"{len(document['results'])} cases"
    )
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "capture":
            return command_capture(args)
        if args.command == "compare":
            return command_compare(args)
        return command_validate(args)
    except BenchmarkError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
