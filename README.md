# Fractal Crypt-Hash (FCH)

Fractal Crypt-Hash (FCH) is an experimental hash design intended to become
a cryptographic hash candidate. It combines a **16-round ARX compression
core** with a **fractal (self-similar) recursive structure**.

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

The current round policy uses 16 full rounds and an 8-round reduced reference,
leaving an 8-round operational gap. This is a conservative testing margin, not
a cryptanalytic proof or a claim of 8 rounds of proven security margin.

---

## Overview

FCH combines local diffusion from a fixed-round compression core with
structural diffusion from **recursive fractal decomposition**.

A small change in the input propagates:

- locally at leaf nodes,
- recursively through variable n-way splits,
- and globally at the root via recompression.

---

## Features

- Fractal recursive hash structure
- 16-round 64-bit ARX compression core over 128-byte blocks
- 8-round operational gap above the reduced-round reference
- 512-bit internal tree state for both variants
- Domain-separated 512-bit split derivation over the entire node input
- Rejection-sampled fan-out and bounded 128–255 split weights
- Order-dependent tree recombination
- Separate root, internal-node, leaf, child, and output-variant domains
- Canonical tree encoding with versioned type tags and fixed fields
- ADD / ROTATE / XOR mixing with fixed rotation distances
- Recompression for every leaf block and internal-node child
- Deterministic, non-keyed hash function

---

## Supported Variants

| Variant | Output Size | Internal State |
| ------ | ----------- | -------------- |
| FCH-256 | 256 bits | 8 × uint64 |
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
- Reduced-round core diffusion checks at 4, 8, 12, and 16 rounds
- Determinism tests (same input → same output)
- Boundary condition tests
- Structural invariant tests (split coverage)
- Split-derivation diffusion, relocation, and balance tests
- Portability and domain-separation tests
- Split-configuration sensitivity tests
- Bounded differential and reduced-round low-weight searches
- Linear-correlation, fixed-point, two-cycle, and near-collision searches
- Tree-attack screens for multicollisions, second preimages, state grafting,
  and long-message splice/extension patterns
- Deterministic fuzz smoke tests for one-shot/streaming equivalence, split
  invariants, API misuse, reader failures, and context lifecycle
- An 8 MiB bounded-memory streaming test with different chunk layouts

The reference implementation includes extensive tests for
determinism, boundary conditions, structural invariants,
and statistical diffusion behavior.

The cryptanalysis harness is deterministic and intentionally bounded so it
can run in CI. Passing it only rules out the tested simple distinguishers and
searches; it is not evidence of collision, preimage, or full-design security.

Test programs:

- `tests/test_avalanche.c`
- `tests/test_consistency.c`
- `tests/test_boundaries.c`
- `tests/test_invariants.c`
- `tests/test_portability.c`
- `tests/test_vectors.c`
- `tests/test_split_sensitivity.c`
- `tests/test_cryptanalysis.c`
- `tests/test_tree_attacks.c`
- `tests/test_hardening.c`

Build/run:

```sh
cd build
make check
make check-extended
```

Run the bounded libFuzzer target with Clang:

```sh
cd build
make fuzz-smoke
```

CI runs the suite on Linux with GCC and Clang, macOS with Clang, and Windows
with UCRT64 GCC. It also runs libFuzzer, AddressSanitizer, and
UndefinedBehaviorSanitizer.

If `make` is unavailable (Windows), you can compile directly with `gcc`:

```sh
gcc -Wall -Wextra -O2 -Iinclude tests/test_consistency.c src/*.c -o build/test_consistency.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_boundaries.c  src/*.c -o build/test_boundaries.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_invariants.c  src/*.c -o build/test_invariants.exe
gcc -Wall -Wextra -O2 -DFCH_ENABLE_REDUCED_ROUND_TESTS -Iinclude tests/test_avalanche.c src/*.c -o build/test_avalanche.exe
gcc -Wall -Wextra -O2 -DFCH_ENABLE_REDUCED_ROUND_TESTS -Iinclude tests/test_cryptanalysis.c src/*.c -o build/test_cryptanalysis.exe
gcc -Wall -Wextra -O2 -DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL -Iinclude tests/test_tree_attacks.c src/*.c -o build/test_tree_attacks.exe
gcc -Wall -Wextra -O2 -DFCH_FUZZ_STANDALONE -Iinclude tests/test_hardening.c src/*.c -o build/test_hardening.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_vectors.c     src/*.c -o build/test_vectors.exe

gcc -Wall -Wextra -O2 -Iinclude tools/fch.c              src/*.c -o build/fch.exe

build\\fch.exe -256 README.md
```
