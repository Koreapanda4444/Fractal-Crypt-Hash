# Fractal Crypt-Hash (FCH)

Fractal Crypt-Hash (FCH) is a cryptographic research hash function built from
a 16-round ARX compression core and an input-dependent recursive tree. The
compression core handles local mixing, while the tree carries changes through
different regions of the message and combines them at the root.

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
2. Derive a variable split from the complete input of the current node.
3. Process each child recursively until it becomes a leaf.
4. Compress the child states in order, together with their positions and
   lengths.
5. Finalize the root in a domain dedicated to FCH-256 or FCH-512.

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
| Fan-out | 2–6 children |
| Split weights | 128–255 |
| Leaf threshold | 64 bytes |
| Maximum tree depth | 16 |

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

The streaming API stores incoming chunks in an anonymous temporary file and
reads them through fixed-size buffers during finalization. This keeps
application RAM usage bounded and produces the same digest as the one-shot API.

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

Build the C CLI and compare both implementations across deterministic boundary
and recursive-tree cases:

```sh
cd build
make check-reference
```

## Tests

The repository includes tests for:

- determinism, fixed outputs, boundaries, and invalid inputs
- avalanche behavior and reduced-round diffusion
- split coverage, balance, relocation, and configuration sensitivity
- domain separation and portable little-endian serialization
- bounded differential, linear, fixed-point, cycle, and near-collision searches
- multicollision, second-preimage, grafting, and long-message tree patterns
- one-shot and streaming equivalence, API lifecycle, and reader failures
- sanitizer and libFuzzer smoke runs
- bounded-memory processing of an 8 MiB input

Run the regular and extended suites:

```sh
cd build
make check
make check-extended
```

Run the bounded libFuzzer target with Clang:

```sh
make fuzz-smoke
```

CI builds and tests the code with GCC and Clang on Linux, Clang on macOS, and
UCRT64 GCC on Windows. Linux jobs also run AddressSanitizer and
UndefinedBehaviorSanitizer.

## Documentation

- [Algorithm specification](spec/fch_spec.md)
- [Implementation notes](spec/implementation_notes.md)
