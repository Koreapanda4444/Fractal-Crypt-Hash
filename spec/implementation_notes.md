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

## Design Philosophy

FCH builds diffusion along two axes:

- time: a fixed 12-round ARX compression core

- space: the recursive fractal tree structure

The recursive structure does not replace sufficient local mixing. Every leaf
block and child state passes through the 12-round core, and the tree adds
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

No cryptographic security claims are made for this design or implementation.
