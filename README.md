# Fractal Crypt-Hash (FCH)

Fractal Crypt-Hash (FCH) is an experimental cryptographic hash function
based on a **fractal (self-similar) recursive structure** rather than
traditional round-based compression.

---

## Overview

Unlike conventional hash functions that rely on repeated rounds,
FCH achieves diffusion through **recursive fractal decomposition**.

A small change in the input propagates:

- locally at leaf nodes,
- recursively through variable n-way splits,
- and globally at the root via recompression.

---

## Features

- Fractal recursive hash structure
- Variable n-way (2–6) pattern-based splitting
- Order-dependent tree recombination
- Minimal non-linearity at leaf level (XOR / ADD / ROTATE / S-box)
- Recompression at each internal node
- Deterministic, non-keyed hash function

---

## Supported Variants

| Variant | Output Size | Internal State |
| ------ | ----------- | -------------- |
| FCH-256 | 256 bits | 4 × uint64 |
| FCH-512 | 512 bits | 8 × uint64 |

---

## Usage

```c
uint8_t out256[32];
uint8_t out512[64];

fch_hash_256(data, len, out256);
fch_hash_512(data, len, out512);
```

## Testing

The implementation includes:

- Statistical avalanche tests (length/bit diffusion)
- Determinism tests (same input → same output)
- Boundary condition tests
- Structural invariant tests (split coverage)

The reference implementation includes extensive tests for
determinism, boundary conditions, structural invariants,
and statistical diffusion behavior.

Test programs:

- `tests/test_avalanche.c`
- `tests/test_consistency.c`
- `tests/test_boundaries.c`
- `tests/test_invariants.c`

Build/run:

```sh
cd build
make test
./test_consistency
./test_boundaries
./test_invariants
./test_avalanche
```

If `make` is unavailable (Windows), you can compile directly with `gcc`:

```sh
gcc -Wall -Wextra -O2 -Iinclude tests/test_consistency.c src/*.c -o build/test_consistency.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_boundaries.c  src/*.c -o build/test_boundaries.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_invariants.c  src/*.c -o build/test_invariants.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_avalanche.c   src/*.c -o build/test_avalanche.exe
```
