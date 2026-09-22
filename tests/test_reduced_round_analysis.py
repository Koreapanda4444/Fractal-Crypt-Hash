from __future__ import annotations

import copy
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import fch_reduced_round_analysis as analysis  # noqa: E402


class ReducedRoundAnalysisTests(unittest.TestCase):
    def test_round_evaluator_matches_reference(self) -> None:
        analysis.verify_reference()

    def test_structural_messages_are_unique(self) -> None:
        messages = [
            analysis.structured_message(family, sample)
            for family in analysis.PROFILE["structural_families"]
            for sample in range(analysis.SAMPLES)
        ]
        self.assertEqual(len(messages), len(set(messages)))

    def test_small_analysis_is_deterministic(self) -> None:
        first = analysis.run_analysis((1, 2), 4)
        second = analysis.run_analysis((1, 2), 4)
        self.assertEqual(first, second)
        self.assertEqual(len(first["diffusion"]), 2)
        self.assertEqual(
            len(first["structural_bias"]),
            2 * len(analysis.PROFILE["structural_families"]),
        )

    def test_diffusion_threshold_detects_regression(self) -> None:
        entry = {
            "round": 2,
            "zero_state_differences": 0,
            "zero_output_differences": 0,
            "minimum_state_weight": analysis.THRESHOLDS["minimum_state_weight"],
            "minimum_state_active_words": 16,
            "minimum_output_weight": analysis.THRESHOLDS["minimum_output_weight"],
            "minimum_output_active_words": 8,
            "maximum_output_bit_bias_pct": 10.0,
        }
        self.assertEqual(analysis.diffusion_status(entry), "PASS")
        entry["minimum_output_weight"] -= 1
        self.assertEqual(analysis.diffusion_status(entry), "FAIL")

    def test_first_difference_reports_nested_path(self) -> None:
        left = {"experiments": [{"round": 1, "value": 2}]}
        right = copy.deepcopy(left)
        right["experiments"][0]["value"] = 3
        self.assertEqual(
            analysis.first_difference(left, right),
            "$.experiments[0].value: 2 != 3",
        )

    def test_validation_rejects_incomplete_experiments(self) -> None:
        document = {
            "schema": analysis.SCHEMA,
            "profile": analysis.PROFILE,
            "experiments": {},
            "summary": {},
            "limitations": analysis.LIMITATIONS,
        }
        with self.assertRaises(analysis.AnalysisError):
            analysis.validate_document(document)

    def test_result_schema_lists_status_inputs(self) -> None:
        for experiment, status_function in analysis.STATUS_FUNCTIONS.items():
            self.assertIn("round", analysis.RESULT_FIELDS[experiment])
            self.assertIn("status", analysis.RESULT_FIELDS[experiment])
            self.assertTrue(callable(status_function))


if __name__ == "__main__":
    unittest.main()
