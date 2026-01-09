# Implementation Notes

## Design Philosophy

The core idea of FCH is to move diffusion from:

- time (rounds)
to:

- space (recursive structure)

Leaf compression is intentionally minimal;
most diffusion arises from structural recombination.

---

## Rationale

- Variable n-way splitting prevents uniform structural assumptions
- Pattern-based splits bind structure to input content
- Order-dependent combine ensures positional sensitivity
- Recompression prevents linear state growth

---

## Reference Implementation Scope

This implementation prioritizes:

- clarity
- determinism
- structural transparency

Performance and cryptographic hardness are secondary.
