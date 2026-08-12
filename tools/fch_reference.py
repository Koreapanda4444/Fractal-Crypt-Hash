from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


MASK64 = (1 << 64) - 1

STATE_WORDS = 8
OUTPUT_256_WORDS = 4
OUTPUT_512_WORDS = 8
BLOCK_SIZE = 128
ROUNDS = 16
TREE_VERSION = 2
PADDING_VERSION = 1
PADDING_MIN_BYTES = 64
TREE_LEAF_BYTES = 1024
TREE_ARITY = 2

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

TAG_LEAF_HEADER = int.from_bytes(b"FCHLEAF2", "little")
TAG_LEAF_DATA = int.from_bytes(b"FCHLDAT2", "little")
TAG_NODE_HEADER = int.from_bytes(b"FCHNODE2", "little")
TAG_NODE_CHILD = int.from_bytes(b"FCHCHLD2", "little")
TAG_OUTPUT = int.from_bytes(b"FCHOUT02", "little")

DOMAIN_LEAF = int.from_bytes(b"FCHLDM02", "little")
DOMAIN_NODE = int.from_bytes(b"FCHNDM02", "little")
DOMAIN_OUTPUT_256 = int.from_bytes(b"FCHO2562", "little")
DOMAIN_OUTPUT_512 = int.from_bytes(b"FCHO5122", "little")

FLAG_LEAF_HEADER = 0x0000000000000001
FLAG_LEAF_DATA = 0x0000000000000002
FLAG_NODE_HEADER = 0x0000000000000004
FLAG_NODE_CHILD = 0x0000000000000008
FLAG_OUTPUT = 0x0000000000000010
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


@dataclass
class TreeNode:
    state: list[int]
    level: int
    first_leaf: int
    leaf_count: int
    byte_offset: int
    byte_length: int


def _tree_level(leaf_count: int) -> int:
    if leaf_count <= 0:
        raise ValueError("tree must contain at least one leaf")
    return (leaf_count - 1).bit_length()


def _largest_power_of_two_below(value: int) -> int:
    if value < 2:
        raise ValueError("an internal node requires at least two leaves")
    return 1 << ((value - 1).bit_length() - 1)


def _leaf(data: bytes, byte_offset: int) -> TreeNode:
    if not 1 <= len(data) <= TREE_LEAF_BYTES:
        raise ValueError("invalid leaf length")
    if byte_offset < 0 or byte_offset % TREE_LEAF_BYTES:
        raise ValueError("leaf offset is not aligned")

    first_leaf = byte_offset // TREE_LEAF_BYTES
    state = _mix_init(DOMAIN_LEAF)
    header = bytearray(BLOCK_SIZE)
    fields = (
        TAG_LEAF_HEADER,
        TREE_VERSION,
        DOMAIN_LEAF,
        first_leaf,
        byte_offset,
        len(data),
        TREE_LEAF_BYTES,
        STATE_WORDS,
        BLOCK_SIZE,
        ROUNDS,
        PADDING_VERSION,
        TREE_ARITY,
    )
    for index, value in enumerate(fields):
        _store64(header, index * 8, value)
    _compress(state, header, BLOCK_SIZE, 0, DOMAIN_LEAF, FLAG_LEAF_HEADER)

    processed = 0
    while processed < len(data):
        chunk = data[processed : processed + BLOCK_SIZE - 8]
        record = bytearray(BLOCK_SIZE)
        _store64(record, 0, TAG_LEAF_DATA)
        record[8 : 8 + len(chunk)] = chunk
        processed += len(chunk)
        flags = FLAG_LEAF_DATA
        if processed == len(data):
            flags |= FLAG_FINAL
        _compress(
            state,
            record,
            len(chunk) + 8,
            processed,
            DOMAIN_LEAF,
            flags,
        )

    return TreeNode(state, 0, first_leaf, 1, byte_offset, len(data))


def _combine(left: TreeNode, right: TreeNode) -> TreeNode:
    if left.first_leaf + left.leaf_count != right.first_leaf:
        raise ValueError("child leaf ranges are not adjacent")
    if left.byte_offset + left.byte_length != right.byte_offset:
        raise ValueError("child byte ranges are not adjacent")

    leaf_count = left.leaf_count + right.leaf_count
    expected_left = _largest_power_of_two_below(leaf_count)
    if left.leaf_count != expected_left:
        raise ValueError("children do not follow the canonical tree schedule")

    node = TreeNode(
        [],
        _tree_level(leaf_count),
        left.first_leaf,
        leaf_count,
        left.byte_offset,
        left.byte_length + right.byte_length,
    )
    state = _mix_init(DOMAIN_NODE)
    header = bytearray(BLOCK_SIZE)
    fields = (
        TAG_NODE_HEADER,
        TREE_VERSION,
        DOMAIN_NODE,
        node.level,
        node.first_leaf,
        node.leaf_count,
        node.byte_offset,
        node.byte_length,
        TREE_ARITY,
        TREE_LEAF_BYTES,
        STATE_WORDS,
        BLOCK_SIZE,
        ROUNDS,
    )
    for index, value in enumerate(fields):
        _store64(header, index * 8, value)
    _compress(state, header, BLOCK_SIZE, 0, DOMAIN_NODE, FLAG_NODE_HEADER)

    for child_index, child in enumerate((left, right)):
        record = bytearray(BLOCK_SIZE)
        fields = (
            TAG_NODE_CHILD,
            TREE_VERSION,
            child_index,
            child.level,
            child.first_leaf,
            child.leaf_count,
            child.byte_offset,
            child.byte_length,
        )
        for index, value in enumerate(fields):
            _store64(record, index * 8, value)
        for index, word in enumerate(child.state):
            _store64(record, 64 + index * 8, word)

        flags = FLAG_NODE_CHILD
        if child_index + 1 == TREE_ARITY:
            flags |= FLAG_FINAL
        _compress(
            state,
            record,
            BLOCK_SIZE,
            child_index + 1,
            DOMAIN_NODE,
            flags,
        )

    node.state = state
    return node


def _process(data: bytes, byte_offset: int = 0) -> TreeNode:
    if not data:
        raise ValueError("the padded tree input cannot be empty")
    if byte_offset < 0 or byte_offset % TREE_LEAF_BYTES:
        raise ValueError("tree offset is not aligned")

    leaf_count = (len(data) + TREE_LEAF_BYTES - 1) // TREE_LEAF_BYTES
    if leaf_count == 1:
        return _leaf(data, byte_offset)

    left_leaves = _largest_power_of_two_below(leaf_count)
    left_length = left_leaves * TREE_LEAF_BYTES
    left = _process(data[:left_length], byte_offset)
    right = _process(data[left_length:], byte_offset + left_length)
    return _combine(left, right)


def _pad(message: bytes) -> bytes:
    if len(message) > (1 << 61) - 1:
        raise ValueError("message is too long for the FCH length field")
    padded_length = max(len(message) + 9, PADDING_MIN_BYTES)
    padding = padded_length - len(message) - 9
    return message + b"\x80" + bytes(padding) + (len(message) * 8).to_bytes(8, "little")


def digest(message: bytes, output_bits: int = 256) -> bytes:
    if output_bits not in (256, 512):
        raise ValueError("output_bits must be 256 or 512")

    output_words = OUTPUT_256_WORDS if output_bits == 256 else OUTPUT_512_WORDS
    domain = DOMAIN_OUTPUT_256 if output_bits == 256 else DOMAIN_OUTPUT_512
    padded = _pad(message)
    root = _process(padded)
    record = bytearray(BLOCK_SIZE)
    fields = (
        TAG_OUTPUT,
        TREE_VERSION,
        output_words * 64,
        STATE_WORDS * 64,
        BLOCK_SIZE,
        ROUNDS,
        len(message),
        len(padded),
        root.leaf_count,
        root.level,
        root.first_leaf,
        root.leaf_count,
        root.byte_offset,
        root.byte_length,
        TREE_LEAF_BYTES,
        PADDING_VERSION,
    )
    for index, value in enumerate(fields):
        _store64(record, index * 8, value)
    _compress(
        root.state,
        record,
        BLOCK_SIZE,
        output_words * 8,
        domain,
        FLAG_OUTPUT | FLAG_FINAL,
    )
    return b"".join(
        word.to_bytes(8, "little")
        for word in root.state[:output_words]
    )


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
        1014,
        1015,
        1016,
        1023,
        1024,
        1025,
        2038,
        2039,
        2040,
        2047,
        2048,
        2049,
        3062,
        3063,
        3064,
        4086,
        4087,
        4088,
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
