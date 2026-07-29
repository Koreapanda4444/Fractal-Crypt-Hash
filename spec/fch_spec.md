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

---

## 10. Reference Implementation Hardening

The reference implementation applies the following checks:

- checked one-shot and streaming APIs reject unsupported lengths and invalid
  pointers before reading input
- failed streaming contexts remain failed, finalization closes temporary
  storage, and repeated finalization returns failure with a zeroed output
- recursive reader failures propagate without returning a partial state
- the debug-hook path uses an explicit external-hook switch instead of a
  compiler-specific weak symbol
- a shared deterministic/libFuzzer harness compares one-shot and streaming
  outputs, verifies split coverage, and exercises error and cleanup paths
- an 8 MiB input is streamed through two different chunk layouts without
  retaining the full message in application memory

CI runs strict builds on hosted x86-64 Linux, macOS, and Windows, with
AddressSanitizer and UndefinedBehaviorSanitizer on Linux. The bounded
libFuzzer job is a regression smoke test, not an exhaustive fuzzing campaign.
The platform matrix does not cover every ABI, allocator, filesystem,
big-endian target, or temporary-file failure mode.

Streaming contexts have single-owner semantics: initialize before use, do not
copy or access an active context concurrently, and call the matching free
function after finalization or failure.

---

## 11. Security Rationale and Parameter Selection

This section records why the current parameters were selected. It is a design
rationale, not a proof that the selections achieve the targets in Section 1.
Where a choice is supported only by engineering bounds or empirical tests,
that limitation is stated explicitly.

### 11.1 State, Word, Block, and Output Widths

| Parameter | Selection | Rationale |
| --------- | --------- | --------- |
| Word width | 64 bits | Matches the selected ARX G function and permits portable modular addition, XOR, and fixed rotations with explicit `uint64_t` arithmetic |
| Internal state | 8 words / 512 bits | An ideal 512-bit state has a generic collision scale of approximately `2^256`, so state width alone does not place a lower generic collision ceiling below the FCH-512 target |
| Compression input | 16 words / 128 bytes | Each round invokes eight G functions and consumes two scheduled message words per G, so all 16 message words are used once per round |
| FCH-256 output | 4 words / 256 bits | Matches the `2^128` generic collision and `2^256` generic preimage targets |
| FCH-512 output | 8 words / 512 bits | Exposes the full state width after output finalization and matches the `2^256` generic collision target |

Both variants keep the same 512-bit tree state. FCH-256 therefore does not
introduce a narrower state inside the tree, and the implementation has only
one internal compression width to analyze. FCH-256 and FCH-512 use distinct
output-finalization domains before serialization, so FCH-256 is not defined as
the raw first half of the FCH-512 digest.

These width arguments describe only generic ideal-function ceilings. They do
not establish that the compression mapping behaves ideally, that the tree is
indifferentiable from a random oracle, or that no structural attack reaches a
lower cost.

### 11.2 ARX Core and Round Count

FCH deliberately reuses the BLAKE2b G function, IV, rotation distances
`32, 24, 16, 63`, and ten-entry message-permutation cycle. These values are
publicly specified and avoid introducing a new set of unexplained random-looking
constants. They also provide a fixed analysis target built from modular
addition, XOR, and rotation.

FCH does **not** reuse the BLAKE2b compression mode. Its state initialization,
tweak positions, domains, flags, record encoding, feed-forward context, round
count, and tree construction differ. Reusing components therefore does not
transfer BLAKE2b's security analysis or claims to FCH.

The production round count is 16:

- the deterministic diffusion harness treats 8 rounds as the conservative
  reduced-round reference
- 16 leaves an operational gap of 8 additional rounds
- each round still processes all message words, while rounds 10–15 continue
  the public ten-permutation cycle with permutations 0–5
- reduced-round entry points are excluded from production builds

The choice of 8 as a reference is deliberately more conservative than using
the earliest round count that passes a statistical diffusion threshold.
Doubling that reference to 16 is an engineering margin, not a derivation from
a differential or linear proof. The full count must be reconsidered if future
cryptanalysis reaches additional rounds.

Before the rounds, counter, actual record length, state width, domain, and
flags enter separate working words. After the rounds, both halves of the
working state feed forward into every chaining word. This binds compression
calls to their record context and prevents the output from being a bare
permutation state. XOR tweak injection and this feed-forward rule still
require dedicated related-tweak and chosen-state analysis.

### 11.3 Constants, Tags, Domains, and Flags

FCH constants are transparent and non-secret:

- the eight IV words are the public BLAKE2b IV
- core identifiers such as `FCH-ARX1` and `CORE-V01` are readable constants
- record tags such as `FCHLEAF1`, `FCHNODE1`, and `FCHCHLD1` identify a
  single encoded record type
- root leaf, internal leaf, root node, internal node, split, FCH-256 output,
  and FCH-512 output use distinct domains
- record flags use distinct single bits, while the final-record marker uses
  the high bit
- numeric structural fields use fixed-width little-endian encoding

Tags, domains, and flags intentionally overlap in purpose. The redundancy
makes accidental cross-type reuse harder and keeps the encoded context
auditable; it is not a substitute for collision resistance in the core.
Constants provide no entropy and must never be treated as keys.

Tree-encoding and split-derivation version fields are included in compressed
headers. A change to state width, round count, schedule, tags, domains,
fan-out, weights, depth cap, block threshold, or field layout changes the
algorithm and requires a version change plus new fixed vectors. Parameters
must not be silently tuned only to improve a statistical test score.

### 11.4 Padding and Tree Parameters

| Parameter | Selection | Rationale and limitation |
| --------- | --------- | ------------------------ |
| Padding | `0x80`, zero fill, 64-bit little-endian bit length | Makes the supported message-to-padded-input mapping unambiguous and binds the original length; the checked API is limited to at most `2^61 - 1` input bytes and may have a lower platform limit |
| Leaf threshold | 64 bytes | Keeps short padded messages in one leaf, bounds recursive overhead, and tests the base transition at 63/64/65 bytes; 64 is an engineering choice, not a proven cryptographic optimum |
| Fan-out | 2 through 6 children | Two guarantees recursive progress, while six bounds child-state allocation, header work, and tree-width variability; the range remains public and attacker-searchable |
| Split weights | 128 through 255 | A seven-bit draw with the high bit set gives 128 equiprobable nominal weights and keeps the maximum/minimum nominal ratio below 2 |
| Split material | 512 bits from the complete node input | Makes every parent byte affect the requested child count and weights and removes cheap local control of the former accumulator; it does not prevent an attacker from searching whole messages for a desired split |
| Maximum depth | 16 | Bounds recursion, stack use, and repeated reads to at most 16 tree levels; this value is unrelated to the 16 compression rounds and may create large depth-cap leaves that still require analysis |

Child lengths are derived from bounded weights but are then corrected to be
positive, contiguous, non-overlapping, and exactly covering the parent. Node
and child encodings bind the resulting count, order, offsets, lengths, parent
length, and depth. This makes the representation canonical for one selected
tree, but security still depends on the core preventing two encoded records
from reaching the same state.

The complete-input split derivation trades stronger structural binding for
cost: input can be read once per realized tree level. The depth cap bounds the
multiplier but does not remove CPU, I/O, or temporary-storage exhaustion risk.

### 11.5 What the Security Argument Depends On

Meeting the targets in Section 1 requires all of the following to hold:

1. the 16-round compression core resists collision, preimage, differential,
   linear, rotational, fixed-point, and related-tweak attacks at the required
   costs
2. the canonical record encoding has no ambiguous parse or cross-domain alias
3. combining valid child states does not enable multicollision, herding,
   expandable-message, grafting, or long-message second-preimage shortcuts
4. content-derived tree shapes do not introduce a cheaper attack through
   split steering or depth-cap behavior
5. output finalization preserves the intended separation and output strength
6. the implementation enforces the specification without memory, integer,
   streaming-state, or platform-dependent faults

The bundled tests provide regression evidence for limited instances of these
properties. They do not compose into a security proof. Until independent
analysis addresses the full list, FCH remains an unvalidated cryptographic
hash candidate and the deployment warning remains in force.

### 11.6 External References

- [RFC 7693: The BLAKE2 Cryptographic Hash and Message Authentication Code](https://www.rfc-editor.org/rfc/rfc7693.html) — source of the reused BLAKE2b IV, G structure, rotation distances, block width, and message schedule
- [NIST SP 800-185](https://csrc.nist.gov/pubs/sp/800/185/final) — background on explicit encoding and parallel/tuple hash domain separation; FCH does not implement or claim conformance to these functions
