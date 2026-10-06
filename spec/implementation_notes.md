# Implementation Notes

This file records choices made by the C reference implementation. The
normative algorithm is defined in [fch_spec.md](fch_spec.md).
Completed local/CI passes, configured campaigns, and unverified coverage are tracked in
[validation_status.md](validation_status.md).

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

`include/fch.h` and `include/fch_stream.h` are the public API. Implementation
headers, including research hooks, live beside their sources in `src/` and
are not a supported external interface.

`make lib` archives the seven core objects in `build/libfch.a`; `make all`
also builds the CLI. The archive is replaced from the current object list so
removed implementation objects cannot survive a rebuild. Compiler settings
and archive command/flags have separate change tracking. `make install`
supports `PREFIX`, `LIBDIR`, `INCLUDEDIR`, and `DESTDIR` and installs only the
archive and two public headers.

- `src/mix.c` implements initialization, the 16-round ARX compression core,
  and output finalization.
- `src/fractal_split.c` computes and validates canonical range descriptors and
  binary child boundaries.
- `src/leaf.c` encodes leaf headers and data records.
- `src/combine.c` validates two child descriptors and encodes a parent.
- `src/fractal_process.c` reduces the canonical tree in left-to-right order.
- `src/fch.c` applies one-shot padding and serializes the digest.
- `src/fch_stream.c` implements incremental leaf and subtree processing.

The one-shot and streaming paths share leaf push, binary-carry merges, and
root folding in `src/fractal_process.c`, using one internal workspace type
and a common descriptor comparison.

`fch_state_t` carries both state words and `fch_tree_position_t`. Keeping the
descriptor beside the state makes it harder for callers to combine a state at
the wrong position accidentally.

## Canonical scheduling

The historical internal names `fch_fractal_split*` remain, but their signatures
are not a supported compatibility interface. They do not inspect input bytes
or derive a variable fan-out. For a
single-leaf range they return that range. For an internal range they return the
two canonical children. The reader callback is validated but is not called
while the schedule is calculated.

The unused legacy `depth` arguments have been removed from internal calls.
Position descriptors and debug hooks retain the actual version-2 tree level:
leaves are level zero and the root has `ceil(log2(leaf_count))`.

`fch_combine` accepts exactly two children. It recomputes the expected parent
and split, checks the child descriptors, verifies relative block offsets and
lengths, and refuses reordered, forged, flat, skewed, gapped, or overlapping
layouts.

## One-shot path

The one-shot API exposes the original input, padding marker, zero fill and
eight-byte length field through a bounded reader and processes leaves from
left to right. It allocates only the 64-byte root state. A binary-carry
workspace retains at most one completed subtree per level; internal-node
construction uses only child states and descriptors.

No complete padded-message buffer is allocated. The reader borrows the input
until hashing finishes; digest serialization follows all input reads, preserving
overlapping input/output behavior. The tree workspace has a fixed number of
slots derived from the width of `size_t`. Auxiliary storage is bounded for a
given platform; tree work is linear in the padded input plus the number of nodes.

## Streaming path

Update calls pass complete 1,024-byte leaves directly to the reader when no
partial leaf is pending. Only partial leaves are copied into the pending buffer;
the caller's input is consumed before `update` returns and is not retained.
Each complete leaf is compressed immediately and merged through a binary-carry
subtree workspace. Finalization
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

CI covers Linux, macOS, and Windows, a native 32-bit x86 build, and a
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
one-shot root allocation, verifies that no second one-shot allocation occurs,
rejects stream-context allocation, verifies that
stream finalization performs no new allocation, and checks output clearing and
context cleanup after every failure.

## Tests and benchmark

All compiler objects and dependencies are written under `build/obj/`; binary,
binary-dependency, benchmark, and fuzz outputs remain under `build/`.
Make disables Python bytecode generation. Checked-in research datasets are
reviewed inputs to verification and are not generated build artifacts.

`make check` runs seven existing C correctness programs and CLI regression.
The canonical KAT reader is part of `test_vectors.c`, not another test file.
`check-library` links that same source against the archive with only public
include paths; `check-install` repeats it using the staged installation. Both
run the complete corpus, including streaming plans and overlapping outputs.
`check-reference` retains 384 differential cases; `check-interoperability`
checks all 54 fixed corpus inputs through C one-shot, eleven streaming plans,
two overlapping output offsets per variant, C CLI, and Python.
`check-extended` runs `test_stress.c`.

Native cryptanalysis and tree attack sources live in `analysis/`. The
consolidated `fch_diffusion.c` implements avalanche, length, pattern, and
tree-level experiments without including another `.c` file. These groups,
trails, characteristics, and conditional accounting run through
`check-research` rather than `check`.

The fixed all-round characteristic report uses 128 bases and 1,024 single-bit
differences for rounds 1 through 16. Working-state/output weights and active
word minima are aggregated independently, with separate state/output
witnesses. `check-full-rounds` requires exact agreement with
`analysis/full-round-characteristics-v1.json`. A regression in the existing
Python analysis test distinguishes the state-minimum witness from the
output-minimum witness.

`tools/fch_reduced_round_analysis.py` evaluates the same compression context
after every round from 1 through 16. Its fixed profile combines internal-state
and output diffusion, chosen XOR differences, word rotations, and seven
structured input families. The deterministic 160-row JSON report is checked
into `analysis/`; CI validates its schema, reruns the profile, and requires an
exact result match. Thresholds are regression alarms from round 2 onward, not
cryptographic bounds.

`tools/fch_second_preimage_bounds.py` reproduces the FCH-512 conditional
second-preimage accounting at padding, leaf, large-message, and maximum-length
boundaries. It checks the canonical descriptor multiplicity, reports complete
message, typed-map, and serial compression-proxy units separately, and stores
the deterministic profile in `analysis/fch512-second-preimage-v1.json`. CI
requires an exact match. These are ideal-map union bounds and format checks,
not results about the real compression function.

`bench/bench_hash.c` measures processor time and throughput across inputs from 64
bytes through 8 MiB. It covers FCH-256 and FCH-512 one-shot hashing, FCH-256
streaming with 1-byte through 64 KiB updates, and FCH-512 streaming with 1 KiB
and 64 KiB updates.

The benchmark replaces the implementation allocator only for this executable.
Its input buffer and allocator metadata are excluded from the reported heap
total. Each result records peak requested heap and allocation count per hash.
The run fails unless one-shot hashing uses one 64-byte root allocation, if a
streaming hash performs more than its context allocation, if streaming peak
memory changes with input or chunk size, or if any allocation remains live.
`--quick` uses a smaller matrix for CI while preserving the scaling checks.

`--baseline` defines the versioned `baseline-v1` measurement profile. It uses
the complete matrix and fixed input seed, gives every case enough iterations
for a sustained sample, then interleaves one warmup and five measured passes.
It emits the median processor time with the deterministic resource counts.
`tools/fch_benchmark.py` stores those rows with source and environment metadata
and compares later runs only when their CPU, platform, compiler, and flags
match. Throughput limits are configurable; heap and allocation changes require
explicit review. `ALLOW_RESOURCE_IMPROVEMENT=1` permits reviewed decreases while
still rejecting any increase; the default rejects all resource-profile changes.

The four existing `tests/fuzz_*.c` targets are driven by
`tools/fch_fuzz.py`. It maintains 456 named seed files, fixed seeds, bounded
smoke runs, and duration-limited campaigns; metadata and logs remain under
`build/fuzz/`. CI runs smoke and ASan/UBSan/leaks on normal pushes and offers
600 seconds per target for scheduled/manual campaigns. The local ptrace
environment required leak detection to be disabled; completed CI leak passes
are recorded separately in the validation status.

## Compatibility policy

Digest compatibility takes priority over optimization. Allocation strategy,
buffering, seeking, recursion, and scheduling may change only when the fixed
vectors and one-shot/streaming results remain identical. Tags, domains, flags,
record layouts, padding, leaf span, tree schedule, state width, and round count
are algorithm parameters and require an explicit format revision when changed.
