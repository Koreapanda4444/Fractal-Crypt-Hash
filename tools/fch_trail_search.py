from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass

import fch_reference as ref


MASK64 = (1 << 64) - 1
COUNTER = 0x0123456789ABCDEF
DOMAIN = ref.DOMAIN_LEAF
FLAGS = ref.FLAG_LEAF_DATA | ref.FLAG_FINAL
BASE_WORDS = tuple(
    (ref.IV[index & 7] ^ ((index + 1) * 0x9E3779B97F4A7C15)) & MASK64
    for index in range(16)
)


@dataclass(frozen=True)
class Family:
    name: str
    word: int
    byte: int
    delta: int


@dataclass(frozen=True)
class Result:
    rounds: int
    family: Family
    minimum_weight: int
    minimum_active_words: int
    witness: int
    zero_pairs: int
    linear_coefficient: int
    linear_mask: int
    linear_output_bit: int


FAMILIES = (
    Family("word0-byte0-xor01", 0, 0, 0x01),
    Family("word15-byte7-xor80", 15, 7, 0x80),
)


def rotr64(value: int, count: int) -> int:
    count &= 63
    return ((value >> count) | (value << ((64 - count) & 63))) & MASK64


def mix_g(
    work: list[int],
    a: int,
    b: int,
    c: int,
    d: int,
    x: int,
    y: int,
) -> None:
    work[a] = (work[a] + work[b] + x) & MASK64
    work[d] = rotr64(work[d] ^ work[a], 32)
    work[c] = (work[c] + work[d]) & MASK64
    work[b] = rotr64(work[b] ^ work[c], 24)
    work[a] = (work[a] + work[b] + y) & MASK64
    work[d] = rotr64(work[d] ^ work[a], 16)
    work[c] = (work[c] + work[d]) & MASK64
    work[b] = rotr64(work[b] ^ work[c], 63)


def mix_round(work: list[int], message: tuple[int, ...], round_index: int) -> None:
    schedule = ref.SIGMA[round_index]
    mix_g(work, 0, 4, 8, 12, message[schedule[0]], message[schedule[1]])
    mix_g(work, 1, 5, 9, 13, message[schedule[2]], message[schedule[3]])
    mix_g(work, 2, 6, 10, 14, message[schedule[4]], message[schedule[5]])
    mix_g(work, 3, 7, 11, 15, message[schedule[6]], message[schedule[7]])
    mix_g(work, 0, 5, 10, 15, message[schedule[8]], message[schedule[9]])
    mix_g(work, 1, 6, 11, 12, message[schedule[10]], message[schedule[11]])
    mix_g(work, 2, 7, 8, 13, message[schedule[12]], message[schedule[13]])
    mix_g(work, 3, 4, 9, 14, message[schedule[14]], message[schedule[15]])


def compress(message: tuple[int, ...], rounds: int) -> tuple[int, ...]:
    state = ref._mix_init(DOMAIN)
    work = list(state) + list(ref.IV)
    work[12] ^= COUNTER
    work[13] ^= ref.BLOCK_SIZE
    work[13] ^= ref.STATE_WORDS << 56
    work[14] ^= DOMAIN
    work[15] ^= FLAGS

    for round_index in range(rounds):
        mix_round(work, message, round_index)

    return tuple(
        (state[index] ^ work[index] ^ work[index + ref.STATE_WORDS]) & MASK64
        for index in range(ref.STATE_WORDS)
    )


def replace_byte(message: tuple[int, ...], family: Family, value: int) -> tuple[int, ...]:
    words = list(message)
    shift = family.byte * 8
    words[family.word] = (
        (words[family.word] & ~(0xFF << shift)) | ((value & 0xFF) << shift)
    ) & MASK64
    return tuple(words)


def output_for(value: int, family: Family, rounds: int) -> tuple[int, ...]:
    return compress(replace_byte(BASE_WORDS, family, value), rounds)


def output_integer(output: tuple[int, ...]) -> int:
    combined = 0
    for index, word in enumerate(output):
        combined |= word << (index * 64)
    return combined


def walsh_hadamard(values: list[int]) -> None:
    width = 1
    while width < len(values):
        for offset in range(0, len(values), width * 2):
            for index in range(offset, offset + width):
                left = values[index]
                right = values[index + width]
                values[index] = left + right
                values[index + width] = left - right
        width *= 2


def linear_screen(outputs: list[int]) -> tuple[int, int, int]:
    strongest = 0
    strongest_mask = 0
    strongest_bit = 0
    for bit in range(ref.STATE_WORDS * 64):
        spectrum = [1 if ((output >> bit) & 1) == 0 else -1 for output in outputs]
        walsh_hadamard(spectrum)
        for mask in range(1, 256):
            coefficient = abs(spectrum[mask])
            if coefficient > strongest:
                strongest = coefficient
                strongest_mask = mask
                strongest_bit = bit
    return strongest, strongest_mask, strongest_bit


def analyze_family(family: Family, rounds: int) -> Result:
    outputs = [output_for(value, family, rounds) for value in range(256)]
    output_integers = [output_integer(output) for output in outputs]
    minimum_weight = ref.STATE_WORDS * 64 + 1
    minimum_active_words = ref.STATE_WORDS + 1
    witness = 0
    zero_pairs = 0

    for value in range(256):
        paired = value ^ family.delta
        differences = tuple(
            outputs[value][index] ^ outputs[paired][index]
            for index in range(ref.STATE_WORDS)
        )
        weight = sum(word.bit_count() for word in differences)
        active_words = sum(word != 0 for word in differences)
        if weight == 0:
            zero_pairs += 1
        if (weight, active_words, value) < (
            minimum_weight,
            minimum_active_words,
            witness,
        ):
            minimum_weight = weight
            minimum_active_words = active_words
            witness = value

    coefficient, input_mask, output_bit = linear_screen(output_integers)
    return Result(
        rounds,
        family,
        minimum_weight,
        minimum_active_words,
        witness,
        zero_pairs,
        coefficient,
        input_mask,
        output_bit,
    )


def verify_reference() -> None:
    block = b"".join(word.to_bytes(8, "little") for word in BASE_WORDS)
    state = ref._mix_init(DOMAIN)
    ref._compress(state, block, ref.BLOCK_SIZE, COUNTER, DOMAIN, FLAGS)
    if tuple(state) != compress(BASE_WORDS, ref.ROUNDS):
        raise RuntimeError("trail evaluator does not match the Python reference")


def symbolic_g(z3, work, a, b, c, d, x, y) -> None:
    work[a] = work[a] + work[b] + x
    work[d] = z3.RotateRight(work[d] ^ work[a], 32)
    work[c] = work[c] + work[d]
    work[b] = z3.RotateRight(work[b] ^ work[c], 24)
    work[a] = work[a] + work[b] + y
    work[d] = z3.RotateRight(work[d] ^ work[a], 16)
    work[c] = work[c] + work[d]
    work[b] = z3.RotateRight(work[b] ^ work[c], 63)


def symbolic_compress(z3, message, rounds: int):
    state = [z3.BitVecVal(word, 64) for word in ref._mix_init(DOMAIN)]
    work = list(state) + [z3.BitVecVal(word, 64) for word in ref.IV]
    work[12] = work[12] ^ z3.BitVecVal(COUNTER, 64)
    work[13] = work[13] ^ z3.BitVecVal(ref.BLOCK_SIZE, 64)
    work[13] = work[13] ^ z3.BitVecVal(ref.STATE_WORDS << 56, 64)
    work[14] = work[14] ^ z3.BitVecVal(DOMAIN, 64)
    work[15] = work[15] ^ z3.BitVecVal(FLAGS, 64)

    for round_index in range(rounds):
        schedule = ref.SIGMA[round_index]
        symbolic_g(z3, work, 0, 4, 8, 12, message[schedule[0]], message[schedule[1]])
        symbolic_g(z3, work, 1, 5, 9, 13, message[schedule[2]], message[schedule[3]])
        symbolic_g(z3, work, 2, 6, 10, 14, message[schedule[4]], message[schedule[5]])
        symbolic_g(z3, work, 3, 7, 11, 15, message[schedule[6]], message[schedule[7]])
        symbolic_g(z3, work, 0, 5, 10, 15, message[schedule[8]], message[schedule[9]])
        symbolic_g(z3, work, 1, 6, 11, 12, message[schedule[10]], message[schedule[11]])
        symbolic_g(z3, work, 2, 7, 8, 13, message[schedule[12]], message[schedule[13]])
        symbolic_g(z3, work, 3, 4, 9, 14, message[schedule[14]], message[schedule[15]])

    return [
        state[index] ^ work[index] ^ work[index + ref.STATE_WORDS]
        for index in range(ref.STATE_WORDS)
    ]


def symbolic_message(z3, family: Family, value):
    message = [z3.BitVecVal(word, 64) for word in BASE_WORDS]
    shift = family.byte * 8
    fixed = BASE_WORDS[family.word] & ~(0xFF << shift)
    message[family.word] = z3.BitVecVal(fixed, 64) | (z3.ZeroExt(56, value) << shift)
    return message


def verify_with_z3(result: Result, timeout_ms: int) -> str:
    try:
        import z3
    except ImportError as error:
        raise RuntimeError("z3-solver is required; install it with python -m pip install z3-solver") from error

    value = z3.BitVec("input_byte", 8)
    left = symbolic_compress(z3, symbolic_message(z3, result.family, value), result.rounds)
    paired_value = value ^ z3.BitVecVal(result.family.delta, 8)
    right = symbolic_compress(
        z3,
        symbolic_message(z3, result.family, paired_value),
        result.rounds,
    )
    differences = [left[index] ^ right[index] for index in range(ref.STATE_WORDS)]
    witness_solver = z3.Solver()
    witness_solver.set(timeout=timeout_ms)
    witness_solver.add(value == result.witness)
    if witness_solver.check() != z3.sat:
        raise RuntimeError("Z3 could not replay the minimum-weight witness")
    model = witness_solver.model()
    symbolic_difference = tuple(
        model.eval(difference, model_completion=True).as_long()
        for difference in differences
    )
    concrete_left = output_for(result.witness, result.family, result.rounds)
    concrete_right = output_for(
        result.witness ^ result.family.delta,
        result.family,
        result.rounds,
    )
    concrete_difference = tuple(
        concrete_left[index] ^ concrete_right[index]
        for index in range(ref.STATE_WORDS)
    )
    if symbolic_difference != concrete_difference:
        raise RuntimeError("Z3 model and concrete trail evaluator disagree")
    return "verified"


def parse_rounds(text: str) -> tuple[int, ...]:
    try:
        rounds = tuple(dict.fromkeys(int(value) for value in text.split(",")))
    except ValueError as error:
        raise argparse.ArgumentTypeError("rounds must be comma-separated integers") from error
    if not rounds or any(rounds_value < 1 or rounds_value > ref.ROUNDS for rounds_value in rounds):
        raise argparse.ArgumentTypeError(f"rounds must be between 1 and {ref.ROUNDS}")
    return rounds


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rounds", type=parse_rounds, default=(1, 2, 3, 4))
    parser.add_argument("--timeout-ms", type=int, default=60000)
    parser.add_argument("--enumerate-only", action="store_true")
    args = parser.parse_args()
    if args.timeout_ms <= 0:
        parser.error("timeout must be positive")

    try:
        verify_reference()
        results = [
            analyze_family(family, rounds)
            for rounds in args.rounds
            for family in FAMILIES
        ]
        print(
            "rounds,family,pairs,min_weight,min_active_words,witness,"
            "zero_pairs,max_linear_correlation,input_mask,output_bit,smt_witness"
        )
        failed = False
        for result in results:
            smt_status = "skipped"
            if not args.enumerate_only:
                smt_status = verify_with_z3(result, args.timeout_ms)
            correlation = result.linear_coefficient / 256.0
            print(
                f"{result.rounds},{result.family.name},256,"
                f"{result.minimum_weight},{result.minimum_active_words},"
                f"0x{result.witness:02x},{result.zero_pairs},"
                f"{correlation:.6f},0x{result.linear_mask:02x},"
                f"{result.linear_output_bit},{smt_status}"
            )
            failed = failed or result.zero_pairs != 0
        if failed:
            print("TRAIL_SEARCH: FAIL")
            return 1
        scope = "enumeration" if args.enumerate_only else "enumeration+SMT"
        print(f"TRAIL_SEARCH: PASS ({scope}; bounded 8-bit families)")
        return 0
    except RuntimeError as error:
        print(f"TRAIL_SEARCH: ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
