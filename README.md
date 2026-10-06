# Fractal Crypt-Hash (FCH)

Fractal Crypt-Hash (FCH) is a cryptographic research hash function built from
a 16-round ARX compression core and a canonical recursive tree. The compression
core handles local mixing, while fixed 1,024-byte leaves and position-bound
binary nodes carry changes across the message and combine them at the root.

This revision is an **FCH research candidate**. It preserves tree encoding 2,
padding 1, the 16-round core, and existing digests. Independent cryptographic
review and the stated full-round security strengths have not been established.

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
The [validation and security status](spec/validation_status.md) separates
local and completed CI passes, bounded observations, conditional analysis,
and work that has not been established.

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

Only `include/fch.h` and `include/fch_stream.h` are public headers. Tree,
compression, parameter, and research-hook headers in `src/` are internal.

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

## Library build and installation

From the repository root:

```sh
make -C build lib
cc -Iinclude consumer.c -Lbuild -lfch -o consumer
make -C build check-library check-install
```

`make all` builds the CLI and `libfch.a`. The static library has no OpenSSL
dependency. `check-library` reuses the existing vector test as an external
consumer; `check-install` runs it using only staged headers and the archive.

```sh
make -C build install PREFIX=/usr/local
make -C build install PREFIX=/usr DESTDIR="$PWD/build/obj/package"
```

Installation copies `libfch.a`, `fch.h`, and `fch_stream.h`. `LIBDIR` and
`INCLUDEDIR` can override the prefix-derived locations. Use an `AR` matching
the target toolchain for cross compilation. This is a research-candidate build,
not a production cryptography release.

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

From the repository root:

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

## Tests and research verification

From `build/`, the regular correctness suite runs seven C programs and the
CLI regression:

```sh
make check
make check-reference
make check-interoperability
```

Coverage includes fixed vectors, input/leaf/tree boundaries, exact tree
records and invariants, one-shot/streaming equivalence, invalid input, reader
and allocation failures, portability, and CLI behavior. The fixed
[interoperability corpus](analysis/interoperability-v1.tsv) contains 54 inputs
for both variants. C checks 1,296 one-shot/streaming digests across eleven
update plans and 216 overlapping-input/output digests, for 1,512 total; Python
and the C CLI each check 108 expected digests. The separate
reference comparison retains 384 cases with three fixed seeds.

Research and stress checks are separate:

```sh
make check-extended
make check-research
```

`check-extended` runs the existing stress program, including 8 MiB streaming
with two chunk patterns. `check-research` runs native cryptanalysis and tree
screens, consolidated avalanche/length/pattern/tree diffusion, trail and
characteristic searches, fixed reduced-round experiments, and conditional
second-preimage accounting. Trail replay requires `z3-solver==4.13.1.0`.
Individual groups remain available as `check-research-native`,
`check-diffusion`, `check-trails`, and `check-characteristics`.

Reproduce the fixed research profiles:

```sh
make check-full-rounds
make check-reduced-rounds
make check-second-preimage-bounds
```

These require exact matches to the checked-in all-round characteristic,
160-row reduced-round, and conditional FCH-512 bound reports. The expanded
characteristic profile covers 1,024 single-bit differences on 128 bases at
each round from 1 through 16. State/output minima are aggregated independently.
Round one is weak and excluded from quantitative acceptance thresholds.
The full 16-round observed minima are 448 working-state bits and 203
compression-output bits; these are bounded observations, not security bounds.
Profile updates require review and must not refresh expected results to hide
a regression.

Run the same combined AddressSanitizer and UndefinedBehaviorSanitizer suite as
the Linux CI job, with leak detection enabled by default:

```sh
make sanitizer-check SANITIZER_TARGETS=check
make sanitizer-check SANITIZER_TARGETS="check-research-native check-diffusion check-extended"
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
make bench-baseline BASELINE=benchmark-local.json
make bench-compare BASELINE=benchmark-local.json
```

The `baseline-v1` profile uses the fixed input seed and complete case matrix,
performs one warmup and five measured trials, and stores the median processor
time. Its JSON record also includes the source revision, operating system, CPU,
compiler, flags, throughput, peak heap, and allocation count. Comparisons
require the same recorded environment, reject resource-profile changes, and
use a 20 percent per-case throughput regression limit by default. Set
`MAX_REGRESSION` to choose another limit.
After reviewing an intentional heap/allocation reduction, compare with
`ALLOW_RESOURCE_IMPROVEMENT=1`; this still rejects increases. One-shot hashing
now uses a single 64-byte internal heap allocation independent of input length.

Build-setting changes trigger recompilation without `make clean`. Captures
include the binary and configuration hashes and reject compiler/flag labels
that disagree with the build record.

Optional OpenSSL comparisons use the separate 66-case `peers-v1` profile:

```sh
make bench-peers PEER_BASELINE=benchmark-peers.json
make bench-matrix MATRIX_DIR=obj/bench-matrix
python3 ../tools/fch_benchmark.py matrix --compiler gcc --compiler clang \
  --cflags "-std=c11 -Wall -Wextra -Wpedantic -O2" \
  --cflags "-std=c11 -Wall -Wextra -Wpedantic -O2 -flto" \
  --peers --output-dir obj/bench-peers-matrix
```

OpenSSL development headers and libcrypto are required only for peer runs;
custom paths can be supplied with `BENCH_OPENSSL_CFLAGS` and
`BENCH_OPENSSL_LIBS`. Peer hashes use the same payloads, iteration counts,
warmup, interleaved trials and timer as FCH. Their heap/allocation fields are
`unmeasured`, since library allocations are not instrumented. Comparisons
include library and CPU optimizations and describe these implementations,
not intrinsic construction costs. Compiler matrices rebuild sequentially and
save each capture and build log; they do not apply regression gates across
different compilers or flags. Ordinary regression comparisons still require
matching profiles, environments and peer-library versions.

Run the four retained sanitizer-backed libFuzzer targets with Clang:

```sh
make fuzz-smoke
make fuzz-campaign FUZZ_SECONDS=300
```

Smoke runs request 1,024 executions per target; the campaign runs each target
for the requested number of seconds. Both use named deterministic boundary
corpora and fixed seeds. Inputs, crashes, logs, binary/corpus hashes, revision,
compiler flags, and sanitizer settings are recorded under `build/fuzz/`.
ASan/UBSan and leak detection are enabled by default. A scheduled or manual
research CI job configures 600 seconds per target; it is skipped on normal
pushes. See the [validation record](spec/validation_status.md) for the campaigns
actually executed and their limits.

The `correctness` workflow covers Linux GCC/Clang, macOS Clang, Windows UCRT64
GCC with native binary stdin, 32-bit x86, and all seven C regressions on
big-endian PowerPC under QEMU. It also runs ASan/UBSan/leaks, GCC static
analysis, and C/Python interoperability. The separate `research-verification`
workflow runs native research, stress, benchmarks, content timing, fixed
profiles, all-round trail replay, and fuzz/runtime sanitizers. `make check-all`
combines local correctness, reference, interoperability, stress, research,
and benchmark checks; fuzzing and timing remain explicit commands.

## Documentation

- [Algorithm specification](spec/fch_spec.md)
- [Validation and security status](spec/validation_status.md)
- [Security analysis](spec/security_analysis.md)
- [Implementation notes](spec/implementation_notes.md)
