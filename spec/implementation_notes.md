# Implementation Notes

## Portability and error handling

- Rotation counts are defined modulo 64
- Padding and digest serialization use explicit little-endian conversion
- Recursive allocation failures propagate to the caller
- Split arithmetic avoids size multiplication overflow
- Split derivation uses a dedicated domain and versioned record tags
- Child-count selection uses rejection sampling instead of biased reduction
- Split weights stay in the bounded range 128–255 to limit imbalance
- Checked one-shot and streaming-final APIs report failures
- Reader-based recursion keeps one-shot and streaming digests identical
- Streaming input uses anonymous temporary storage and fixed-size RAM buffers
- Structural fields use fixed-position 64-bit little-endian encoding
- Negative depths and non-canonical child ranges or layouts are rejected
- Debug hooks no longer depend on compiler-specific weak symbols
- Streaming contexts document single-owner, non-concurrent use
- Length overflow, failed-reader, repeated-final, and cleanup paths are tested

## Design Philosophy

FCH builds diffusion along two axes:

- time: a fixed 16-round ARX compression core

- space: the recursive fractal tree structure

The recursive structure does not replace sufficient local mixing. Every leaf
block and child state passes through the 16-round core, and the tree adds
global diffusion while binding order and structure.

---

## Rationale

- Variable n-way splitting prevents uniform structural assumptions
- A 512-bit whole-input split derivation binds structure to all bytes in a node
- Separate split header, data, and output records remove the old directly
  steerable 64-bit accumulator
- Node, child, and leaf metadata provide explicit domain separation
- Order-dependent combine retains child position and length information
- Versioned unique tags and fixed-width records make structural boundaries explicit
- A fixed-rotation ARX core gives a clearer analysis target than the previous
  position-dependent S-box mixing
- The full core uses 16 rounds, eight rounds above the conservative
  reduced-round diffusion reference
- Reduced-round compression is available only in analysis builds; production
  hashing always uses all 16 rounds
- The deterministic analysis harness screens chosen differences, output-bit
  bias, linear correlation, fixed points, two-cycles, and near collisions
- Tree-attack regression checks cover truncated multicollision distribution,
  bounded second-preimage candidates, state grafting, and long-message
  splice/extension patterns
- A shared standalone/libFuzzer harness checks one-shot/streaming equivalence,
  split coverage, API failure paths, and context cleanup under sanitizers
- An 8 MiB two-layout stream test checks bounded-memory large-input behavior
- The 512-bit internal state gives FCH-256 a wider internal path than its output
- Recompression prevents linear state growth

---

## Reference Implementation Scope

This implementation prioritizes:

- clarity
- determinism
- structural transparency
- a fixed, analyzable core

Performance optimization follows design correctness and security analysis.

The eight-round operational gap is a testing policy, not a proven
cryptanalytic security margin. The bundled searches are bounded CI regression
checks, not a substitute for attack-based analysis or independent review.
Long inputs also retain a resource-exhaustion risk because split derivation
re-reads input across tree levels and streaming uses temporary storage.
The CI platform set covers Linux, macOS, and Windows on hosted x86-64
runners; it does not establish correctness on every ABI, filesystem, or
big-endian target.

No cryptographic security claims are made for this design or implementation.
