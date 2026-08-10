#!/usr/bin/env python3
"""Straightforward Python reference implementation of Fractal Crypt-Hash."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


MASK64 = (1 << 64) - 1

STATE_WORDS = 8
OUTPUT_256_WORDS = 4
OUTPUT_512_WORDS = 8
BLOCK_SIZE = 128
ROUNDS = 16
TREE_VERSION = 1
SPLIT_VERSION = 1
MIN_BLOCK_SIZE = 64
MAX_DEPTH = 16
N_MIN = 2
N_MAX = 6
WEIGHT_MIN = 128
WEIGHT_MAX = 255

IV = (
    0x6A09E667F3BCC908,
    0xBB67AE8584CAA73B,
    0x3C6EF372FE94F82B,
    0xA54FF53A5F1D36F1,
    0x510E527FADE682D1,
    0x9B05688C2B3E6C1F,
    0x1F83D9ABFB41BD6B,
    0x5BE0CD19137E2179,
)

SIGMA = (
    (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
    (14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3),
    (11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4),
    (7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8),
    (9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13),
    (2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9),
    (12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11),
    (13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10),
    (6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5),
    (10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0),
    (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
    (14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3),
    (11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4),
    (7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8),
    (9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13),
    (2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9),
)

TAG_LEAF_HEADER = int.from_bytes(b"FCHLEAF1", "little")
TAG_LEAF_DATA = int.from_bytes(b"FCHLDAT1", "little")
TAG_NODE_HEADER = int.from_bytes(b"FCHNODE1", "little")
TAG_NODE_CHILD = int.from_bytes(b"FCHCHLD1", "little")
TAG_OUTPUT = int.from_bytes(b"FCHOUT01", "little")
TAG_SPLIT_HEADER = int.from_bytes(b"FCHSPH01", "little")
TAG_SPLIT_DATA = int.from_bytes(b"FCHSPD01", "little")
TAG_SPLIT_OUTPUT = int.from_bytes(b"FCHSPO01", "little")

DOMAIN_SPLIT = 0x31544C5053484346
DOMAIN_ROOT_LEAF = 0x524F4F544C454146
DOMAIN_INNER_LEAF = 0x494E544C45414631
DOMAIN_ROOT_NODE = 0x524F4F544E4F4445
DOMAIN_INNER_NODE = 0x494E544E4F444531
DOMAIN_OUTPUT_256 = 0x4643484F55543235
DOMAIN_OUTPUT_512 = 0x4643484F55543531

FLAG_LEAF_HEADER = 0x0000000000000001
FLAG_LEAF_DATA = 0x0000000000000002
FLAG_NODE_HEADER = 0x0000000000000004
FLAG_NODE_CHILD = 0x0000000000000008
FLAG_OUTPUT = 0x0000000000000010
FLAG_SPLIT_HEADER = 0x0000000000000020
FLAG_SPLIT_DATA = 0x0000000000000040
FLAG_SPLIT_OUTPUT = 0x0000000000000080
FLAG_FINAL = 0x8000000000000000


def _store64(buffer: bytearray, offset: int, value: int) -> None:
    buffer[offset : offset + 8] = (value & MASK64).to_bytes(8, "little")


def _rotr64(value: int, count: int) -> int:
    count &= 63
    return ((value >> count) | (value << ((64 - count) & 63))) & MASK64


def _g(
    work: list[int],
    a: int,
    b: int,
    c: int,
    d: int,
    x: int,
    y: int,
) -> None:
    work[a] = (work[a] + work[b] + x) & MASK64
    work[d] = _rotr64(work[d] ^ work[a], 32)
    work[c] = (work[c] + work[d]) & MASK64
    work[b] = _rotr64(work[b] ^ work[c], 24)
    work[a] = (work[a] + work[b] + y) & MASK64
    work[d] = _rotr64(work[d] ^ work[a], 16)
    work[c] = (work[c] + work[d]) & MASK64
    work[b] = _rotr64(work[b] ^ work[c], 63)


def _round(work: list[int], message: list[int], round_index: int) -> None:
    schedule = SIGMA[round_index]
    _g(work, 0, 4, 8, 12, message[schedule[0]], message[schedule[1]])
    _g(work, 1, 5, 9, 13, message[schedule[2]], message[schedule[3]])
    _g(work, 2, 6, 10, 14, message[schedule[4]], message[schedule[5]])
    _g(work, 3, 7, 11, 15, message[schedule[6]], message[schedule[7]])
    _g(work, 0, 5, 10, 15, message[schedule[8]], message[schedule[9]])
    _g(work, 1, 6, 11, 12, message[schedule[10]], message[schedule[11]])
    _g(work, 2, 7, 8, 13, message[schedule[12]], message[schedule[13]])
    _g(work, 3, 4, 9, 14, message[schedule[14]], message[schedule[15]])


def _mix_init(domain: int) -> list[int]:
    state = list(IV)
    state[0] ^= domain
    state[1] ^= 0x4643482D41525831
    state[2] ^= STATE_WORDS << 56
    state[7] ^= 0x434F52452D563031
    return state


def _compress(
    state: list[int],
    record: bytes | bytearray,
    record_length: int,
    counter: int,
    domain: int,
    flags: int,
) -> None:
    if len(state) != STATE_WORDS:
        raise ValueError("FCH requires an eight-word state")
    if not 0 <= record_length <= BLOCK_SIZE or len(record) > BLOCK_SIZE:
        raise ValueError("invalid compression record length")

    block = bytes(record).ljust(BLOCK_SIZE, b"\0")
    message = [
        int.from_bytes(block[index : index + 8], "little")
        for index in range(0, BLOCK_SIZE, 8)
    ]
    work = list(state) + list(IV)
    work[12] ^= counter & MASK64
    work[13] ^= record_length
    work[13] ^= STATE_WORDS << 56
    work[14] ^= domain
    work[15] ^= flags

    for round_index in range(ROUNDS):
        _round(work, message, round_index)

    for index in range(STATE_WORDS):
        state[index] = (
            state[index] ^ work[index] ^ work[index + STATE_WORDS]
        ) & MASK64


def _split_material(data: bytes, depth: int) -> list[int]:
    state = _mix_init(DOMAIN_SPLIT)
    header = bytearray(BLOCK_SIZE)
    fields = (
        TAG_SPLIT_HEADER,
        SPLIT_VERSION,
        DOMAIN_SPLIT,
        len(data),
        depth,
        STATE_WORDS,
        N_MIN,
        N_MAX,
        WEIGHT_MIN,
        WEIGHT_MAX,
        MIN_BLOCK_SIZE,
        MAX_DEPTH,
        BLOCK_SIZE,
        ROUNDS,
        TREE_VERSION,
    )
    for index, value in enumerate(fields):
        _store64(header, index * 8, value)
    _compress(state, header, BLOCK_SIZE, 0, DOMAIN_SPLIT, FLAG_SPLIT_HEADER)

    processed = 0
    record_count = 0
    while processed < len(data):
        chunk = data[processed : processed + BLOCK_SIZE - 8]
        record = bytearray(BLOCK_SIZE)
        _store64(record, 0, TAG_SPLIT_DATA)
        record[8 : 8 + len(chunk)] = chunk
        processed += len(chunk)
        record_count += 1
        flags = FLAG_SPLIT_DATA
        if processed == len(data):
            flags |= FLAG_FINAL
        _compress(
            state,
            record,
            len(chunk) + 8,
            processed,
            DOMAIN_SPLIT,
            flags,
        )

    rejection_threshold = (1 << 64) % (N_MAX - N_MIN + 1)
    draw_counter = 0
    while True:
        output_record = bytearray(BLOCK_SIZE)
        fields = (
            TAG_SPLIT_OUTPUT,
            SPLIT_VERSION,
            draw_counter,
            len(data),
            depth,
            record_count,
            N_MIN,
            N_MAX,
            WEIGHT_MIN,
            WEIGHT_MAX,
            STATE_WORDS,
            BLOCK_SIZE,
            ROUNDS,
            TREE_VERSION,
        )
        for index, value in enumerate(fields):
            _store64(output_record, index * 8, value)
        _compress(
            state,
            output_record,
            BLOCK_SIZE,
            draw_counter,
            DOMAIN_SPLIT,
            FLAG_SPLIT_OUTPUT | FLAG_FINAL,
        )
        if state[0] >= rejection_threshold:
            return state
        draw_counter += 1
        if draw_counter > MASK64:
            raise OverflowError("split output counter exhausted")


def _split(data: bytes, depth: int) -> list[tuple[int, int]]:
    if not data:
        return [(0, 0)]

    material = _split_material(data, depth)
    if len(data) < MIN_BLOCK_SIZE * 2:
        count = N_MIN
    else:
        count = N_MIN + material[0] % (N_MAX - N_MIN + 1)
    count = min(count, N_MAX, len(data))

    weights = [WEIGHT_MIN + (material[index + 1] & 0x7F) for index in range(count)]
    total_weight = sum(weights)
    blocks: list[tuple[int, int]] = []
    block_offset = 0

    for index, weight in enumerate(weights):
        remaining = len(data) - block_offset
        blocks_left = count - index - 1
        quotient, remainder = divmod(len(data), total_weight)
        block_length = quotient * weight + (remainder * weight) // total_weight
        block_length = max(block_length, 1)

        if index == count - 1:
            block_length = remaining
        else:
            block_length = min(block_length, remaining - blocks_left)

        blocks.append((block_offset, block_length))
        block_offset += block_length

    if block_offset != len(data):
        raise ValueError("split does not cover its parent")
    return blocks


def _leaf(data: bytes, depth: int) -> list[int]:
    domain = DOMAIN_ROOT_LEAF if depth == 0 else DOMAIN_INNER_LEAF
    state = _mix_init(domain)
    header = bytearray(BLOCK_SIZE)
    fields = (
        TAG_LEAF_HEADER,
        TREE_VERSION,
        domain,
        len(data),
        depth,
        STATE_WORDS,
        MIN_BLOCK_SIZE,
        MAX_DEPTH,
        N_MIN,
        N_MAX,
        BLOCK_SIZE,
        ROUNDS,
        WEIGHT_MIN,
        WEIGHT_MAX,
        SPLIT_VERSION,
    )
    for index, value in enumerate(fields):
        _store64(header, index * 8, value)
    _compress(state, header, BLOCK_SIZE, 0, domain, FLAG_LEAF_HEADER)

    if not data:
        record = bytearray(BLOCK_SIZE)
        _store64(record, 0, TAG_LEAF_DATA)
        _compress(state, record, 8, 0, domain, FLAG_LEAF_DATA | FLAG_FINAL)
        return state

    processed = 0
    while processed < len(data):
        chunk = data[processed : processed + BLOCK_SIZE - 8]
        record = bytearray(BLOCK_SIZE)
        _store64(record, 0, TAG_LEAF_DATA)
        record[8 : 8 + len(chunk)] = chunk
        flags = FLAG_LEAF_DATA
        if len(chunk) == len(data) - processed:
            flags |= FLAG_FINAL
        processed += len(chunk)
        _compress(state, record, len(chunk) + 8, processed, domain, flags)
    return state


def _combine(
    children: list[list[int]],
    blocks: list[tuple[int, int]],
    node_length: int,
    depth: int,
) -> list[int]:
    domain = DOMAIN_ROOT_NODE if depth == 0 else DOMAIN_INNER_NODE
    state = _mix_init(domain)
    header = bytearray(BLOCK_SIZE)
    fields = (
        TAG_NODE_HEADER,
        TREE_VERSION,
        domain,
        node_length,
        depth,
        len(children),
        STATE_WORDS,
        MIN_BLOCK_SIZE,
        MAX_DEPTH,
        N_MIN,
        N_MAX,
        BLOCK_SIZE,
        ROUNDS,
        WEIGHT_MIN,
        WEIGHT_MAX,
        SPLIT_VERSION,
    )
    for index, value in enumerate(fields):
        _store64(header, index * 8, value)
    _compress(state, header, BLOCK_SIZE, 0, domain, FLAG_NODE_HEADER)

    covered = 0
    for child_index, (child, block) in enumerate(zip(children, blocks)):
        offset, length = block
        if offset != covered or length <= 0 or len(child) != STATE_WORDS:
            raise ValueError("non-canonical child layout")

        record = bytearray(BLOCK_SIZE)
        fields = (
            TAG_NODE_CHILD,
            TREE_VERSION,
            node_length,
            depth,
            len(children),
            child_index,
            offset,
            length,
        )
        for index, value in enumerate(fields):
            _store64(record, index * 8, value)
        for index, word in enumerate(child):
            _store64(record, 64 + index * 8, word)

        flags = FLAG_NODE_CHILD
        if child_index + 1 == len(children):
            flags |= FLAG_FINAL
        _compress(state, record, BLOCK_SIZE, child_index + 1, domain, flags)
        covered += length

    if covered != node_length:
        raise ValueError("children do not cover their parent")
    return state


def _process(data: bytes, depth: int = 0) -> list[int]:
    if depth >= MAX_DEPTH or len(data) <= MIN_BLOCK_SIZE:
        return _leaf(data, depth)

    blocks = _split(data, depth)
    children = [
        _process(data[offset : offset + length], depth + 1)
        for offset, length in blocks
    ]
    return _combine(children, blocks, len(data), depth)


def _pad(message: bytes) -> bytes:
    if len(message) > (1 << 61) - 1:
        raise ValueError("message is too long for the FCH length field")
    padded_length = max(len(message) + 9, MIN_BLOCK_SIZE)
    padding = padded_length - len(message) - 9
    return message + b"\x80" + bytes(padding) + (len(message) * 8).to_bytes(8, "little")


def digest(message: bytes, output_bits: int = 256) -> bytes:
    """Return an FCH-256 or FCH-512 digest."""
    if output_bits not in (256, 512):
        raise ValueError("output_bits must be 256 or 512")

    output_words = OUTPUT_256_WORDS if output_bits == 256 else OUTPUT_512_WORDS
    domain = DOMAIN_OUTPUT_256 if output_bits == 256 else DOMAIN_OUTPUT_512
    state = _process(_pad(message))
    record = bytearray(BLOCK_SIZE)
    fields = (
        TAG_OUTPUT,
        TREE_VERSION,
        output_words * 64,
        STATE_WORDS * 64,
        BLOCK_SIZE,
        ROUNDS,
    )
    for index, value in enumerate(fields):
        _store64(record, index * 8, value)
    _compress(
        state,
        record,
        BLOCK_SIZE,
        output_words * 8,
        domain,
        FLAG_OUTPUT | FLAG_FINAL,
    )
    return b"".join(word.to_bytes(8, "little") for word in state[:output_words])


def _comparison_messages() -> list[bytes]:
    messages = [
        b"",
        b"abc",
        b"The quick brown fox jumps over the lazy dog",
    ]
    lengths = (
        1,
        7,
        8,
        31,
        54,
        55,
        56,
        63,
        64,
        65,
        119,
        120,
        121,
        127,
        128,
        129,
        191,
        255,
        256,
        257,
        511,
        512,
        513,
        1024,
        2048,
        4096,
    )
    for length in lengths:
        messages.append(
            bytes((index * 131 + length * 17 + (index >> 3)) & 0xFF for index in range(length))
        )
    messages.extend((bytes(513), b"\xff" * 513, bytes(range(256)) * 4))
    return messages


def _c_digest(executable: Path, message: bytes, output_bits: int) -> str:
    completed = subprocess.run(
        [str(executable), f"-{output_bits}"],
        input=message,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != 0:
        error = completed.stderr.decode("utf-8", "replace").strip()
        raise RuntimeError(f"C CLI failed with status {completed.returncode}: {error}")
    fields = completed.stdout.decode("ascii").split()
    if not fields:
        raise RuntimeError("C CLI returned no digest")
    return fields[0]


def check_c_implementation(executable: Path) -> int:
    """Compare the Python model with the compiled C command-line tool."""
    executable = executable.resolve()
    if not executable.is_file():
        print(f"reference check: C executable not found: {executable}", file=sys.stderr)
        return 2

    messages = _comparison_messages()
    comparisons = 0
    for case_index, message in enumerate(messages):
        for output_bits in (256, 512):
            expected = digest(message, output_bits).hex()
            actual = _c_digest(executable, message, output_bits)
            comparisons += 1
            if actual != expected:
                print(
                    f"reference mismatch: case={case_index} length={len(message)} "
                    f"variant=FCH-{output_bits}",
                    file=sys.stderr,
                )
                print(f"  python: {expected}", file=sys.stderr)
                print(f"  c     : {actual}", file=sys.stderr)
                return 1

    print(f"PASS: Python reference matches C ({comparisons} comparisons)")
    return 0


def _hash_files(paths: list[str], output_bits: int) -> int:
    if not paths:
        paths = ["-"]

    exit_code = 0
    for name in paths:
        try:
            data = sys.stdin.buffer.read() if name == "-" else Path(name).read_bytes()
            print(f"{digest(data, output_bits).hex()}  {name}")
        except (OSError, ValueError) as error:
            print(f"fch_reference: {name}: {error}", file=sys.stderr)
            exit_code = 2
    return exit_code


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Python reference implementation of FCH")
    variants = parser.add_mutually_exclusive_group()
    variants.add_argument("-256", "--256", dest="output_bits", action="store_const", const=256)
    variants.add_argument("-512", "--512", dest="output_bits", action="store_const", const=512)
    parser.set_defaults(output_bits=256)
    parser.add_argument(
        "--check-c",
        metavar="EXECUTABLE",
        type=Path,
        help="compare the reference implementation with a compiled FCH CLI",
    )
    parser.add_argument("files", nargs="*", metavar="FILE")
    args = parser.parse_args(argv)

    if args.check_c is not None:
        if args.files:
            parser.error("FILE arguments cannot be combined with --check-c")
        return check_c_implementation(args.check_c)
    return _hash_files(args.files, args.output_bits)


if __name__ == "__main__":
    raise SystemExit(main())
