# Implementation Notes

## Portability and error handling

- Rotation counts are defined modulo 64
- Padding and digest serialization use explicit little-endian conversion
- Recursive allocation failures propagate to the caller
- Split arithmetic avoids size multiplication overflow
- Checked one-shot and buffered-final APIs report failures

## Design Philosophy

The core idea of FCH is to move diffusion from:

- time (rounds)
to:

- space (recursive structure)

Diffusion is shared between full-width leaf mixing and structural
recombination. Leaf and node processing use separate domain constants.

---

## Rationale

- Variable n-way splitting prevents uniform structural assumptions
- Whole-input split seeds bind structure to all bytes in a node
- Node, child, and leaf metadata provide explicit domain separation
- Order-dependent combine retains child position and length information
- Recompression prevents linear state growth

---

## Reference Implementation Scope

This implementation prioritizes:

- clarity
- determinism
- structural transparency

Performance and cryptographic hardness are secondary.

No cryptographic security claims are made for this design or implementation.
