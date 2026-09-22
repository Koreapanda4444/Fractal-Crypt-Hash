from __future__ import annotations

import argparse
import json
import math
import os
import sys
import tempfile
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

import fch_reference as ref
import fch_trail_search as trail


MASK64 = (1 << 64) - 1
SCHEMA = "fch-reduced-round-analysis-v1"
SEED = 0xD1FF0510C0DEC0DE
SAMPLES = 128
DIFFUSION_BASES = 4
ROTATIONS = (1, 7, 8, 16, 24, 32, 63)
ROUNDS = tuple(range(1, ref.ROUNDS + 1))

DIFFUSION_BITS = tuple(
    (f"word-{word}-bit-{bit}", word, bit)
    for word in range(16)
    for bit in (0, 31, 32, 63)
)

XOR_DIFFERENCES = (
    ("single-w0-b0", ((0, 1),)),
    ("single-w15-b63", ((15, 1 << 63),)),
    ("adjacent-w5-b0-b1", ((5, 0x3),)),
    ("boundary-w10-b31-b32", ((10, (1 << 31) | (1 << 32)),)),
    ("cross-word-w0-b63-w1-b0", ((0, 1 << 63), (1, 1))),
    (
        "diagonal-four-word",
        ((0, 1 << 7), (5, 1 << 23), (10, 1 << 39), (15, 1 << 55)),
    ),
    ("full-word-w7", ((7, MASK64),)),
    ("alternating-w3-w12", ((3, 0xAAAAAAAAAAAAAAAA), (12, 0x5555555555555555))),
)

PROJECTIONS = (
    ("word0-low12", tuple(range(0, 12))),
    ("word3-middle12", tuple(3 * 64 + bit for bit in range(20, 32))),
    ("word7-high12", tuple(7 * 64 + bit for bit in range(52, 64))),
    (
        "diagonal12",
        tuple((index % 8) * 64 + ((index * 11 + 7) % 64) for index in range(12)),
    ),
)

THRESHOLDS = {
    "first_checked_round": 2,
    "minimum_state_weight": 256,
    "minimum_output_weight": 128,
    "maximum_diffusion_bit_bias_pct": 20.0,
    "maximum_differential_bit_bias_pct": 30.0,
    "maximum_differential_projection_frequency": 0.125,
    "maximum_differential_exact_count": 1,
    "maximum_rotation_average_bias_pct": 5.0,
    "maximum_rotation_bit_bias_pct": 30.0,
    "maximum_structural_bit_bias_pct": 30.0,
    "maximum_structural_word_bias_pct": 5.0,
    "maximum_structural_projection_frequency": 0.125,
}

PROFILE = {
    "name": "reduced-round-v1",
    "seed": f"{SEED:016x}",
    "rounds": list(ROUNDS),
    "samples": SAMPLES,
    "diffusion_base_samples": DIFFUSION_BASES,
    "diffusion_input_bits": [name for name, _, _ in DIFFUSION_BITS],
    "xor_differences": [name for name, _ in XOR_DIFFERENCES],
    "projection_bits": 12,
    "projections": [name for name, _ in PROJECTIONS],
    "rotations": list(ROTATIONS),
    "structural_families": [
        "random",
        "zero-sparse",
        "one-sparse",
        "repeated-word",
        "repeated-byte",
        "counter",
        "alternating",
    ],
    "thresholds": THRESHOLDS,
}

LIMITATIONS = [
    "The experiments are deterministic bounded screens, not security proofs.",
    "Round one is reported as WEAK and is excluded from quantitative thresholds.",
    "Projection frequencies do not bound complete 512-bit differential probabilities.",
    "The structured families and chosen differences do not cover arbitrary inputs.",
]


class AnalysisError(RuntimeError):
    pass


@dataclass
class PairStats:
    pairs: int = 0
    state_total_weight: int = 0
    output_total_weight: int = 0
    minimum_state_weight: int = ref.STATE_WORDS * 128 + 1
    minimum_state_active_words: int = ref.STATE_WORDS * 2 + 1
    minimum_output_weight: int = ref.STATE_WORDS * 64 + 1
    minimum_output_active_words: int = ref.STATE_WORDS + 1
    minimum_witness: str = ""
    zero_state_differences: int = 0
    zero_output_differences: int = 0
    state_bit_ones: list[int] | None = None
    output_bit_ones: list[int] | None = None


@dataclass
class StructuralStats:
    outputs: list[tuple[int, ...]] = field(default_factory=list)
    bit_ones: list[int] = field(
        default_factory=lambda: [0] * (ref.STATE_WORDS * 64)
    )
    word_ones: list[int] = field(default_factory=lambda: [0] * ref.STATE_WORDS)
    projection_counts: dict[str, Counter[int]] = field(
        default_factory=lambda: {name: Counter() for name, _ in PROJECTIONS}
    )


def splitmix64(value: int) -> int:
    value = (value + 0x9E3779B97F4A7C15) & MASK64
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK64
    return (value ^ (value >> 31)) & MASK64


def derived_word(stream: int, sample: int, word: int) -> int:
    value = (
        SEED
        ^ ((stream + 1) * 0xD6E8FEB86659FD93)
        ^ ((sample + 1) * 0xA0761D6478BD642F)
        ^ ((word + 1) * 0xE7037ED1A0B428DB)
    ) & MASK64
    return splitmix64(value)


def random_message(sample: int) -> tuple[int, ...]:
    return tuple(derived_word(0, sample, word) for word in range(16))


def initial_work() -> tuple[list[int], tuple[int, ...]]:
    state = tuple(ref._mix_init(trail.DOMAIN))
    work = list(state) + list(ref.IV)
    work[12] ^= trail.COUNTER
    work[13] ^= ref.BLOCK_SIZE
    work[13] ^= ref.STATE_WORDS << 56
    work[14] ^= trail.DOMAIN
    work[15] ^= trail.FLAGS
    return work, state


def compression_output(
    state: tuple[int, ...], work: tuple[int, ...]
) -> tuple[int, ...]:
    return tuple(
        (state[index] ^ work[index] ^ work[index + ref.STATE_WORDS]) & MASK64
        for index in range(ref.STATE_WORDS)
    )


def trajectory(
    message: tuple[int, ...], maximum_round: int = ref.ROUNDS
) -> tuple[tuple[tuple[int, ...], tuple[int, ...]], ...]:
    if len(message) != 16:
        raise AnalysisError("compression messages must contain 16 words")
    if maximum_round < 1 or maximum_round > ref.ROUNDS:
        raise AnalysisError(f"round count must be between 1 and {ref.ROUNDS}")
    work, state = initial_work()
    results = []
    for round_index in range(maximum_round):
        trail.mix_round(work, message, round_index)
        frozen = tuple(work)
        results.append((frozen, compression_output(state, frozen)))
    return tuple(results)


def verify_reference() -> None:
    message = random_message(0)
    final_output = trajectory(message)[-1][1]
    if final_output != trail.compress(message, ref.ROUNDS):
        raise AnalysisError("round evaluator does not match the Python reference")


def xor_words(left: tuple[int, ...], right: tuple[int, ...]) -> tuple[int, ...]:
    return tuple(a ^ b for a, b in zip(left, right))


def rotate_word(value: int, amount: int) -> int:
    amount &= 63
    return ((value << amount) | (value >> ((64 - amount) & 63))) & MASK64


def rotate_words(words: tuple[int, ...], amount: int) -> tuple[int, ...]:
    return tuple(rotate_word(word, amount) for word in words)


def add_bit_counts(counts: list[int], words: tuple[int, ...]) -> None:
    for word_index, word in enumerate(words):
        offset = word_index * 64
        for bit in range(64):
            counts[offset + bit] += (word >> bit) & 1


def add_pair(
    stats: PairStats,
    state_difference: tuple[int, ...],
    output_difference: tuple[int, ...],
    witness: str,
) -> None:
    state_weight = sum(word.bit_count() for word in state_difference)
    output_weight = sum(word.bit_count() for word in output_difference)
    state_active = sum(word != 0 for word in state_difference)
    output_active = sum(word != 0 for word in output_difference)
    stats.pairs += 1
    stats.state_total_weight += state_weight
    stats.output_total_weight += output_weight
    stats.zero_state_differences += state_weight == 0
    stats.zero_output_differences += output_weight == 0
    current = (state_weight, state_active, output_weight, output_active, witness)
    previous = (
        stats.minimum_state_weight,
        stats.minimum_state_active_words,
        stats.minimum_output_weight,
        stats.minimum_output_active_words,
        stats.minimum_witness,
    )
    if current < previous:
        stats.minimum_state_weight = state_weight
        stats.minimum_state_active_words = state_active
        stats.minimum_output_weight = output_weight
        stats.minimum_output_active_words = output_active
        stats.minimum_witness = witness
    if stats.state_bit_ones is not None:
        add_bit_counts(stats.state_bit_ones, state_difference)
    if stats.output_bit_ones is not None:
        add_bit_counts(stats.output_bit_ones, output_difference)


def merge_pair_stats(target: PairStats, source: PairStats) -> None:
    target.pairs += source.pairs
    target.state_total_weight += source.state_total_weight
    target.output_total_weight += source.output_total_weight
    target.zero_state_differences += source.zero_state_differences
    target.zero_output_differences += source.zero_output_differences
    source_key = (
        source.minimum_state_weight,
        source.minimum_state_active_words,
        source.minimum_output_weight,
        source.minimum_output_active_words,
        source.minimum_witness,
    )
    target_key = (
        target.minimum_state_weight,
        target.minimum_state_active_words,
        target.minimum_output_weight,
        target.minimum_output_active_words,
        target.minimum_witness,
    )
    if source_key < target_key:
        target.minimum_state_weight = source.minimum_state_weight
        target.minimum_state_active_words = source.minimum_state_active_words
        target.minimum_output_weight = source.minimum_output_weight
        target.minimum_output_active_words = source.minimum_output_active_words
        target.minimum_witness = source.minimum_witness


def percentage(numerator: int | float, denominator: int | float) -> float:
    return round(100.0 * numerator / denominator, 6)


def bit_bias(counts: list[int], samples: int) -> tuple[float, int]:
    if samples <= 0:
        raise AnalysisError("bit-bias sample count must be positive")
    bit = max(
        range(len(counts)),
        key=lambda index: (abs(counts[index] / samples - 0.5), -index),
    )
    return round(abs(counts[bit] / samples - 0.5) * 100.0, 6), bit


def pair_summary(stats: PairStats) -> dict[str, Any]:
    if stats.pairs == 0:
        raise AnalysisError("cannot summarize an empty pair set")
    result: dict[str, Any] = {
        "pairs": stats.pairs,
        "average_state_changed_pct": percentage(
            stats.state_total_weight, stats.pairs * ref.STATE_WORDS * 128
        ),
        "minimum_state_weight": stats.minimum_state_weight,
        "minimum_state_active_words": stats.minimum_state_active_words,
        "average_output_changed_pct": percentage(
            stats.output_total_weight, stats.pairs * ref.STATE_WORDS * 64
        ),
        "minimum_output_weight": stats.minimum_output_weight,
        "minimum_output_active_words": stats.minimum_output_active_words,
        "minimum_witness": stats.minimum_witness,
        "zero_state_differences": stats.zero_state_differences,
        "zero_output_differences": stats.zero_output_differences,
    }
    if stats.state_bit_ones is not None:
        bias, bit = bit_bias(stats.state_bit_ones, stats.pairs)
        result["maximum_state_bit_bias_pct"] = bias
        result["state_bias_bit"] = bit
    if stats.output_bit_ones is not None:
        bias, bit = bit_bias(stats.output_bit_ones, stats.pairs)
        result["maximum_output_bit_bias_pct"] = bias
        result["output_bias_bit"] = bit
    return result


def project(words: tuple[int, ...], positions: tuple[int, ...]) -> int:
    value = 0
    for output_bit, position in enumerate(positions):
        word, bit = divmod(position, 64)
        value |= ((words[word] >> bit) & 1) << output_bit
    return value


def apply_word_difference(
    message: tuple[int, ...], changes: tuple[tuple[int, int], ...]
) -> tuple[int, ...]:
    words = list(message)
    for word, mask in changes:
        words[word] ^= mask
    return tuple(words)


def diffusion_status(entry: dict[str, Any]) -> str:
    invalid = (
        entry["zero_state_differences"] != 0
        or entry["zero_output_differences"] != 0
    )
    if entry["round"] >= THRESHOLDS["first_checked_round"]:
        invalid = invalid or (
            entry["minimum_state_weight"] < THRESHOLDS["minimum_state_weight"]
            or entry["minimum_state_active_words"] != ref.STATE_WORDS * 2
            or entry["minimum_output_weight"] < THRESHOLDS["minimum_output_weight"]
            or entry["minimum_output_active_words"] != ref.STATE_WORDS
            or entry["maximum_output_bit_bias_pct"]
            > THRESHOLDS["maximum_diffusion_bit_bias_pct"]
        )
    return "FAIL" if invalid else "WEAK" if entry["round"] == 1 else "PASS"


def differential_status(entry: dict[str, Any]) -> str:
    invalid = (
        entry["zero_state_differences"] != 0
        or entry["zero_output_differences"] != 0
    )
    if entry["round"] >= THRESHOLDS["first_checked_round"]:
        invalid = invalid or (
            entry["minimum_state_weight"] < THRESHOLDS["minimum_state_weight"]
            or entry["minimum_state_active_words"] != ref.STATE_WORDS * 2
            or entry["minimum_output_weight"] < THRESHOLDS["minimum_output_weight"]
            or entry["minimum_output_active_words"] != ref.STATE_WORDS
            or entry["maximum_output_bit_bias_pct"]
            > THRESHOLDS["maximum_differential_bit_bias_pct"]
            or entry["maximum_exact_count"]
            > THRESHOLDS["maximum_differential_exact_count"]
            or entry["maximum_projection_frequency"]
            > THRESHOLDS["maximum_differential_projection_frequency"]
        )
    return "FAIL" if invalid else "WEAK" if entry["round"] == 1 else "PASS"


def rotation_status(entry: dict[str, Any]) -> str:
    invalid = (
        entry["zero_state_relations"] != 0
        or entry["zero_output_relations"] != 0
    )
    if entry["round"] >= THRESHOLDS["first_checked_round"]:
        invalid = invalid or (
            entry["minimum_state_weight"] < THRESHOLDS["minimum_state_weight"]
            or entry["minimum_state_active_words"] != ref.STATE_WORDS * 2
            or entry["minimum_output_weight"] < THRESHOLDS["minimum_output_weight"]
            or entry["minimum_output_active_words"] != ref.STATE_WORDS
            or entry["maximum_rotation_average_bias_pct"]
            > THRESHOLDS["maximum_rotation_average_bias_pct"]
            or entry["maximum_output_bit_bias_pct"]
            > THRESHOLDS["maximum_rotation_bit_bias_pct"]
        )
    return "FAIL" if invalid else "WEAK" if entry["round"] == 1 else "PASS"


def structural_status(entry: dict[str, Any]) -> str:
    invalid = entry["unique_outputs"] != entry["samples"]
    if entry["round"] >= THRESHOLDS["first_checked_round"]:
        invalid = invalid or (
            entry["maximum_output_bit_bias_pct"]
            > THRESHOLDS["maximum_structural_bit_bias_pct"]
            or entry["maximum_word_mean_bias_pct"]
            > THRESHOLDS["maximum_structural_word_bias_pct"]
            or entry["maximum_projection_frequency"]
            > THRESHOLDS["maximum_structural_projection_frequency"]
        )
    return "FAIL" if invalid else "WEAK" if entry["round"] == 1 else "PASS"


def analyze_diffusion(
    bases: list[tuple[int, ...]],
    base_trajectories: list[tuple[tuple[tuple[int, ...], tuple[int, ...]], ...]],
    rounds: tuple[int, ...],
) -> list[dict[str, Any]]:
    selected = set(rounds)
    maximum_round = max(rounds)
    stats = {
        round_count: PairStats(
            state_bit_ones=[0] * (ref.STATE_WORDS * 128),
            output_bit_ones=[0] * (ref.STATE_WORDS * 64),
        )
        for round_count in rounds
    }
    for sample in range(DIFFUSION_BASES):
        for name, word, bit in DIFFUSION_BITS:
            changed = list(bases[sample])
            changed[word] ^= 1 << bit
            changed_trajectory = trajectory(tuple(changed), maximum_round)
            for round_index in range(maximum_round):
                round_count = round_index + 1
                if round_count not in selected:
                    continue
                base_work, base_output = base_trajectories[sample][round_index]
                changed_work, changed_output = changed_trajectory[round_index]
                add_pair(
                    stats[round_count],
                    xor_words(base_work, changed_work),
                    xor_words(base_output, changed_output),
                    f"sample-{sample}:{name}",
                )
    results = []
    for round_count in rounds:
        entry = {"round": round_count, **pair_summary(stats[round_count])}
        entry["status"] = diffusion_status(entry)
        results.append(entry)
    return results


def analyze_differentials(
    bases: list[tuple[int, ...]],
    base_trajectories: list[tuple[tuple[tuple[int, ...], tuple[int, ...]], ...]],
    rounds: tuple[int, ...],
) -> list[dict[str, Any]]:
    selected = set(rounds)
    maximum_round = max(rounds)
    grouped = {
        name: {
            round_count: PairStats(
                output_bit_ones=[0] * (ref.STATE_WORDS * 64)
            )
            for round_count in rounds
        }
        for name, _ in XOR_DIFFERENCES
    }
    exact = {
        name: {round_count: Counter() for round_count in rounds}
        for name, _ in XOR_DIFFERENCES
    }
    projected = {
        name: {
            round_count: {projection: Counter() for projection, _ in PROJECTIONS}
            for round_count in rounds
        }
        for name, _ in XOR_DIFFERENCES
    }
    for name, changes in XOR_DIFFERENCES:
        for sample, base in enumerate(bases):
            changed_trajectory = trajectory(
                apply_word_difference(base, changes), maximum_round
            )
            for round_index in range(maximum_round):
                round_count = round_index + 1
                if round_count not in selected:
                    continue
                base_work, base_output = base_trajectories[sample][round_index]
                changed_work, changed_output = changed_trajectory[round_index]
                output_difference = xor_words(base_output, changed_output)
                add_pair(
                    grouped[name][round_count],
                    xor_words(base_work, changed_work),
                    output_difference,
                    f"{name}:sample-{sample}",
                )
                exact[name][round_count][output_difference] += 1
                for projection, positions in PROJECTIONS:
                    projected[name][round_count][projection][
                        project(output_difference, positions)
                    ] += 1

    results = []
    for round_count in rounds:
        combined = PairStats()
        maximum_bit_bias = (-1.0, "", -1)
        maximum_exact = (0, "")
        maximum_projection = (0, "", "")
        for name, _ in XOR_DIFFERENCES:
            group = grouped[name][round_count]
            merge_pair_stats(combined, group)
            bias, bit = bit_bias(group.output_bit_ones or [], group.pairs)
            maximum_bit_bias = max(maximum_bit_bias, (bias, name, -bit))
            exact_count = max(exact[name][round_count].values())
            maximum_exact = max(maximum_exact, (exact_count, name))
            for projection, _ in PROJECTIONS:
                count = max(projected[name][round_count][projection].values())
                maximum_projection = max(
                    maximum_projection, (count, name, projection)
                )
        summary = pair_summary(combined)
        entry = {
            "round": round_count,
            "differences": len(XOR_DIFFERENCES),
            **summary,
            "maximum_output_bit_bias_pct": maximum_bit_bias[0],
            "bit_bias_difference": maximum_bit_bias[1],
            "output_bias_bit": -maximum_bit_bias[2],
            "maximum_exact_count": maximum_exact[0],
            "maximum_exact_frequency": round(maximum_exact[0] / len(bases), 9),
            "exact_difference": maximum_exact[1],
            "maximum_projection_count": maximum_projection[0],
            "maximum_projection_frequency": round(
                maximum_projection[0] / len(bases), 9
            ),
            "projection_difference": maximum_projection[1],
            "projection": maximum_projection[2],
        }
        entry["status"] = differential_status(entry)
        results.append(entry)
    return results


def analyze_rotations(
    bases: list[tuple[int, ...]],
    base_trajectories: list[tuple[tuple[tuple[int, ...], tuple[int, ...]], ...]],
    rounds: tuple[int, ...],
) -> list[dict[str, Any]]:
    selected = set(rounds)
    maximum_round = max(rounds)
    grouped = {
        rotation: {
            round_count: PairStats(
                output_bit_ones=[0] * (ref.STATE_WORDS * 64)
            )
            for round_count in rounds
        }
        for rotation in ROTATIONS
    }
    for rotation in ROTATIONS:
        for sample, base in enumerate(bases):
            rotated_trajectory = trajectory(rotate_words(base, rotation), maximum_round)
            for round_index in range(maximum_round):
                round_count = round_index + 1
                if round_count not in selected:
                    continue
                base_work, base_output = base_trajectories[sample][round_index]
                rotated_work, rotated_output = rotated_trajectory[round_index]
                add_pair(
                    grouped[rotation][round_count],
                    xor_words(rotate_words(base_work, rotation), rotated_work),
                    xor_words(rotate_words(base_output, rotation), rotated_output),
                    f"rotate-{rotation}:sample-{sample}",
                )

    results = []
    for round_count in rounds:
        combined = PairStats()
        average_bias = (-1.0, -1)
        bit_bias_value = (-1.0, -1, -1)
        for rotation in ROTATIONS:
            group = grouped[rotation][round_count]
            merge_pair_stats(combined, group)
            mean = percentage(
                group.output_total_weight, group.pairs * ref.STATE_WORDS * 64
            )
            average_bias = max(average_bias, (round(abs(mean - 50.0), 6), rotation))
            bias, bit = bit_bias(group.output_bit_ones or [], group.pairs)
            bit_bias_value = max(bit_bias_value, (bias, rotation, -bit))
        summary = pair_summary(combined)
        zero_state_relations = summary.pop("zero_state_differences")
        zero_output_relations = summary.pop("zero_output_differences")
        entry = {
            "round": round_count,
            "rotations": len(ROTATIONS),
            **summary,
            "maximum_rotation_average_bias_pct": average_bias[0],
            "average_bias_rotation": average_bias[1],
            "maximum_output_bit_bias_pct": bit_bias_value[0],
            "bit_bias_rotation": bit_bias_value[1],
            "output_bias_bit": -bit_bias_value[2],
            "zero_state_relations": zero_state_relations,
            "zero_output_relations": zero_output_relations,
        }
        entry["status"] = rotation_status(entry)
        results.append(entry)
    return results


def repeated_byte(value: int) -> int:
    return (value & 0xFF) * 0x0101010101010101


def structured_message(family: str, sample: int) -> tuple[int, ...]:
    if family == "random":
        return tuple(derived_word(1, sample, word) for word in range(16))
    if family == "zero-sparse":
        words = [0] * 16
        words[sample % 16] = derived_word(2, sample, sample % 16) | 1
        return tuple(words)
    if family == "one-sparse":
        words = [MASK64] * 16
        words[sample % 16] ^= derived_word(3, sample, sample % 16) | 1
        return tuple(words)
    if family == "repeated-word":
        value = derived_word(4, sample, 0)
        return (value,) * 16
    if family == "repeated-byte":
        value = repeated_byte((sample * 197 + 53) & 0xFF)
        return (value,) * 16
    if family == "counter":
        start = derived_word(5, sample, 0)
        return tuple((start + word) & MASK64 for word in range(16))
    if family == "alternating":
        words = [
            0xAAAAAAAAAAAAAAAA if word % 2 == 0 else 0x5555555555555555
            for word in range(16)
        ]
        words[sample % 16] ^= derived_word(6, sample, sample % 16)
        return tuple(words)
    raise AnalysisError(f"unknown structural family: {family}")


def analyze_structural_bias(
    rounds: tuple[int, ...], samples: int
) -> list[dict[str, Any]]:
    selected = set(rounds)
    maximum_round = max(rounds)
    families = tuple(PROFILE["structural_families"])
    all_messages = [
        structured_message(family, sample)
        for family in families
        for sample in range(samples)
    ]
    if len(set(all_messages)) != len(all_messages):
        raise AnalysisError("structural input families contain a duplicate message")
    stats = {
        family: {round_count: StructuralStats() for round_count in rounds}
        for family in families
    }
    for family in families:
        for sample in range(samples):
            values = trajectory(structured_message(family, sample), maximum_round)
            for round_index in range(maximum_round):
                round_count = round_index + 1
                if round_count not in selected:
                    continue
                output = values[round_index][1]
                current = stats[family][round_count]
                current.outputs.append(output)
                add_bit_counts(current.bit_ones, output)
                for word, value in enumerate(output):
                    current.word_ones[word] += value.bit_count()
                for projection, positions in PROJECTIONS:
                    current.projection_counts[projection][project(output, positions)] += 1

    results = []
    for round_count in rounds:
        for family in families:
            current = stats[family][round_count]
            bias, bit = bit_bias(current.bit_ones, samples)
            word_biases = [
                abs(current.word_ones[word] / (samples * 64) - 0.5) * 100.0
                for word in range(ref.STATE_WORDS)
            ]
            word = max(
                range(ref.STATE_WORDS),
                key=lambda index: (word_biases[index], -index),
            )
            projection_count, projection = max(
                (
                    max(current.projection_counts[name].values()),
                    name,
                )
                for name, _ in PROJECTIONS
            )
            total_ones = sum(current.word_ones)
            entry = {
                "round": round_count,
                "family": family,
                "samples": samples,
                "mean_output_one_pct": percentage(
                    total_ones, samples * ref.STATE_WORDS * 64
                ),
                "maximum_output_bit_bias_pct": bias,
                "output_bias_bit": bit,
                "maximum_word_mean_bias_pct": round(word_biases[word], 6),
                "word_bias_word": word,
                "maximum_projection_count": projection_count,
                "maximum_projection_frequency": round(
                    projection_count / samples, 9
                ),
                "projection": projection,
                "unique_outputs": len(set(current.outputs)),
            }
            entry["status"] = structural_status(entry)
            results.append(entry)
    return results


def run_analysis(
    rounds: tuple[int, ...] = ROUNDS, samples: int = SAMPLES
) -> dict[str, list[dict[str, Any]]]:
    if not rounds or any(round_count not in ROUNDS for round_count in rounds):
        raise AnalysisError("analysis rounds are outside the supported range")
    if tuple(sorted(set(rounds))) != rounds:
        raise AnalysisError("analysis rounds must be unique and increasing")
    if samples < DIFFUSION_BASES:
        raise AnalysisError(
            f"analysis requires at least {DIFFUSION_BASES} base samples"
        )
    maximum_round = max(rounds)
    bases = [random_message(sample) for sample in range(samples)]
    base_trajectories = [trajectory(message, maximum_round) for message in bases]
    return {
        "diffusion": analyze_diffusion(bases, base_trajectories, rounds),
        "differential": analyze_differentials(
            bases, base_trajectories, rounds
        ),
        "rotation_symmetry": analyze_rotations(
            bases, base_trajectories, rounds
        ),
        "structural_bias": analyze_structural_bias(rounds, samples),
    }


def expected_summary(experiments: dict[str, list[dict[str, Any]]]) -> dict[str, Any]:
    entries = [entry for rows in experiments.values() for entry in rows]
    failures = [
        f"{experiment}:r{entry['round']}"
        + (f":{entry['family']}" if "family" in entry else "")
        for experiment, rows in experiments.items()
        for entry in rows
        if entry["status"] == "FAIL"
    ]
    return {
        "status": "FAIL" if failures else "PASS",
        "result_count": len(entries),
        "weak_result_count": sum(entry["status"] == "WEAK" for entry in entries),
        "failures": failures,
    }


def capture_document() -> dict[str, Any]:
    verify_reference()
    experiments = run_analysis()
    document = {
        "schema": SCHEMA,
        "profile": PROFILE,
        "experiments": experiments,
        "summary": expected_summary(experiments),
        "limitations": LIMITATIONS,
    }
    return validate_document(document)


STATUS_FUNCTIONS: dict[str, Callable[[dict[str, Any]], str]] = {
    "diffusion": diffusion_status,
    "differential": differential_status,
    "rotation_symmetry": rotation_status,
    "structural_bias": structural_status,
}

RESULT_FIELDS = {
    "diffusion": {
        "round",
        "pairs",
        "average_state_changed_pct",
        "minimum_state_weight",
        "minimum_state_active_words",
        "average_output_changed_pct",
        "minimum_output_weight",
        "minimum_output_active_words",
        "minimum_witness",
        "zero_state_differences",
        "zero_output_differences",
        "maximum_state_bit_bias_pct",
        "state_bias_bit",
        "maximum_output_bit_bias_pct",
        "output_bias_bit",
        "status",
    },
    "differential": {
        "round",
        "differences",
        "pairs",
        "average_state_changed_pct",
        "minimum_state_weight",
        "minimum_state_active_words",
        "average_output_changed_pct",
        "minimum_output_weight",
        "minimum_output_active_words",
        "minimum_witness",
        "zero_state_differences",
        "zero_output_differences",
        "maximum_output_bit_bias_pct",
        "bit_bias_difference",
        "output_bias_bit",
        "maximum_exact_count",
        "maximum_exact_frequency",
        "exact_difference",
        "maximum_projection_count",
        "maximum_projection_frequency",
        "projection_difference",
        "projection",
        "status",
    },
    "rotation_symmetry": {
        "round",
        "rotations",
        "pairs",
        "average_state_changed_pct",
        "minimum_state_weight",
        "minimum_state_active_words",
        "average_output_changed_pct",
        "minimum_output_weight",
        "minimum_output_active_words",
        "minimum_witness",
        "maximum_rotation_average_bias_pct",
        "average_bias_rotation",
        "maximum_output_bit_bias_pct",
        "bit_bias_rotation",
        "output_bias_bit",
        "zero_state_relations",
        "zero_output_relations",
        "status",
    },
    "structural_bias": {
        "round",
        "family",
        "samples",
        "mean_output_one_pct",
        "maximum_output_bit_bias_pct",
        "output_bias_bit",
        "maximum_word_mean_bias_pct",
        "word_bias_word",
        "maximum_projection_count",
        "maximum_projection_frequency",
        "projection",
        "unique_outputs",
        "status",
    },
}


def validate_document(document: Any) -> dict[str, Any]:
    if not isinstance(document, dict):
        raise AnalysisError("analysis root is not an object")
    if set(document) != {
        "schema",
        "profile",
        "experiments",
        "summary",
        "limitations",
    }:
        raise AnalysisError("analysis root has an invalid field set")
    if document["schema"] != SCHEMA:
        raise AnalysisError(f"analysis schema must be {SCHEMA}")
    if document["profile"] != PROFILE:
        raise AnalysisError("analysis profile does not match reduced-round-v1")
    if document["limitations"] != LIMITATIONS:
        raise AnalysisError("analysis limitations do not match reduced-round-v1")
    experiments = document["experiments"]
    if not isinstance(experiments, dict) or set(experiments) != set(STATUS_FUNCTIONS):
        raise AnalysisError("analysis experiment set is incomplete")
    for experiment, status_function in STATUS_FUNCTIONS.items():
        rows = experiments[experiment]
        if not isinstance(rows, list):
            raise AnalysisError(f"{experiment} results are not a list")
        expected_keys: set[Any]
        if experiment == "structural_bias":
            expected_keys = {
                (round_count, family)
                for round_count in ROUNDS
                for family in PROFILE["structural_families"]
            }
            keys = {
                (row.get("round"), row.get("family"))
                for row in rows
                if isinstance(row, dict)
            }
        else:
            expected_keys = set(ROUNDS)
            keys = {
                row.get("round") for row in rows if isinstance(row, dict)
            }
        if len(rows) != len(expected_keys) or keys != expected_keys:
            raise AnalysisError(f"{experiment} does not cover the complete profile")
        for row in rows:
            if not isinstance(row, dict):
                raise AnalysisError(f"{experiment} contains a non-object result")
            if set(row) != RESULT_FIELDS[experiment]:
                raise AnalysisError(f"{experiment} contains an invalid result field set")
            for value in row.values():
                if isinstance(value, float) and not math.isfinite(value):
                    raise AnalysisError(f"{experiment} contains a non-finite value")
            if row.get("status") != status_function(row):
                raise AnalysisError(f"{experiment} contains an invalid status")
    summary = expected_summary(experiments)
    if document["summary"] != summary:
        raise AnalysisError("analysis summary does not match its results")
    if summary["status"] != "PASS":
        raise AnalysisError(
            "analysis contains threshold failures: " + ", ".join(summary["failures"])
        )
    return document


def load_document(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            document = json.load(handle)
    except OSError as error:
        raise AnalysisError(f"could not read {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise AnalysisError(f"invalid JSON in {path}: {error}") from error
    return validate_document(document)


def write_document(path: Path, document: dict[str, Any]) -> None:
    destination = path.expanduser().resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = ""
    try:
        with tempfile.NamedTemporaryFile(
            "w",
            encoding="utf-8",
            dir=destination.parent,
            prefix=f".{destination.name}.",
            suffix=".tmp",
            delete=False,
        ) as handle:
            temporary = handle.name
            json.dump(document, handle, indent=2, sort_keys=True)
            handle.write("\n")
        os.replace(temporary, destination)
    except OSError as error:
        if temporary:
            try:
                os.unlink(temporary)
            except OSError:
                pass
        raise AnalysisError(f"could not write {destination}: {error}") from error


def first_difference(left: Any, right: Any, path: str = "$") -> str | None:
    if type(left) is not type(right):
        return f"{path}: type changed"
    if isinstance(left, dict):
        if set(left) != set(right):
            return f"{path}: field set changed"
        for key in sorted(left):
            difference = first_difference(left[key], right[key], f"{path}.{key}")
            if difference is not None:
                return difference
        return None
    if isinstance(left, list):
        if len(left) != len(right):
            return f"{path}: length changed"
        for index, (left_value, right_value) in enumerate(zip(left, right)):
            difference = first_difference(
                left_value, right_value, f"{path}[{index}]"
            )
            if difference is not None:
                return difference
        return None
    if left != right:
        return f"{path}: {left!r} != {right!r}"
    return None


def print_summary(document: dict[str, Any]) -> None:
    experiments = document["experiments"]
    diffusion = {row["round"]: row for row in experiments["diffusion"]}
    differential = {row["round"]: row for row in experiments["differential"]}
    rotation = {row["round"]: row for row in experiments["rotation_symmetry"]}
    structural: dict[int, dict[str, Any]] = {}
    for row in experiments["structural_bias"]:
        current = structural.get(row["round"])
        if current is None or (
            row["maximum_output_bit_bias_pct"], row["family"]
        ) > (current["maximum_output_bit_bias_pct"], current["family"]):
            structural[row["round"]] = row
    print(
        "round,diffusion_output_avg_pct,differential_max_projection_frequency,"
        "rotation_max_average_bias_pct,structural_max_bit_bias_pct,status"
    )
    for round_count in (1, 2, 4, 8, 12, 16):
        statuses = {
            diffusion[round_count]["status"],
            differential[round_count]["status"],
            rotation[round_count]["status"],
            *(
                row["status"]
                for row in experiments["structural_bias"]
                if row["round"] == round_count
            ),
        }
        status = "FAIL" if "FAIL" in statuses else "WEAK" if "WEAK" in statuses else "PASS"
        print(
            f"{round_count},"
            f"{diffusion[round_count]['average_output_changed_pct']:.6f},"
            f"{differential[round_count]['maximum_projection_frequency']:.9f},"
            f"{rotation[round_count]['maximum_rotation_average_bias_pct']:.6f},"
            f"{structural[round_count]['maximum_output_bit_bias_pct']:.6f},"
            f"{status}"
        )
    print(
        f"REDUCED_ROUND_ANALYSIS: {document['summary']['status']} "
        "(deterministic bounded experiments; not a security proof)"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    capture = subparsers.add_parser("capture")
    capture.add_argument("--output", type=Path, required=True)
    check = subparsers.add_parser("check")
    check.add_argument("--baseline", type=Path, required=True)
    validate = subparsers.add_parser("validate")
    validate.add_argument("--file", type=Path, required=True)
    args = parser.parse_args()

    try:
        if args.command == "capture":
            document = capture_document()
            write_document(args.output, document)
            print_summary(document)
            print(f"wrote {args.output}")
            return 0
        if args.command == "validate":
            document = load_document(args.file)
            print_summary(document)
            print(f"validated {args.file}")
            return 0
        baseline = load_document(args.baseline)
        current = capture_document()
        difference = first_difference(baseline, current)
        print_summary(current)
        if difference is not None:
            print(f"REDUCED_ROUND_BASELINE: FAIL: {difference}", file=sys.stderr)
            return 1
        print("REDUCED_ROUND_BASELINE: PASS")
        return 0
    except AnalysisError as error:
        print(f"REDUCED_ROUND_ANALYSIS: ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
