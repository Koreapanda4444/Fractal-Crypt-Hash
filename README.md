# Fractal Crypt-Hash (FCH)

Fractal Crypt-Hash (FCH) is an experimental hash design intended to become
a cryptographic hash candidate. It uses a **fractal (self-similar) recursive
structure** rather than traditional round-based compression.

---

## Security Notice (Read First)

FCH is a **research / experimental** hash design and reference implementation.
It has **not** undergone broad public cryptanalysis or independent security review.

Do **not** use FCH in production or for security-critical purposes, including (but not limited to):

- authentication, signatures, tokens, or MACs
- password hashing / key derivation
- integrity checks where adversaries exist
- any scenario where a broken hash causes harm

This repository is intended for learning, experimentation, benchmarking, and discussion.

---

## Security Targets

FCH is intended to be a public, deterministic, non-keyed hash function with
the following resistance targets against classical generic attacks.

| Variant | Collision Resistance | Preimage Resistance | Second-Preimage Resistance |
| ------- | -------------------- | ------------------- | -------------------------- |
| FCH-256 | 2^128 | 2^256 | 2^256 |
| FCH-512 | 2^256 | 2^512 | 2^512 |

These values are **design targets**, not claims that the current implementation
achieves them and not security guarantees. FCH is currently an **unvalidated
cryptographic hash candidate**. Passing statistical diffusion and avalanche
tests does not establish collision, preimage, or second-preimage resistance.

Stronger security wording requires a stable specification, analysis of the
full and reduced designs, a documented security margin, no known attacks below
the targets, and meaningful independent public review. The detailed targets
and evaluation criteria are defined in `spec/fch_spec.md`.

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
- Variable n-way (2–6) splitting derived from the entire node input
- Order-dependent tree recombination
- Separate root, internal-node, leaf, and child domains
- Full-width leaf states with XOR / ADD / ROTATE / S-box mixing
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

int ok256 = fch_hash_256_checked(data, len, out256);
int ok512 = fch_hash_512_checked(data, len, out512);
```

The checked functions return `1` on success and `0` on invalid input,
allocation failure, or an unsupported input length. The original `void`
functions remain available as compatibility wrappers.

### Bounded-memory streaming API

FCH provides a streaming API that writes incoming chunks to an anonymous
temporary file. Finalization reads that data through fixed-size buffers, so RAM
usage does not grow with the total input size. The digest is identical to the
one-shot API. This mode requires temporary-file support and can be slower.

```c
#include "fch_stream.h"

fch256_ctx ctx;
fch256_init(&ctx);
fch256_update(&ctx, chunk1, chunk1_len);
fch256_update(&ctx, chunk2, chunk2_len);
int ok = fch256_final_checked(&ctx, out256);
fch256_free(&ctx);
```

After finalization, further updates and repeated finalization are rejected.
The CLI uses the same bounded-memory path for files and standard input.

### CLI

Build the CLI:

```sh
cd build
make all
```

Hash a file:

```sh
./fch -256 path/to/file
./fch -512 path/to/file
```

Hash stdin:

```sh
cat path/to/file | ./fch -256
```

## Testing

The implementation includes:

- Statistical avalanche tests (length/bit diffusion)
- Determinism tests (same input → same output)
- Boundary condition tests
- Structural invariant tests (split coverage)
- Portability and domain-separation tests
- Split-configuration sensitivity tests

The reference implementation includes extensive tests for
determinism, boundary conditions, structural invariants,
and statistical diffusion behavior.

Test programs:

- `tests/test_avalanche.c`
- `tests/test_consistency.c`
- `tests/test_boundaries.c`
- `tests/test_invariants.c`
- `tests/test_portability.c`
- `tests/test_vectors.c`
- `tests/test_split_sensitivity.c`

Build/run:

```sh
cd build
make check
```

The CI workflow runs the suite with GCC and Clang and also runs
AddressSanitizer and UndefinedBehaviorSanitizer.

If `make` is unavailable (Windows), you can compile directly with `gcc`:

```sh
gcc -Wall -Wextra -O2 -Iinclude tests/test_consistency.c src/*.c -o build/test_consistency.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_boundaries.c  src/*.c -o build/test_boundaries.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_invariants.c  src/*.c -o build/test_invariants.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_avalanche.c   src/*.c -o build/test_avalanche.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_vectors.c     src/*.c -o build/test_vectors.exe

gcc -Wall -Wextra -O2 -Iinclude tools/fch.c              src/*.c -o build/fch.exe

build\\fch.exe -256 README.md
```
