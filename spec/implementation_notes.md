# Implementation Notes

This file records choices made by the C reference implementation. The
normative algorithm is defined in [fch_spec.md](fch_spec.md).

## Current format

The C implementation and `tools/fch_reference.py` both implement tree encoding
version 2. Fixed 1,024-byte leaves and a left-complete binary schedule have
replaced content-derived fan-out and weighted boundaries. Version-1 digests are
not emitted as a compatibility fallback.

Both output variants use the same eight-word tree state. Leaves, nodes, and the
two output sizes have separate domains. A node state includes the complete
512-bit state of each child plus the child's level, leaf range, and absolute
byte range. Root status is added only by output finalization.

## Source layout

- `src/mix.c` implements initialization, the 16-round ARX compression core,
  and output finalization.
- `src/fractal_split.c` computes and validates canonical range descriptors and
  binary child boundaries.
- `src/leaf.c` encodes leaf headers and data records.
- `src/combine.c` validates two child descriptors and encodes a parent.
- `src/fractal_process.c` reduces the canonical tree in left-to-right order.
- `src/fch.c` applies one-shot padding and serializes the digest.
- `src/fch_stream.c` implements incremental leaf and subtree processing.

`fch_state_t` carries both state words and `fch_tree_position_t`. Keeping the
descriptor beside the state makes it harder for callers to combine a state at
the wrong position accidentally.

## Canonical scheduling

The functions named `fch_fractal_split*` remain for source compatibility, but
they no longer inspect input bytes or derive a variable fan-out. For a
single-leaf range they return that range. For an internal range they return the
two canonical children. The reader callback is validated but is not called
while the schedule is calculated.

The legacy `depth` arguments are also retained at the internal API boundary.
Negative values are rejected; nonnegative values do not change the encoding or
tree shape. Debug-hook parameters still use the old name in the C signature,
but their value is the version-2 tree level: leaves are level zero and the root
has `ceil(log2(leaf_count))`.

`fch_combine` accepts exactly two children. It recomputes the expected parent
and split, checks the child descriptors, verifies relative block offsets and
lengths, and refuses reordered, forged, flat, skewed, gapped, or overlapping
layouts.

## One-shot path

The one-shot API allocates the complete padded message, exposes it through a
memory reader, and processes leaves from left to right. A binary-carry
workspace retains at most one completed subtree per level; internal-node
construction uses only child states and descriptors.

The padded buffer uses `O(L)` memory. The tree workspace has a fixed number of
slots derived from the width of `size_t`. Tree work is linear in the padded
input plus the number of nodes.

## Streaming path

Update calls fill one 1,024-byte buffer. Each complete leaf is compressed
immediately and merged through a binary-carry subtree workspace. Finalization
constructs at most 1,033 bytes of pending data and padding, processes the last
one or two leaves, and folds the saved subtrees into the canonical root.

The stream retains no complete-message copy, performs no input replay, and
requires no temporary file. Its storage is bounded by one partial leaf, the
final padding buffer, and a fixed set of subtree states. The digest remains
identical for every update partition because leaf offsets, tree descriptors,
and final padding are unchanged.

## Portability

The implementation avoids native byte-order assumptions:

- state words use `uint64_t`;
- modular addition follows unsigned C arithmetic;
- rotation counts are reduced modulo 64;
- padding, metadata, and digests use explicit little-endian operations;
- structural values use fixed 64-bit record fields; and
- range calculations check addition and multiplication before use.

CI covers x86-64 Linux, macOS, and Windows, a native 32-bit x86 build, and a
big-endian PowerPC build executed through QEMU. Fixed vectors and explicit
little-endian serialization checks run on the emulated big-endian target.

## Error handling

Checked one-shot and streaming APIs return an explicit success value. Invalid
pointers, unsupported lengths, allocation failures, reader errors, and
non-canonical tree layouts propagate to the caller. Public output buffers are
cleared on failure; no partial digest or alternate tree is returned.

A failed streaming context remains failed until freed. Repeated finalization
clears the destination and fails. Active contexts have single-owner semantics
and must not be copied or accessed concurrently.

The failure-path test replaces `malloc` and `calloc` at build time, rejects each
one-shot allocation in turn, rejects stream-context allocation, verifies that
stream finalization performs no new allocation, and checks output clearing and
context cleanup after every failure.

## Tests and benchmark

The regular and extended suites check fixed vectors, C/Python agreement,
record bytes, canonical boundaries, content independence, prefix stability,
tree-layout rejection, streaming boundary equivalence, forced allocation
failures, reduced-round
diffusion, bounded cryptanalytic searches, long messages, lifecycle failures,
fuzz paths, and sanitizer builds.

`bench/bench_hash.c` measures processor time and throughput across inputs from 64
bytes through 8 MiB. It covers FCH-256 and FCH-512 one-shot hashing, FCH-256
streaming with 1-byte through 64 KiB updates, and FCH-512 streaming with 1 KiB
and 64 KiB updates.

The benchmark replaces the implementation allocator only for this executable.
Its input buffer and allocator metadata are excluded from the reported heap
total. Each result records peak requested heap and allocation count per hash.
The run fails if one-shot memory does not follow the padded input size, if a
streaming hash performs more than its context allocation, if streaming peak
memory changes with input or chunk size, or if any allocation remains live.
`--quick` uses a smaller matrix for CI while preserving the scaling checks.

## Compatibility policy

Digest compatibility takes priority over optimization. Allocation strategy,
buffering, seeking, recursion, and scheduling may change only when the fixed
vectors and one-shot/streaming results remain identical. Tags, domains, flags,
record layouts, padding, leaf span, tree schedule, state width, and round count
are algorithm parameters and require an explicit format revision when changed.
