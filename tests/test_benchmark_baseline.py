from __future__ import annotations

import copy
import csv
import importlib.util
import io
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "fch_benchmark", ROOT / "tools" / "fch_benchmark.py"
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load benchmark module")
benchmark = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(benchmark)


def make_results() -> list[dict[str, object]]:
    results = []
    for index, (key, iterations) in enumerate(
        benchmark.expected_cases().items()
    ):
        algorithm, length, chunk = key
        throughput = 40.0 + index
        seconds = length * iterations / 1000000.0 / throughput
        results.append(
            {
                "algorithm": algorithm,
                "bytes": length,
                "chunk_bytes": chunk,
                "iterations": iterations,
                "median_seconds": seconds,
                "mb_per_second": throughput,
                "peak_heap_bytes": 8208 if chunk else length + 73,
                "allocations_per_hash": 1 if chunk else 2,
            }
        )
    return results


def make_document() -> dict[str, object]:
    return {
        "schema": benchmark.SCHEMA,
        "captured_at_utc": "2026-09-21T00:00:00Z",
        "profile": dict(benchmark.PROFILE),
        "source": {"revision": "abc", "dirty": False},
        "environment": {
            "system": "TestOS",
            "release": "1",
            "machine": "test64",
            "cpu": "Test CPU",
            "logical_cpus": 4,
            "compiler_command": "cc",
            "compiler_version": "cc 1.0",
            "cflags": "-O2",
            "python": "3.12.0",
        },
        "results": make_results(),
    }


def make_csv() -> str:
    output = io.StringIO()
    writer = csv.DictWriter(output, fieldnames=benchmark.CSV_FIELDS)
    writer.writeheader()
    for result in make_results():
        writer.writerow(
            {
                "profile": benchmark.PROFILE["name"],
                "input_seed": benchmark.PROFILE["input_seed"],
                "timer": benchmark.PROFILE["timer"],
                "warmups": benchmark.PROFILE["warmups"],
                "trials": benchmark.PROFILE["trials"],
                "algorithm": result["algorithm"],
                "bytes": result["bytes"],
                "chunk_bytes": result["chunk_bytes"],
                "iterations": result["iterations"],
                "median_seconds": f"{result['median_seconds']:.12f}",
                "mb_per_second": f"{result['mb_per_second']:.3f}",
                "peak_heap_bytes": result["peak_heap_bytes"],
                "allocations_per_hash": result["allocations_per_hash"],
            }
        )
    return output.getvalue()


class BenchmarkBaselineTests(unittest.TestCase):
    def test_parse_complete_profile(self) -> None:
        results = benchmark.parse_profile_csv(make_csv())
        self.assertEqual(len(results), 48)
        self.assertEqual(results[0]["algorithm"], "fch256-one-shot")
        self.assertEqual(results[-1]["chunk_bytes"], 65536)

    def test_parse_rejects_missing_case(self) -> None:
        lines = make_csv().splitlines()
        with self.assertRaises(benchmark.BenchmarkError):
            benchmark.parse_profile_csv("\n".join(lines[:-1]) + "\n")

    def test_document_round_trip(self) -> None:
        document = benchmark.validate_document(make_document())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "baseline.json"
            benchmark.write_document(path, document)
            loaded = benchmark.load_document(path)
        self.assertEqual(loaded, document)

    def test_compare_detects_throughput_regression(self) -> None:
        baseline = benchmark.validate_document(make_document())
        current_data = copy.deepcopy(baseline)
        current_data["results"][0]["mb_per_second"] *= 0.8
        current = benchmark.validate_document(current_data)
        rows, failed = benchmark.compare_results(baseline, current, 15.0)
        self.assertTrue(failed)
        self.assertEqual(rows[0]["status"], "throughput")

    def test_compare_detects_resource_profile_change(self) -> None:
        baseline = benchmark.validate_document(make_document())
        current_data = copy.deepcopy(baseline)
        current_data["results"][0]["peak_heap_bytes"] += 1
        current_data["results"][0]["allocations_per_hash"] += 1
        current = benchmark.validate_document(current_data)
        rows, failed = benchmark.compare_results(baseline, current, 15.0)
        self.assertTrue(failed)
        self.assertEqual(rows[0]["status"], "peak-heap+allocations")

    def test_environment_mismatch_is_reported(self) -> None:
        baseline = benchmark.validate_document(make_document())
        current_data = copy.deepcopy(baseline)
        current_data["environment"]["cpu"] = "Different CPU"
        current = benchmark.validate_document(current_data)
        mismatches = benchmark.environment_mismatches(baseline, current)
        self.assertEqual(mismatches, [("cpu", "Test CPU", "Different CPU")])


if __name__ == "__main__":
    unittest.main()
