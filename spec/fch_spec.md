# Fractal Crypt-Hash Specification

## 0. Scope & Non-Goals

This specification describes the **deterministic behavior** of the FCH reference design.
It does **not** claim cryptographic security (collision resistance / preimage resistance / etc.),
and it should not be used as a basis to deploy FCH in production security contexts.

FCH is intended for research, experimentation, benchmarking, and discussion.

---

## 1. Security Targets and Status

### 1.1 Attacker Model

FCH is a public, deterministic, non-keyed hash function. The attacker is
assumed to know the algorithm, constants, messages, and hash outputs and may
freely choose messages to hash. The targets below cover only the classical
computation model. No security strength against quantum attacks is currently
defined or claimed.

### 1.2 Target Strength

| Variant | Output Size | Collision Resistance | Preimage Resistance | Second-Preimage Resistance |
| ------- | ----------- | -------------------- | ------------------- | -------------------------- |
| FCH-256 | 256 bits | 2^128 | 2^256 | 2^256 |
| FCH-512 | 512 bits | 2^256 | 2^512 | 2^512 |

Each value is the approximate generic attack cost expected from an ideal hash
of the corresponding output size. These values are **design targets** that FCH
must aim to meet, not security claims or proofs for the current design.
Structural weaknesses may reduce the actual security strength below these
targets.

### 1.3 Structural Security Targets

The FCH tree construction is intended to:

- separate root, internal-node, leaf, and child-state domains
- bind node type, depth, length, child count, child order, offset, and length
  without ambiguity
- prevent different messages or tree structures from merging into the same
  internal representation through structural collisions
- avoid tree-specific shortcuts for multicollisions, herding, and long-message
  second-preimage attacks below the target strengths
- produce the same digest for the same message through one-shot and streaming APIs

### 1.4 Candidate Evaluation Criteria

Before FCH can be described as a security candidate suitable for serious
evaluation, it requires at least:

- a stable and complete specification that matches the implementation
- differential, linear, and structural analysis of the full and reduced designs
- an explicit security margin that can be compared with attack results
- no known full-design attack faster than the target costs in the table
- reproducible test results and meaningful independent public review

Statistical distribution, avalanche behavior, test vectors, and implementation
tests support analysis but do not by themselves establish cryptographic security.

### 1.5 Round Policy and Current Margin Status

Production FCH always uses 16 rounds. The reference implementation measures
single-bit core diffusion at 4, 8, 12, and 16 rounds, and conservatively uses
8 rounds as the reduced-round reference. The full design is therefore eight
rounds above that reference.

This is an **operational round gap**, not a proven cryptanalytic security
margin. The reference harness performs bounded chosen-difference searches at
1, 2, 4, 8, and 16 rounds, differential bit-bias and linear-correlation
screens at 8 and 16 rounds, and fixed-point, two-cycle, collision, and
near-collision searches. These deterministic CI checks only reject simple
properties within their sample bounds. They do not measure resistance to
general differential, linear, rotational, rebound, or structural attacks.
Attack-based analysis of reduced rounds and the full design is still required
before an actual security margin can be claimed.

---

## 2. Input Padding

Given input message M of length L bytes:

1. Append byte 0x80
2. Append zero bytes
3. Append 64-bit little-endian value of (L × 8)
4. Ensure total length ≥ FCH_MIN_BLOCK_SIZE

Inputs whose byte length cannot be represented safely by the 64-bit bit-length
field are rejected by the checked API.

---

## 3. Fractal Processing

Processing starts at depth = 0.
Negative depths and internal states other than eight words are not valid tree
representations and are rejected.

At each node:

- If depth ≥ MAX_DEPTH or length ≤ MIN_BLOCK_SIZE:
  → Leaf compression
- Else:
  → Variable n-way split and recursive processing

---

## 4. Variable n-Way Split

- n ∈ [2, 6]
- Blocks cover the entire input without overlap
- Child blocks in an internal node are non-empty and contiguous from offset 0

For every internal node, FCH derives 512 bits of split material with the same
ARX core but a dedicated `FCHSPLT1` domain. It first compresses a fixed
128-byte `FCHSPH01` header with split-derivation version 1. The header binds
the node length and depth, internal-state width, fan-out range, split-weight
range, minimum block size, maximum depth, compression block size, round count,
and tree-encoding version.

The complete node input is then absorbed in records consisting of the
eight-byte `FCHSPD01` tag followed by up to 120 original bytes. Actual record
length, cumulative byte count, and final-record status are injected through
the compression tweaks. A fixed `FCHSPO01` output record binds the same
configuration, input-record count, and a draw counter.

Let `r = N_MAX - N_MIN + 1` and `t = (2^64 mod r)`. The first material word
is accepted only when it is at least `t`; otherwise another domain-separated
output draw is compressed. The child count is:

`n = N_MIN + (material[0] mod r)`

Internal nodes shorter than `2 × MIN_BLOCK_SIZE` use `n = N_MIN` so every
supported fan-out configuration remains canonical. All other internal nodes
use the formula above. The rejection step avoids modulo bias. For each child
`i`, its weight is:

`weight[i] = 128 + (material[i + 1] AND 0x7f)`

Weights are therefore in `[128, 255]`. This bounded ratio prevents a selected
split from producing an extremely unbalanced internal node. Length allocation
still guarantees at least one byte per child and assigns rounding remainder to
the final child.

Because FCH is public, deterministic, and non-keyed, an attacker can still
search several messages for a desired value of `n`. The split derivation is
intended to remove cheap local or algebraic control over the old 64-bit
accumulator and to bound pathological imbalance; it is not a secret or keyed
partition function.

---

## 5. Leaf Compression

FCH-256 and FCH-512 both use a 512-bit internal tree state consisting of
eight 64-bit words. A leaf initializes this state with separate domains for
root leaves and internal leaves.

The leaf first compresses a 128-byte header containing the `FCHLEAF1` type
tag and encoding version 1. Fixed 64-bit little-endian fields bind the domain,
leaf length, depth, state width, minimum block size, maximum depth, fan-out
range, compression block size, round count, split-weight range, and
split-derivation version. Unused fields are zero.

Each leaf-data record starts with the eight-byte `FCHLDAT1` tag followed by up
to 120 original input bytes. The unused tail of the last record is zero. The
actual record length and cumulative original-byte count are injected through
the block-length tweak and counter. An empty leaf still processes one final
tag-only data record. The last data record carries the final flag.

### 5.1 Canonical Record Rules

- Every type tag is exactly eight ASCII bytes.
- Every numeric field is an unsigned 64-bit little-endian value.
- The encoding version is 1. A format change also changes the version and
  fixed test vectors.
- Leaf headers, node headers, child records, output headers, split headers,
  and split-output records are 128 bytes. Tagged leaf and split data records
  use their actual length from 8 to 128 bytes.
- Record types use distinct tags, compression flags, and domains.
- Negative depths, fewer than two or more than six children, empty children,
  gaps, overlaps, and out-of-range partitions are rejected.

### 5.2 ARX Compression Core

Each compression uses a 16-word working state. Its first eight 64-bit words
are initialized from the chaining state and its last eight words from a fixed
IV. The counter, actual block length, state width, domain, and flags are XORed
into the working state before 16 rounds. Each round applies four column G
functions followed by four diagonal G functions so every working word mixes.

G uses only 64-bit modular addition, XOR, and right rotation:

1. `a = a + b + x`, `d = ROTR64(d XOR a, 32)`
2. `c = c + d`, `b = ROTR64(b XOR c, 24)`
3. `a = a + b + y`, `d = ROTR64(d XOR a, 16)`
4. `c = c + d`, `b = ROTR64(b XOR c, 63)`

The message schedule contains ten fixed permutations and repeats cyclically.
Rounds 0–9 use those ten permutations in order, and rounds 10–15 repeat
permutations 0–5.

After 16 rounds, each chaining word is updated as
`h[i] XOR work[i] XOR work[i + 8]`.
The G structure and rotation distances follow the 64-bit ARX structure used
by BLAKE2b, but FCH has its own initialization, tweak layout, message mode,
and tree construction. FCH therefore does not inherit BLAKE2b's security
claims.

---

## 6. Tree Combine & Recompression

Internal nodes:

- use separate domains for the root node and other internal nodes
- first compress a version-1 node header tagged `FCHNODE1`
- bind the split-weight range and split-derivation version in that header
- serialize each child as the following 128-byte `FCHCHLD1` record
- feed child blocks to the 16-round ARX core in order
- set the final flag on the last child block
- retain the full 512-bit state after combination

| 64-bit word | Child-record field |
| ----------- | ------------------ |
| 0 | `FCHCHLD1` |
| 1 | Encoding version |
| 2 | Parent node length |
| 3 | Parent depth |
| 4 | Total child count |
| 5 | Child index |
| 6 | Child offset within the parent |
| 7 | Child length |
| 8–15 | 512-bit child state |

---

## 7. Output

- Compress the root state once more with a fixed header tagged `FCHOUT01`
  containing the encoding version, output width, internal-state width, block
  size, and round count
- Use different output domains for FCH-256 and FCH-512
- FCH-256 serializes the first four words of the final 512-bit state as
  little-endian bytes
- FCH-512 serializes all eight final state words as little-endian bytes
- Result sizes are 32 bytes for FCH-256 and 64 bytes for FCH-512

Allocation or validation failure does not produce an alternative tree. The
checked API reports failure and clears the output buffer.

---

## 8. Streaming Processing

- Update calls write input bytes to anonymous temporary storage
- Finalization exposes the stored input and virtual padding through a
  random-access reader
- The reader follows the same split, leaf, and combine rules as one-shot input
- RAM usage is bounded by fixed-size I/O buffers and recursive states
- Finalization closes the temporary storage
- Updates after finalization and repeated finalization fail

Streaming therefore preserves the one-shot digest but requires temporary-file
support and may perform more I/O.

---

## 9. Tree Attack Analysis Status

The tree encoding is intended to prevent a valid child state from being
silently reused in a different structural context. This is supported by the
following bindings:

- leaf headers bind leaf length and depth
- node headers bind parent length, depth, child count, and configuration
- child records bind index, offset, length, parent metadata, and child state
- root, internal-node, leaf, split, and output records use separate domains
- the content-dependent split derivation reads the complete parent input

The current attack status is:

| Attack class | Current automated screen | Remaining limitation |
| ------------ | ------------------------ | -------------------- |
| Multicollision | 4,096 leaf states and 4,096 derived node states are checked for exact collisions and 20-bit-prefix bucket anomalies | A small truncated screen cannot establish the expected 512-bit internal collision cost or rule out a Joux-style construction |
| Second preimage | 512 related candidates for a 16 KiB target cover bit changes, chunk replacement, swaps, and splices | This does not establish the FCH-256 or FCH-512 target costs against expandable-message, herding, or other dedicated attacks |
| Tree-shape/grafting | Fixed child states are recombined under binary, flat, skewed, reordered, rebounded, and depth-shifted contexts | Structural binding still depends on the compression core resisting collisions and chosen-state attacks |
| Long message | Fifteen 256 KiB variants cover append, truncate, prefix, embedded-padding extension, chunk swap/copy/rotation, boundary edits, and low-entropy patterns | Larger searches, adversarial split steering, depth-cap leaves, and resource-exhaustion behavior still require analysis |

The 20-bit prefix used by the multicollision screen is deliberately small
enough for ordinary birthday collisions to appear during CI. The test checks
for abnormal bucket concentration and exact 512-bit equality; its result
cannot be extrapolated into a proof about the full state.

Split derivation reads every byte of each visited node before recursively
processing its children. For an input of length `L` and realized tree depth
`d`, input-reading work is therefore `O(L * d)`, with `d` capped at 16.
One-shot hashing also allocates `O(L)` padding storage. Streaming keeps RAM
bounded but uses `O(L)` temporary storage and can repeat I/O across tree
levels. A large-input CPU, disk, or temporary-file exhaustion attack is
therefore an implementation risk even when no cryptographic break is found.

The current tests found no simple structural alias, full-state collision, or
second preimage in their bounded samples. This does not change FCH's
experimental status or replace independent tree-hash cryptanalysis.
