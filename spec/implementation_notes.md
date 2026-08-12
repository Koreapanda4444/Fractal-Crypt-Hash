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
- `src/fractal_process.c` walks the canonical tree from left to right.
- `src/fch.c` applies one-shot padding and serializes the digest.
- `src/fch_stream.c` implements temporary-file-backed update/final handling.

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
memory reader, and recursively processes the canonical descriptors. Message
bytes are read once as leaf data; internal-node construction uses only child
states and descriptors.

The padded buffer uses `O(L)` memory. The recursive walk retains at most one
completed left subtree per active level, so tree-state overhead is
`O(log leaf_count)`. Tree work is linear in the padded input plus the number of
nodes.

## Streaming path

Update calls append input to an anonymous file created with `tmpfile()`.
Finalization presents stored bytes and virtual padding through the same reader
interface used by the one-shot tree walk. Leaves are requested in increasing
offset order, so version 2 no longer rereads complete ancestor ranges to choose
splits.

This keeps application RAM bounded but still uses `O(L)` temporary storage and
requires temporary-file support. It is not yet a fully online tree hash:
updates do not produce persistent leaf states, and all tree compression happens
during finalization. The canonical schedule makes a future power-of-two
subtree stack possible without changing the digest, but that optimization is
not part of the current code.

## Portability

The implementation avoids native byte-order assumptions:

- state words use `uint64_t`;
- modular addition follows unsigned C arithmetic;
- rotation counts are reduced modulo 64;
- padding, metadata, and digests use explicit little-endian operations;
- structural values use fixed 64-bit record fields; and
- range calculations check addition and multiplication before use.

Current CI builds x86-64 Linux, macOS, and Windows configurations. The test
suite verifies serialization explicitly. Direct 32-bit and big-endian runtime
coverage remains a separate portability task.

## Error handling

Checked one-shot and streaming APIs return an explicit success value. Invalid
pointers, unsupported lengths, allocation failures, reader errors, and
non-canonical tree layouts propagate to the caller. Public output buffers are
cleared on failure; no partial digest or alternate tree is returned.

A failed streaming context remains failed until freed. Repeated finalization
clears the destination and fails. Active contexts have single-owner semantics
and must not be copied or accessed concurrently.

## Tests and benchmark

The regular and extended suites check fixed vectors, C/Python agreement,
record bytes, canonical boundaries, content independence, prefix stability,
tree-layout rejection, one-shot/streaming equivalence, reduced-round
diffusion, bounded cryptanalytic searches, long messages, lifecycle failures,
fuzz paths, and sanitizer builds.

`bench/bench_hash.c` measures FCH-256 and FCH-512 one-shot throughput and
FCH-256 streaming throughput with 64 KiB updates. It benchmarks the current
format directly; it no longer recompiles obsolete depth-cap variants.

## Compatibility policy

Digest compatibility takes priority over optimization. Allocation strategy,
buffering, seeking, recursion, and scheduling may change only when the fixed
vectors and one-shot/streaming results remain identical. Tags, domains, flags,
record layouts, padding, leaf span, tree schedule, state width, and round count
are algorithm parameters and require an explicit format revision when changed.
