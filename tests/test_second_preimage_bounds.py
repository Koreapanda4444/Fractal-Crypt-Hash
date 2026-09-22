from __future__ import annotations

import copy
import math
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import fch_second_preimage_bounds as bounds  # noqa: E402


class SecondPreimageBoundsTests(unittest.TestCase):
    def test_padding_and_leaf_boundaries(self) -> None:
        expected = {
            0: (64, 1, 64, 1, 3),
            55: (64, 1, 64, 1, 3),
            56: (65, 1, 65, 1, 3),
            1015: (1024, 1, 1024, 1, 11),
            1016: (1025, 2, 1, 3, 16),
            1024: (1033, 2, 9, 3, 16),
        }
        for length, values in expected.items():
            with self.subTest(length=length):
                result = bounds.geometry(length)
                self.assertEqual(
                    (
                        result["padded_bytes"],
                        result["leaves"],
                        result["final_leaf_bytes"],
                        result["tree_map_evaluations"],
                        result["compression_calls_per_hash"],
                    ),
                    values,
                )

    def test_large_message_geometry(self) -> None:
        mebibyte = bounds.geometry(1 << 20)
        self.assertEqual(mebibyte["leaves"], 1025)
        self.assertEqual(mebibyte["tree_map_evaluations"], 2049)
        self.assertEqual(mebibyte["compression_calls_per_hash"], 13315)

        maximum = bounds.geometry(bounds.MAX_MESSAGE_BYTES)
        self.assertEqual(maximum["leaves"], (1 << 51) + 1)
        self.assertEqual(maximum["tree_map_evaluations"], (1 << 52) + 1)
        self.assertEqual(
            maximum["compression_calls_per_hash"],
            29273397577908227,
        )

    def test_canonical_descriptors_are_unique(self) -> None:
        for length in bounds.ENUMERATED_LENGTHS:
            with self.subTest(length=length):
                result = bounds.descriptor_check(length)
                self.assertEqual(
                    result["descriptors"], result["unique_descriptors"]
                )
                self.assertEqual(
                    result["maximum_compatible_target_states_per_descriptor"],
                    1,
                )

    def test_query_count_and_primitive_work_are_separate(self) -> None:
        for label, length in bounds.PROFILE_LENGTHS:
            with self.subTest(label=label):
                result = bounds.result_for(label, length)
                opportunity_bits = math.log2(
                    result["complete_candidate_opportunities"]
                )
                self.assertAlmostEqual(
                    result["classical_complete_message_union_scale_log2"]
                    + opportunity_bits,
                    512.0,
                    places=5,
                )
                self.assertEqual(
                    result["classical_descriptor_map_evaluations_log2"],
                    512.0,
                )
                self.assertAlmostEqual(
                    2
                    * result[
                        "quantum_complete_hash_opportunity_scale_log2"
                    ]
                    + opportunity_bits,
                    512.0,
                    places=5,
                )
                self.assertEqual(
                    result["quantum_descriptor_map_queries_log2"],
                    256.0,
                )

    def test_invalid_lengths_are_rejected(self) -> None:
        for length in (-1, bounds.MAX_MESSAGE_BYTES + 1, 1.5, True):
            with self.subTest(length=length):
                with self.assertRaises(bounds.AnalysisError):
                    bounds.geometry(length)  # type: ignore[arg-type]

    def test_document_is_deterministic_and_valid(self) -> None:
        first = bounds.build_document()
        second = bounds.build_document()
        self.assertEqual(first, second)
        bounds.validate_document(first)
        self.assertEqual(first["summary"]["status"], "PASS")
        self.assertEqual(
            first["established_format_facts"][
                "single_target_descriptor_multiplicity"
            ],
            1,
        )
        self.assertIn("fixed_target", first["conditional_bounds"])
        self.assertIn("not proofs", first["limitations"][0])

    def test_validation_rejects_changed_bound(self) -> None:
        document = copy.deepcopy(bounds.build_document())
        document["results"][0]["canonical_compatibility_kappa"] = 2
        with self.assertRaises(bounds.AnalysisError):
            bounds.validate_document(document)

    def test_first_difference_reports_nested_path(self) -> None:
        left = {"results": [{"value": 1}]}
        right = copy.deepcopy(left)
        right["results"][0]["value"] = 2
        self.assertEqual(
            bounds.first_difference(left, right),
            "$.results[0].value: 1 != 2",
        )


if __name__ == "__main__":
    unittest.main()
