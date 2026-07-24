# Implementation Notes

## Portability and error handling

- Rotation counts are defined modulo 64
- Padding and digest serialization use explicit little-endian conversion
- Recursive allocation failures propagate to the caller
- Split arithmetic avoids size multiplication overflow
- Checked one-shot and streaming-final APIs report failures
- Reader-based recursion keeps one-shot and streaming digests identical
- Streaming input uses anonymous temporary storage and fixed-size RAM buffers

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
- Whole-input split seeds bind structure to all bytes in a node
- Node, child, and leaf metadata provide explicit domain separation
- Order-dependent combine retains child position and length information
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
