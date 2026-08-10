# Fractal Crypt-Hash Specification

## 0. Purpose and status

This document defines the byte-for-byte behavior of Fractal Crypt-Hash. FCH is
a deterministic, public, non-keyed hash function developed for cryptographic
research. The security levels in this document are design goals used to guide
analysis. Independent public analysis of the construction is still in progress.

The Korean version is available in [fch_spec.ko.md](fch_spec.ko.md).

## 1. Security model

### 1.1 Attacker model

The algorithm, constants, messages, and digests are public. An attacker may
choose arbitrary messages and evaluate FCH without restriction. The targets in
this specification use the classical computation model; no separate quantum
security level is defined here.

### 1.2 Target strength

| Variant | Output | Collision | Preimage | Second preimage |
| ------- | ------ | --------- | -------- | --------------- |
| FCH-256 | 256 bits | 2^128 | 2^256 | 2^256 |
| FCH-512 | 512 bits | 2^256 | 2^512 | 2^512 |

These are the generic attack costs expected from ideal hashes of the same
output sizes. They are evaluation targets for FCH rather than conclusions
drawn from the current test suite.

### 1.3 Structural goals

The tree mode is designed to:

- distinguish roots, internal nodes, leaves, child states, split records, and
  output variants;
- bind node type, depth, length, child count, child position, offset, and
  length to each internal state;
- give every valid tree a canonical encoded form;
- resist tree-specific multicollision, herding, grafting, and long-message
  second-preimage shortcuts; and
- produce identical results through the one-shot and streaming APIs.

### 1.4 Round policy

Normal hashing always uses 16 rounds. Analysis builds expose reduced-round
compression and measure diffusion at 4, 8, 12, and 16 rounds. Eight rounds are
used as the current reduced-round reference, leaving an eight-round gap to the
full core.

The bundled searches cover selected differences at 1, 2, 4, 8, and 16 rounds,
plus linear correlation, fixed points, two-cycles, collisions, and near
collisions within bounded samples. Dedicated differential, rotational,
related-tweak, and structural analysis remains part of the research work.

## 2. Fixed parameters

| Parameter | Value |
| --------- | ----- |
| Word width | 64 bits |
| Internal state | 8 words / 512 bits |
| Compression input | 16 words / 128 bytes |
| Full rounds | 16 |
| Reduced-round reference | 8 rounds |
| Leaf threshold | 64 bytes |
| Maximum depth | 16 |
| Fan-out | 2–6 children |
| Split weights | 128–255 |
| Tree encoding version | 1 |
| Split derivation version | 1 |

FCH-256 and FCH-512 use the same 512-bit state throughout the tree. They
separate only at output finalization.

## 3. Input padding

For a message `M` of `L` bytes:

1. append `0x80`;
2. append zero bytes;
3. append `L × 8` as an unsigned 64-bit little-endian integer; and
4. extend the result to at least 64 bytes.

The checked APIs reject lengths that cannot safely be represented by the
64-bit bit-length field or by the host `size_t` calculations. The largest
format-level input is `2^61 - 1` bytes, although a platform may impose a lower
limit.

## 4. Recursive processing

Processing starts at depth 0 with an eight-word state width.

- A node is a leaf when its length is at most 64 bytes or its depth is 16.
- Every other node is split into two through six contiguous children.
- Children are processed recursively from left to right.
- Their 512-bit states are recompressed into the parent state.

Negative depths, unsupported state widths, gaps, overlaps, empty internal
children, and out-of-range offsets are invalid.

## 5. Split derivation

Each internal node derives 512 bits of split material from its complete input.
The derivation uses the normal ARX core in the dedicated `FCHSPLT1` domain.

### 5.1 Header

The first compression input is a 128-byte `FCHSPH01` record. Fixed-width
little-endian fields bind:

- split derivation version;
- node length and depth;
- state width;
- minimum and maximum fan-out;
- split-weight range;
- leaf threshold and maximum depth;
- compression block size and round count; and
- tree encoding version.

### 5.2 Input records

The complete node input is absorbed in `FCHSPD01` records. Each record contains
the eight-byte tag followed by at most 120 bytes from the node. The block-length
tweak carries the actual record length, the counter carries the cumulative
input length, and the last record carries the final flag.

### 5.3 Output draw

A fixed 128-byte `FCHSPO01` record binds the configuration, number of input
records, node length, depth, and draw counter.

Let:

```text
r = N_MAX - N_MIN + 1
t = 2^64 mod r
```

`material[0]` is accepted only when it is at least `t`. Otherwise another
output draw is compressed with an incremented draw counter. The child count is:

```text
n = N_MIN + (material[0] mod r)
```

Internal nodes shorter than `2 × MIN_BLOCK_SIZE` use `N_MIN`. This keeps the
short transition canonical. Rejection sampling removes modulo bias.

Each child weight is derived as:

```text
weight[i] = 128 + (material[i + 1] AND 0x7f)
```

The resulting weights lie between 128 and 255. Child lengths are proportional
to those weights, then corrected so every child is non-empty, contiguous, and
the children cover the parent exactly. Any rounding remainder is assigned to
the final child.

The partition function is public. Searching messages for a chosen fan-out is
therefore possible; the purpose of this derivation is to make every byte of the
parent participate in the split and to avoid cheap local control of the former
64-bit accumulator.

## 6. Record encoding and compression

### 6.1 Canonical records

- Type tags are exactly eight ASCII bytes.
- Numeric fields are unsigned 64-bit little-endian values.
- Fixed records are 128 bytes and unused bytes are zero.
- Tagged data records are 8–128 bytes and use their actual length.
- Each record type has a distinct tag and compression flag.
- Root and non-root leaf or node contexts use distinct domains.
- Format changes require a new encoding version.

The principal tags are:

| Tag | Record |
| --- | ------ |
| `FCHLEAF1` | Leaf header |
| `FCHLDAT1` | Leaf data |
| `FCHNODE1` | Internal-node header |
| `FCHCHLD1` | Child state |
| `FCHOUT01` | Output finalization |
| `FCHSPH01` | Split header |
| `FCHSPD01` | Split data |
| `FCHSPO01` | Split output draw |

### 6.2 ARX core

Compression uses a 16-word working state. Words 0–7 are initialized from the
current chaining state and words 8–15 from the fixed IV. Before the rounds,
the counter, actual record length and state width, domain, and flags are placed
in dedicated tweak positions.

Each round applies four column G functions followed by four diagonal G
functions. G operates on 64-bit words with modular addition, XOR, and right
rotation:

1. `a = a + b + x`; `d = ROTR64(d XOR a, 32)`
2. `c = c + d`; `b = ROTR64(b XOR c, 24)`
3. `a = a + b + y`; `d = ROTR64(d XOR a, 16)`
4. `c = c + d`; `b = ROTR64(b XOR c, 63)`

The message schedule contains ten fixed permutations. Rounds 0–9 use them in
order; rounds 10–15 repeat permutations 0–5. After the final round, every
chaining word is updated as:

```text
h[i] = h[i] XOR work[i] XOR work[i + 8]
```

The G function, IV, rotation distances, and message permutations are taken
from BLAKE2b components. FCH defines different initialization, tweak placement,
record handling, domains, round count, and tree mode.

### 6.3 Leaf compression

A leaf initializes its 512-bit state in either the root-leaf or internal-leaf
domain. It first compresses a 128-byte `FCHLEAF1` header that binds the encoding
version, domain, leaf length, depth, state width, tree limits, core parameters,
and split parameters.

Leaf bytes are then absorbed in `FCHLDAT1` records. Each record holds up to 120
message bytes after its tag. The counter records cumulative message bytes, the
block-length tweak records the actual tagged length, and the last record uses
the final flag. An empty leaf still processes one tag-only final record.

## 7. Tree combination

An internal node initializes a fresh 512-bit state in the root-node or
internal-node domain. It first compresses a `FCHNODE1` header that binds the
parent length, depth, child count, state width, and current tree parameters.

Each child is then encoded as one 128-byte `FCHCHLD1` record:

| Word | Field |
| ---- | ----- |
| 0 | `FCHCHLD1` |
| 1 | Encoding version |
| 2 | Parent length |
| 3 | Parent depth |
| 4 | Child count |
| 5 | Child index |
| 6 | Child offset |
| 7 | Child length |
| 8–15 | 512-bit child state |

Child records are compressed in index order. The last record carries the final
flag. The combined node retains all eight state words.

## 8. Output

The root state is compressed once more with a fixed `FCHOUT01` record. The
record binds the encoding version, requested output width, internal-state
width, block size, and round count.

- FCH-256 uses its own output domain and serializes the first four final state
  words as 32 little-endian bytes.
- FCH-512 uses a different output domain and serializes all eight final state
  words as 64 little-endian bytes.

Validation or allocation failure returns an error through the checked API and
clears the output buffer.

## 9. Streaming

Update calls append input to anonymous temporary storage. Finalization exposes
the stored bytes and virtual padding through a random-access reader, then runs
the same split, leaf, combination, and output steps as the one-shot API.

RAM use is bounded by fixed-size I/O buffers and recursive states. Temporary
storage grows with the message, and the content-derived tree may reread data at
several levels. Finalization closes the storage. An active context may not be
copied or accessed concurrently, and updates or repeated finalization after a
final call fail.

## 10. Analysis and implementation status

### 10.1 Automated analysis

The current suite includes:

| Area | Current coverage |
| ---- | ---------------- |
| Core diffusion | Single-bit and length changes; reduced-round comparisons |
| Differential and linear behavior | Bounded chosen differences, output-bit bias, low-weight outputs, and linear correlation |
| Cycles and collisions | Fixed points, two-cycles, exact collisions, and near collisions in bounded samples |
| Multicollisions | 4,096 leaf states and 4,096 derived node states, including 20-bit prefix-bucket distribution |
| Second preimages | 512 related candidates for a 16 KiB target |
| Tree contexts | Binary, flat, skewed, reordered, boundary-shifted, grafted, and depth-shifted combinations |
| Long messages | Fifteen 256 KiB variants covering extension, truncation, reordering, copying, rotation, boundary edits, and low-entropy inputs |

The small prefix width in the multicollision test is chosen so ordinary
birthday matches appear during CI; the test looks for abnormal concentration
and exact 512-bit equality. These searches are regression tools and define a
reproducible starting point for deeper cryptanalysis.

### 10.2 Resource behavior

Split derivation reads the complete input of every visited node. For a message
of length `L` and realized depth `d`, input-reading work is `O(L × d)`, with
`d ≤ 16`. One-shot hashing also allocates `O(L)` padding storage. Streaming
uses `O(L)` temporary storage while keeping application RAM bounded.

### 10.3 Reference implementation checks

The checked APIs validate pointers and lengths before reading input. Reader and
allocation failures propagate to the caller without returning a partial state.
Failed streaming contexts remain failed, finalization closes their storage,
and repeated finalization clears the destination before returning failure.

The shared deterministic/libFuzzer harness compares one-shot and streaming
results, validates split coverage, and exercises lifecycle and cleanup paths.
An 8 MiB message is processed with two chunk layouts. CI builds on x86-64
Linux, macOS, and Windows; Linux jobs also use AddressSanitizer and
UndefinedBehaviorSanitizer.

## 11. Design rationale

### 11.1 Widths

The 64-bit word size matches the selected ARX G function and maps directly to
portable `uint64_t` operations. A 512-bit internal state keeps the generic
birthday scale at `2^256`, matching the collision target of FCH-512. Keeping
the same state width for FCH-256 avoids a narrower path inside the tree.

The 128-byte compression input provides sixteen message words. Every round
uses all sixteen once across eight G calls. Output-domain separation ensures
that the 256-bit result is not defined as the raw prefix of the 512-bit result.

### 11.2 Rounds and constants

FCH uses public BLAKE2b-derived components rather than a new set of unexplained
constants. Sixteen rounds were selected as an engineering margin above the
eight-round reduced analysis reference. Future attack results, rather than
statistical scores alone, determine whether that count should change.

Tags, domains, and flags overlap deliberately: tags make records readable,
domains separate state initialization, and flags distinguish the role and end
of a compression sequence. All are public constants and contain no secret
material.

### 11.3 Tree parameters

The 64-byte leaf threshold keeps short padded messages in one leaf and limits
recursion overhead. Fan-out 2 guarantees progress; fan-out 6 bounds temporary
child state and node-width variation. Weights from 128 to 255 keep the nominal
largest-to-smallest ratio below two. Maximum depth 16 bounds recursion and
repeated reads; it is unrelated to the 16 compression rounds.

The security case for the complete design depends on the core, canonical
encoding, tree combination, content-derived topology, output finalization, and
implementation all behaving as specified. Those are the main subjects of the
ongoing analysis.

### 11.4 References

- [RFC 7693: The BLAKE2 Cryptographic Hash and Message Authentication Code](https://www.rfc-editor.org/rfc/rfc7693.html)
- [NIST SP 800-185: SHA-3 Derived Functions](https://csrc.nist.gov/pubs/sp/800/185/final)
