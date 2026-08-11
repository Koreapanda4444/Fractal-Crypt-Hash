# Implementation Notes

This file records decisions specific to the C reference implementation. The
algorithm itself is defined in [fch_spec.md](fch_spec.md).

## Structure

FCH builds diffusion in two layers. The 16-round ARX core mixes data inside
each compression call, and the recursive tree carries those changes across
leaves and parent nodes. The tree is an additional structure around the core;
it does not replace local mixing.

Both output variants use the same eight-word internal state. Leaves, internal
nodes, split derivation, and output finalization use separate domains and
record types. Child records include their order, offset, length, parent
metadata, and complete 512-bit state.

## Portability

The implementation avoids native byte-order assumptions:

- state words use `uint64_t`;
- modular addition follows unsigned C arithmetic;
- rotation counts are reduced modulo 64;
- padding, metadata, and digests use explicit little-endian loads and stores;
- structural values are encoded in fixed 64-bit fields; and
- split calculations check for overflow before multiplying or adding sizes.

Current CI covers x86-64 Linux, macOS, and Windows. A future portability pass
will add direct 32-bit and big-endian execution rather than relying only on
serialization tests.

## Error handling

The checked one-shot and streaming APIs return an explicit success value.
Invalid pointers, unsupported lengths, allocation failures, reader errors, and
non-canonical tree layouts propagate to the caller. A failed hash does not fall
back to a different tree or return a partial state.

On failure, public output buffers are cleared. A failed streaming context
remains failed until it is freed, and repeated finalization fails after clearing
the destination. Streaming contexts have single-owner semantics and must not be
copied or used concurrently while active.

## Streaming path

Update calls append bytes to an anonymous temporary file. Finalization exposes
that file through the same random-access reader used by tree processing and
synthesizes padding without placing the complete padded message in RAM.

This keeps application memory bounded, but it is not a one-pass online tree
hash. Split derivation reads the complete contents of each visited node, so a
message can be read again at several tree levels. Temporary storage grows with
the message, and disk I/O is part of the streaming cost.

## Split implementation

Split material is derived from the complete node input in a dedicated domain.
The fan-out draw uses rejection sampling, and the remaining state words provide
weights from 128 through 255. After proportional allocation, the implementation
corrects child lengths so they are positive, contiguous, non-overlapping, and
cover the parent exactly.

This replaced the earlier 64-bit accumulator, whose local influence was easier
to steer. The current split remains public and deterministic; its purpose is
full-input structural binding, not secrecy.

## Frozen next tree format

The next incompatible tree format is defined in section 12 of the
specification. It uses fixed 1,024-byte leaves and a left-complete binary tree.
Leaf and node descriptors bind their absolute byte range, leaf range, and
level. Message bytes do not select fan-out, weights, or boundaries.

This document-only decision does not alter the current implementation. Version
1 remains active until the following pieces move to version 2 together:

- the C one-shot and streaming paths;
- the Python reference;
- record tags, domains, and structural validation;
- expected digests and tree-encoding tests; and
- the normative parameter and processing sections of the specification.

No partial compatibility mode will be added. A version-2 implementation must
not emit a version-1 digest after encountering an unsupported layout or an
allocation failure.

The implementation will keep a stack of completed power-of-two subtrees.
Ordinary updates can finalize full leaves without knowing the eventual message
length. Finalization handles the partial leaf and padding, folds the remaining
stack from right to left, then commits the complete range in `FCHOUT02`. This
removes the need to reread earlier leaves and is the basis for replacing the
temporary-file streaming path later.

## Analysis hooks and tests

Reduced-round compression is compiled only for analysis targets. Debug hooks
use an explicit external-hook switch instead of compiler-specific weak symbols.
Normal builds always use all 16 rounds.

The test suite covers deterministic outputs, boundaries, tree invariants,
domain separation, split sensitivity, reduced-round diffusion, bounded
cryptanalytic searches, tree-attack patterns, API misuse, reader failures, and
one-shot/streaming equivalence. Sanitizer and libFuzzer smoke jobs exercise the
same implementation paths used by the regular tests.

## Optimization policy

Digest compatibility takes priority over optimization. Changes to allocation,
buffering, seeking, or scheduling must preserve fixed outputs and one-shot/
streaming equivalence. Changes to domains, tags, field layouts, round count,
fan-out, weights, or depth limits are algorithm changes and must be treated as
such.
