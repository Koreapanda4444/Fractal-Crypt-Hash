from __future__ import annotations

import argparse
import sys
from collections import Counter
from dataclasses import dataclass

import fch_reference as ref
import fch_trail_search as trail


MASK64 = (1 << 64) - 1
DEFAULT_ROUNDS = tuple(range(1, 9))
DEFAULT_SAMPLES = 32
MIN_STATE_WEIGHT = 256
MIN_OUTPUT_WEIGHT = 128


@dataclass(frozen=True)
class InputDifference:
    name: str
    word: int
    mask: int


@dataclass
class RoundAccumulator:
    rounds: int
    minimum_state_weight: int = ref.STATE_WORDS * 128 + 1
    minimum_state_active_words: int = ref.STATE_WORDS * 2 + 1
    minimum_output_weight: int = ref.STATE_WORDS * 64 + 1
    minimum_output_active_words: int = ref.STATE_WORDS + 1
    minimum_candidate: str = ""
    minimum_sample: int = 0
    maximum_characteristic_count: int = 0
    characteristic_candidate: str = ""
    characteristic_sample: int = 0
    zero_state_differences: int = 0
    zero_output_differences: int = 0


def splitmix64(value: int) -> int:
    value = (value + 0x9E3779B97F4A7C15) & MASK64
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK64
    return (value ^ (value >> 31)) & MASK64


def sample_message(sample: int) -> tuple[int, ...]:
    return tuple(
        trail.BASE_WORDS[index]
        ^ splitmix64((sample << 8) ^ index ^ 0x6A09E667F3BCC909)
        for index in range(16)
    )


def input_differences() -> tuple[InputDifference, ...]:
    return tuple(
        InputDifference(f"word-{word}-bit-{bit}", word, 1 << bit)
        for word in range(16)
        for bit in range(64)
    )


def apply_difference(
    message: tuple[int, ...], difference: InputDifference
) -> tuple[int, ...]:
    words = list(message)
    words[difference.word] ^= difference.mask
    return tuple(words)


def initial_work() -> tuple[list[int], tuple[int, ...]]:
    state = ref._mix_init(trail.DOMAIN)
    work = list(state) + list(ref.IV)
    work[12] ^= trail.COUNTER
    work[13] ^= ref.BLOCK_SIZE
    work[13] ^= ref.STATE_WORDS << 56
    work[14] ^= trail.DOMAIN
    work[15] ^= trail.FLAGS
    return work, state


def work_trajectory(
    message: tuple[int, ...], rounds: int
) -> tuple[tuple[int, ...], ...]:
    work, _ = initial_work()
    states = []
    for round_index in range(rounds):
        trail.mix_round(work, message, round_index)
        states.append(tuple(work))
    return tuple(states)


def compression_output(
    state: tuple[int, ...], work: tuple[int, ...]
) -> tuple[int, ...]:
    return tuple(
        (state[index] ^ work[index] ^ work[index + ref.STATE_WORDS]) & MASK64
        for index in range(ref.STATE_WORDS)
    )


def output_difference(state_difference: tuple[int, ...]) -> tuple[int, ...]:
    return tuple(
        state_difference[index] ^ state_difference[index + ref.STATE_WORDS]
        for index in range(ref.STATE_WORDS)
    )


def verify_reference() -> None:
    message = sample_message(0)
    trajectory = work_trajectory(message, ref.ROUNDS)
    _, state = initial_work()
    if compression_output(state, trajectory[-1]) != trail.compress(message, ref.ROUNDS):
        raise RuntimeError("characteristic evaluator does not match the reference")


def update_minimum(
    accumulator: RoundAccumulator,
    state_difference: tuple[int, ...],
    output: tuple[int, ...],
    candidate: InputDifference,
    sample: int,
) -> None:
    state_weight = sum(word.bit_count() for word in state_difference)
    state_active_words = sum(word != 0 for word in state_difference)
    output_weight = sum(word.bit_count() for word in output)
    output_active_words = sum(word != 0 for word in output)
    if state_weight == 0:
        accumulator.zero_state_differences += 1
    if output_weight == 0:
        accumulator.zero_output_differences += 1
    current = (
        state_weight,
        state_active_words,
        output_weight,
        output_active_words,
        candidate.name,
        sample,
    )
    minimum = (
        accumulator.minimum_state_weight,
        accumulator.minimum_state_active_words,
        accumulator.minimum_output_weight,
        accumulator.minimum_output_active_words,
        accumulator.minimum_candidate,
        accumulator.minimum_sample,
    )
    if current < minimum:
        accumulator.minimum_state_weight = state_weight
        accumulator.minimum_state_active_words = state_active_words
        accumulator.minimum_output_weight = output_weight
        accumulator.minimum_output_active_words = output_active_words
        accumulator.minimum_candidate = candidate.name
        accumulator.minimum_sample = sample


def analyze(
    rounds: tuple[int, ...], samples: int, candidates: tuple[InputDifference, ...]
) -> tuple[RoundAccumulator, ...]:
    maximum_rounds = max(rounds)
    selected_rounds = set(rounds)
    messages = [sample_message(sample) for sample in range(samples)]
    base_trajectories = [
        work_trajectory(message, maximum_rounds) for message in messages
    ]
    accumulators = {round_count: RoundAccumulator(round_count) for round_count in rounds}

    for candidate in candidates:
        counters = {round_count: Counter() for round_count in rounds}
        witnesses = {round_count: {} for round_count in rounds}
        for sample, message in enumerate(messages):
            changed = apply_difference(message, candidate)
            changed_trajectory = work_trajectory(changed, maximum_rounds)
            characteristic = []
            for round_index in range(maximum_rounds):
                state_difference = tuple(
                    base_trajectories[sample][round_index][word]
                    ^ changed_trajectory[round_index][word]
                    for word in range(ref.STATE_WORDS * 2)
                )
                characteristic.extend(state_difference)
                round_count = round_index + 1
                if round_count not in selected_rounds:
                    continue
                signature = tuple(characteristic)
                counters[round_count][signature] += 1
                witnesses[round_count].setdefault(signature, sample)
                update_minimum(
                    accumulators[round_count],
                    state_difference,
                    output_difference(state_difference),
                    candidate,
                    sample,
                )

        for round_count in rounds:
            signature, count = max(
                counters[round_count].items(),
                key=lambda item: (item[1], item[0]),
            )
            accumulator = accumulators[round_count]
            if count > accumulator.maximum_characteristic_count:
                accumulator.maximum_characteristic_count = count
                accumulator.characteristic_candidate = candidate.name
                accumulator.characteristic_sample = witnesses[round_count][signature]

    return tuple(accumulators[round_count] for round_count in rounds)


def parse_rounds(text: str) -> tuple[int, ...]:
    try:
        rounds = tuple(dict.fromkeys(int(value) for value in text.split(",")))
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "rounds must be comma-separated integers"
        ) from error
    if not rounds or any(value < 1 or value > ref.ROUNDS for value in rounds):
        raise argparse.ArgumentTypeError(f"rounds must be between 1 and {ref.ROUNDS}")
    return rounds


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rounds", type=parse_rounds, default=DEFAULT_ROUNDS)
    parser.add_argument("--samples", type=int, default=DEFAULT_SAMPLES)
    parser.add_argument("--candidate-limit", type=int, default=1024)
    args = parser.parse_args()
    candidates = input_differences()
    if args.samples < 2:
        parser.error("samples must be at least 2")
    if args.candidate_limit < 1 or args.candidate_limit > len(candidates):
        parser.error(f"candidate limit must be between 1 and {len(candidates)}")

    try:
        verify_reference()
        selected_candidates = candidates[: args.candidate_limit]
        results = analyze(args.rounds, args.samples, selected_candidates)
        print(
            "full_state_characteristic,rounds,candidates,samples,pairs,"
            "min_state_weight,min_state_active_words,min_output_weight,"
            "min_output_active_words,max_exact_count,max_exact_frequency,"
            "min_candidate,min_sample,characteristic_candidate,"
            "characteristic_sample,status"
        )
        failed = False
        for result in results:
            weak = result.rounds == 1
            invalid = (
                result.zero_state_differences != 0
                or result.zero_output_differences != 0
                or (
                    result.rounds >= 2
                    and (
                        result.minimum_state_weight < MIN_STATE_WEIGHT
                        or result.minimum_state_active_words != ref.STATE_WORDS * 2
                        or result.minimum_output_weight < MIN_OUTPUT_WEIGHT
                        or result.minimum_output_active_words != ref.STATE_WORDS
                        or result.maximum_characteristic_count > 1
                    )
                )
            )
            status = "FAIL" if invalid else "WEAK" if weak else "PASS"
            print(
                f"full_state_characteristic,{result.rounds},"
                f"{len(selected_candidates)},{args.samples},"
                f"{len(selected_candidates) * args.samples},"
                f"{result.minimum_state_weight},"
                f"{result.minimum_state_active_words},"
                f"{result.minimum_output_weight},"
                f"{result.minimum_output_active_words},"
                f"{result.maximum_characteristic_count},"
                f"{result.maximum_characteristic_count / args.samples:.6f},"
                f"{result.minimum_candidate},{result.minimum_sample},"
                f"{result.characteristic_candidate},"
                f"{result.characteristic_sample},{status}"
            )
            failed = failed or invalid
        if failed:
            print("CHARACTERISTIC_SEARCH: FAIL")
            return 1
        print(
            "CHARACTERISTIC_SEARCH: PASS "
            "(bounded full-state single-bit search; not a security proof)"
        )
        return 0
    except RuntimeError as error:
        print(f"CHARACTERISTIC_SEARCH: ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
