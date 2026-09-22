from __future__ import annotations

import argparse
import json
import math
import os
import sys
import tempfile
from pathlib import Path
from typing import Any


SCHEMA = "fch-second-preimage-bounds-v1"
STATE_BITS = 512
OUTPUT_BITS = 512
LEAF_BYTES = 1024
MIN_PADDED_BYTES = 64
PADDING_OVERHEAD = 9
MAX_MESSAGE_BYTES = (1 << 61) - 1

PROFILE_LENGTHS = (
    ("empty", 0),
    ("last-minimum-padding", 55),
    ("first-above-minimum-padding", 56),
    ("last-single-leaf", 1015),
    ("first-two-leaf", 1016),
    ("one-kibibyte", 1 << 10),
    ("one-mebibyte", 1 << 20),
    ("one-gibibyte", 1 << 30),
    ("format-maximum", MAX_MESSAGE_BYTES),
)

ENUMERATED_LENGTHS = (0, 55, 56, 1015, 1016, 1024, 1 << 20)

PROFILE = {
    "name": "fch512-second-preimage-v1",
    "state_bits": STATE_BITS,
    "output_bits": OUTPUT_BITS,
    "leaf_bytes": LEAF_BYTES,
    "minimum_padded_bytes": MIN_PADDED_BYTES,
    "padding_overhead_bytes": PADDING_OVERHEAD,
    "maximum_message_bytes": MAX_MESSAGE_BYTES,
    "lengths": [
        {"label": label, "original_bytes": length}
        for label, length in PROFILE_LENGTHS
    ],
    "cost_units": {
        "complete_message_query": (
            "one evaluation of a complete candidate message"
        ),
        "typed_map_evaluation": (
            "one distinct Leaf, Node, or Output input evaluation"
        ),
        "serial_compression_proxy": (
            "complete-message queries multiplied by compression calls per hash"
        ),
    },
    "conditional_model": [
        "Leaf and Node are independent random maps to 512 bits on distinct typed inputs.",
        "Output is an independent random map to 512 bits.",
        "There is no shortcut inside the compression construction.",
        "A candidate substitution must match the target role and complete tree descriptor.",
    ],
}

LIMITATIONS = [
    "The bounds are conditional ideal-map union bounds, not proofs for the FCH compression function.",
    "A unit-advantage union-bound scale is not a constructive attack or a success threshold.",
    "The square-rooted complete-message value is an optimistic opportunity scale; the union bound does not establish marked density for amplitude amplification.",
    "Complete-message queries and typed-map evaluations are different cost units.",
    "The serial compression proxy omits memory, parallelism, circuit depth, uncomputation, and other attack costs.",
    "The descriptor check establishes canonical compatibility, not resistance to structural or compression shortcuts.",
    "The existing bounded second-preimage screen is empirical evidence only and does not establish these exponents.",
]


class AnalysisError(RuntimeError):
    pass


def ceil_div(numerator: int, denominator: int) -> int:
    return (numerator + denominator - 1) // denominator


def geometry(original_bytes: int) -> dict[str, int]:
    if not isinstance(original_bytes, int) or isinstance(original_bytes, bool):
        raise AnalysisError("message length must be an integer")
    if original_bytes < 0 or original_bytes > MAX_MESSAGE_BYTES:
        raise AnalysisError("message length is outside the format range")

    padded_bytes = max(MIN_PADDED_BYTES, original_bytes + PADDING_OVERHEAD)
    leaves = ceil_div(padded_bytes, LEAF_BYTES)
    final_leaf_bytes = padded_bytes - LEAF_BYTES * (leaves - 1)
    root_level = (leaves - 1).bit_length()
    tree_map_evaluations = 2 * leaves - 1
    compression_calls = (
        13 * leaves - 11 + ceil_div(final_leaf_bytes, 120)
    )
    return {
        "padded_bytes": padded_bytes,
        "leaves": leaves,
        "final_leaf_bytes": final_leaf_bytes,
        "root_level": root_level,
        "tree_map_evaluations": tree_map_evaluations,
        "compression_calls_per_hash": compression_calls,
    }


def largest_power_of_two_below(value: int) -> int:
    if value < 2:
        raise AnalysisError("a split requires at least two leaves")
    return 1 << ((value - 1).bit_length() - 1)


def position(offset: int, length: int) -> tuple[int, int, int, int, int]:
    if length <= 0 or offset < 0 or offset % LEAF_BYTES != 0:
        raise AnalysisError("invalid canonical tree range")
    leaves = ceil_div(length, LEAF_BYTES)
    return (
        (leaves - 1).bit_length(),
        offset // LEAF_BYTES,
        leaves,
        offset,
        length,
    )


def canonical_descriptors(padded_bytes: int) -> list[tuple[Any, ...]]:
    if padded_bytes < MIN_PADDED_BYTES:
        raise AnalysisError("padded length is below the format minimum")

    descriptors: list[tuple[Any, ...]] = []
    pending = [(0, padded_bytes)]
    while pending:
        offset, length = pending.pop()
        descriptor = position(offset, length)
        leaves = descriptor[2]
        role = "Leaf" if leaves == 1 else "Node"
        descriptors.append((role, *descriptor))
        if leaves == 1:
            continue

        left_leaves = largest_power_of_two_below(leaves)
        left_length = left_leaves * LEAF_BYTES
        pending.append((offset + left_length, length - left_length))
        pending.append((offset, left_length))
    return descriptors


def result_for(label: str, original_bytes: int) -> dict[str, Any]:
    layout = geometry(original_bytes)
    tree_maps = layout["tree_map_evaluations"]
    calls = layout["compression_calls_per_hash"]
    opportunities = tree_maps + 1
    opportunity_bits = math.log2(opportunities)
    complete_query_bits = STATE_BITS - opportunity_bits
    quantum_complete_query_bits = STATE_BITS / 2 - opportunity_bits / 2

    return {
        "label": label,
        "original_bytes": original_bytes,
        **layout,
        "canonical_compatibility_kappa": 1,
        "complete_candidate_opportunities": opportunities,
        "classical_complete_message_union_scale_log2": round(
            complete_query_bits, 6
        ),
        "classical_descriptor_map_evaluations_log2": float(STATE_BITS),
        "classical_serial_compression_union_proxy_log2": round(
            complete_query_bits + math.log2(calls), 6
        ),
        "quantum_complete_hash_opportunity_scale_log2": round(
            quantum_complete_query_bits, 6
        ),
        "quantum_descriptor_map_queries_log2": STATE_BITS / 2,
        "quantum_serial_compression_opportunity_proxy_log2": round(
            quantum_complete_query_bits + math.log2(calls), 6
        ),
    }


def descriptor_check(original_bytes: int) -> dict[str, int]:
    layout = geometry(original_bytes)
    descriptors = canonical_descriptors(layout["padded_bytes"])
    unique = len(set(descriptors))
    expected = layout["tree_map_evaluations"]
    if len(descriptors) != expected or unique != expected:
        raise AnalysisError("canonical tree descriptors are not unique")
    return {
        "original_bytes": original_bytes,
        "descriptors": len(descriptors),
        "unique_descriptors": unique,
        "maximum_compatible_target_states_per_descriptor": 1,
    }


def build_document() -> dict[str, Any]:
    results = [result_for(label, length) for label, length in PROFILE_LENGTHS]
    descriptor_checks = [
        descriptor_check(length) for length in ENUMERATED_LENGTHS
    ]
    return {
        "schema": SCHEMA,
        "profile": PROFILE,
        "established_format_facts": {
            "geometry": "P=max(64,L+9); N=ceil(P/1024); T=2*N-1",
            "compression_calls": "C(L)=13*N-11+ceil(b/120)",
            "canonical_tree": (
                "padded length fixes every split and subtree descriptor"
            ),
            "single_target_descriptor_multiplicity": 1,
        },
        "conditional_bounds": {
            "fixed_target": (
                "Adv_2nd(H,Q;kappa) <= H/2^512 + kappa*Q/2^512"
            ),
            "canonical_single_target_kappa": 1,
            "complete_candidates": (
                "Adv_2nd(q) <= q*(T+1)/2^512 when H<=q and Q<=q*T"
            ),
            "multi_target": (
                "Adv_multi(H,Q;r,kappa_r) <= r*H/2^512 + kappa_r*Q/2^512"
            ),
            "canonical_multi_target_kappa_upper_bound": "kappa_r <= r",
        },
        "results": results,
        "descriptor_checks": descriptor_checks,
        "summary": {
            "status": "PASS",
            "result_rows": len(results),
            "enumerated_trees": len(descriptor_checks),
            "enumerated_descriptors": sum(
                item["descriptors"] for item in descriptor_checks
            ),
            "canonical_single_target_kappa": 1,
            "classical_descriptor_map_evaluations_log2": float(STATE_BITS),
            "quantum_descriptor_map_queries_log2": STATE_BITS / 2,
        },
        "limitations": LIMITATIONS,
    }


def first_difference(left: Any, right: Any, path: str = "$") -> str | None:
    if type(left) is not type(right):
        return f"{path}: type {type(left).__name__} != {type(right).__name__}"
    if isinstance(left, dict):
        if list(left) != list(right):
            return f"{path}: keys {list(left)!r} != {list(right)!r}"
        for key in left:
            difference = first_difference(left[key], right[key], f"{path}.{key}")
            if difference:
                return difference
        return None
    if isinstance(left, list):
        if len(left) != len(right):
            return f"{path}: length {len(left)} != {len(right)}"
        for index, (left_item, right_item) in enumerate(zip(left, right)):
            difference = first_difference(
                left_item, right_item, f"{path}[{index}]"
            )
            if difference:
                return difference
        return None
    if left != right:
        return f"{path}: {left!r} != {right!r}"
    return None


def validate_document(document: Any) -> None:
    if not isinstance(document, dict):
        raise AnalysisError("analysis document must be an object")
    expected = build_document()
    difference = first_difference(document, expected)
    if difference:
        raise AnalysisError(f"invalid analysis document: {difference}")


def load_document(path: Path) -> Any:
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        raise AnalysisError(f"cannot read {path}: {error}") from error


def write_document(path: Path, document: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=path.parent, text=True
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            json.dump(document, handle, indent=2, ensure_ascii=False)
            handle.write("\n")
        os.replace(temporary_name, path)
    except BaseException:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def report(document: dict[str, Any]) -> None:
    summary = document["summary"]
    print(
        "SECOND_PREIMAGE_BOUNDS: "
        f"{summary['status']} rows={summary['result_rows']} "
        f"descriptor_checks={summary['enumerated_trees']} "
        f"kappa={summary['canonical_single_target_kappa']} "
        "(conditional ideal-map accounting; not a security proof)"
    )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Reproduce the conditional FCH-512 second-preimage bounds."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    capture = subparsers.add_parser("capture", help="write the fixed profile")
    capture.add_argument("--output", type=Path, required=True)

    check = subparsers.add_parser("check", help="verify the fixed profile")
    check.add_argument("--baseline", type=Path, required=True)

    subparsers.add_parser("show", help="print the fixed profile")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        current = build_document()
        validate_document(current)
        if args.command == "capture":
            write_document(args.output, current)
            report(current)
            print(f"SECOND_PREIMAGE_BASELINE: WROTE {args.output}")
            return 0
        if args.command == "show":
            json.dump(current, sys.stdout, indent=2, ensure_ascii=False)
            sys.stdout.write("\n")
            return 0

        baseline = load_document(args.baseline)
        validate_document(baseline)
        difference = first_difference(baseline, current)
        if difference:
            print(f"SECOND_PREIMAGE_BASELINE: FAIL: {difference}", file=sys.stderr)
            return 1
        report(current)
        print("SECOND_PREIMAGE_BASELINE: PASS")
        return 0
    except AnalysisError as error:
        print(f"SECOND_PREIMAGE_BOUNDS: ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
