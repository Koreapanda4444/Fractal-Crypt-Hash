# Fractal Crypt-Hash (FCH)

Fractal Crypt-Hash (FCH) is a cryptographic research hash function built from
a 16-round ARX compression core and a canonical recursive tree. The compression
core handles local mixing, while fixed 1,024-byte leaves and position-bound
binary nodes carry changes across the message and combine them at the root.

FCH is an open research project. Its security goals are defined below, and
independent public analysis is still ongoing.

Korean documentation: [README.ko.md](README.ko.md)

## Security goals

FCH is deterministic, public, and non-keyed. The current design targets the
generic classical attack costs expected for each output size.

| Variant | Output | Collision | Preimage | Second preimage |
| ------- | ------ | --------- | -------- | --------------- |
| FCH-256 | 256 bits | 2^128 | 2^256 | 2^256 |
| FCH-512 | 512 bits | 2^256 | 2^512 | 2^512 |

These figures are design targets. The specification describes what still has
to be analyzed before those targets can be treated as established properties.

## Design

FCH processes a message as a recursive tree:

1. Pad the message and start at the root.
2. Divide the padded input into consecutive 1,024-byte leaves.
3. Build the unique left-complete binary tree for that leaf count.
4. Compress child states in order, together with their positions and
   lengths.
5. Finalize the root in a domain dedicated to FCH-256 or FCH-512.

The tree shape depends only on the padded length. Message bytes cannot select
the number of children or move a boundary, and completed power-of-two prefix
subtrees keep the same encoding when a suffix is added.

The two variants share a 512-bit internal state. FCH-256 uses a separate
output-finalization domain and is not simply a truncated FCH-512 digest.

### Main parameters

| Parameter | Value |
| --------- | ----- |
| Word size | 64 bits |
| Internal state | 512 bits (8 words) |
| Compression input | 128 bytes (16 words) |
| Full rounds | 16 |
| Reduced-round analysis reference | 8 rounds |
| Tree encoding | Version 2 |
| Leaf span | 1,024 bytes |
| Internal-node arity | 2 |
| Tree level | Determined by the leaf count |

The ARX G function, IV, rotation distances, and message permutations are based
on BLAKE2b components. FCH uses its own initialization, tweaks, record format,
feed-forward context, round count, and tree construction.

## API

```c
#include "fch.h"

uint8_t out256[32];
uint8_t out512[64];

int ok256 = fch_hash_256_checked(data, len, out256);
int ok512 = fch_hash_512_checked(data, len, out512);
```

The checked functions return `1` on success and `0` for invalid input,
unsupported length, or allocation failure. Compatibility wrappers with the
original `void` signatures are also available.

### Streaming

The streaming API compresses each complete 1,024-byte leaf during `update`.
The context keeps only one unfinished leaf and one completed subtree per tree
level. `final` adds the padding, processes the remaining leaf data, and folds
the saved subtrees into the root. It does not retain or replay the complete
input, requires no temporary file, and produces the same result as the one-shot
API.

```c
#include "fch_stream.h"

fch256_ctx ctx;
fch256_init(&ctx);
fch256_update(&ctx, chunk1, chunk1_len);
fch256_update(&ctx, chunk2, chunk2_len);
int ok = fch256_final_checked(&ctx, out256);
fch256_free(&ctx);
```

An active context has a single owner. Updates after finalization and repeated
finalization are rejected.

## Command-line tool

Build the tool:

```sh
cd build
make all
```

Hash a file or standard input:

```sh
./fch -256 path/to/file
./fch -512 path/to/file
cat path/to/file | ./fch -256
```

### Python reference

`tools/fch_reference.py` is a direct, readable implementation of the
specification using only the Python standard library. It is kept separate from
the C sources so the two implementations can be compared independently.

```sh
python3 tools/fch_reference.py -256 path/to/file
python3 tools/fch_reference.py -512 path/to/file
```

Build the C CLI and compare both implementations across deterministic boundary,
recursive-tree, and fixed-seed differential cases:

```sh
cd build
make check-reference
```

## Tests

The repository includes tests for:

- determinism, fixed outputs, boundaries, and invalid inputs
- avalanche behavior and reduced-round diffusion
- canonical tree boundaries, prefix stability, and content independence
- domain separation and portable little-endian serialization
- bounded differential, linear, fixed-point, cycle, and near-collision searches
- multicollision, second-preimage, grafting, and long-message tree patterns
- one-shot and streaming equivalence, API lifecycle, and forced allocation failures
- sanitizer-backed libFuzzer targets for core hashing, stream chunking,
  padding boundaries, tree combination, and CLI input handling
- bounded-memory processing of an 8 MiB input

Run the regular and extended suites:

```sh
cd build
make check
make check-extended
```

Reproduce the 1-through-16-round diffusion, XOR-differential, rotational
symmetry, and structured-input bias profile:

```sh
make check-reduced-rounds
```

The command validates unit tests and reruns the fixed `reduced-round-v1`
profile against `analysis/reduced-round-v1.json`. The checked-in JSON contains
all 160 per-round results and thresholds; an intentional profile update can be
captured with `make reduced-round-report` and must be reviewed with the code
change.

Reproduce the conditional FCH-512 second-preimage accounting:

```sh
make check-second-preimage-bounds
```

This checks padding and tree boundaries, canonical descriptor uniqueness, and
the separation between complete-message queries and typed-map work against
`analysis/fch512-second-preimage-v1.json`. The reported exponents are
ideal-map union-bound scales, not attacks or security proofs. An intentional
profile update is captured with `make second-preimage-report`.

Run the same combined AddressSanitizer and UndefinedBehaviorSanitizer suite as
the Linux CI job:

```sh
make sanitizer-check
```

Build and run the full scaling benchmark:

```sh
make bench
./bench_hash
```

Use `make bench-check` for the shorter CI run. The CSV output reports input
size, streaming chunk size, iterations, processor time, throughput, peak internal
heap use, and allocations per hash. The benchmark covers both digest sizes in
one-shot and streaming modes and checks that streaming memory stays bounded as
the input grows.

Capture a machine-specific reproducible baseline and compare a later build:

```sh
make bench-baseline BASELINE=../bench/baselines/local.json
make bench-compare BASELINE=../bench/baselines/local.json
```

The `baseline-v1` profile uses the fixed input seed and complete case matrix,
performs one warmup and five measured trials, and stores the median processor
time. Its JSON record also includes the source revision, operating system, CPU,
compiler, flags, throughput, peak heap, and allocation count. Comparisons
require the same recorded environment, reject resource-profile changes, and
use a 20 percent per-case throughput regression limit by default. Set
`MAX_REGRESSION` to choose another limit.

Run the bounded focused libFuzzer targets with Clang:

```sh
make fuzz-smoke
```

The smoke run uses 1,024 inputs per target and a 10-second per-input timeout.
`FUZZ_RUNS` and `FUZZ_TIMEOUT` can override those bounds for local runs.

CI builds and tests the code with GCC and Clang on Linux, Clang on macOS, and
UCRT64 GCC on Windows. It also runs a 32-bit x86 build and executes the fixed
vectors, streaming checks, invariants, and failure paths on big-endian PowerPC
through QEMU. Dedicated Linux jobs run the combined sanitizer suite and all
five bounded libFuzzer targets.

## Documentation

- [Algorithm specification](spec/fch_spec.md)
- [Security analysis](spec/security_analysis.md)
- [Implementation notes](spec/implementation_notes.md)
