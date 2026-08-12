# Fractal Crypt-Hash Specification

## 0. Scope and status

This document defines the current byte-for-byte behavior of Fractal Crypt-Hash
(FCH). FCH is a deterministic, public, non-keyed hash function developed for
cryptographic research. The security levels below are design targets, not
claims established by the bundled tests. Independent analysis is still in
progress.

This revision makes tree encoding version 2 normative. It replaces the former
content-dependent tree and intentionally changes every digest produced by tree
encoding version 1. There is no mixed-version or fallback mode.

The Korean version is available in [fch_spec.ko.md](fch_spec.ko.md).

## 1. Security model

### 1.1 Attacker model

The algorithm, constants, messages, intermediate formats, and digests are
public. An attacker may choose arbitrary messages and evaluate FCH without
restriction. The targets below use the classical computation model; this
document does not define separate quantum security levels.

### 1.2 Target strength

| Variant | Output | Collision | Preimage | Second preimage |
| ------- | ------ | --------- | -------- | --------------- |
| FCH-256 | 256 bits | 2^128 | 2^256 | 2^256 |
| FCH-512 | 512 bits | 2^256 | 2^512 | 2^512 |

These are the generic costs expected from ideal hashes of the same output
sizes. Statistical diffusion and bounded searches are useful regression
evidence, but they do not establish these bounds.

### 1.3 Structural goals

The tree mode is designed to:

- give each padded length one canonical tree;
- prevent message bytes from selecting arity or boundaries;
- bind node role, range, position, level, and child order to the state;
- separate leaves, nodes, output variants, and finalization;
- keep completed power-of-two prefix subtrees stable when a suffix is added;
- reject gaps, overlaps, reordering, grafts, and non-canonical groupings; and
- produce identical results through the one-shot and streaming APIs.

### 1.4 Round policy

Normal hashing always uses 16 rounds. Analysis builds expose reduced-round
compression at 1 through 16 rounds. Eight rounds are the current reduced-round
reference, leaving an eight-round gap to the full core. A change to the round
count is an incompatible algorithm change.

## 2. Fixed parameters

| Parameter | Value |
| --------- | ----- |
| Word width | 64 bits |
| Internal state | 8 words / 512 bits |
| Compression input | 16 words / 128 bytes |
| Full rounds | 16 |
| Reduced-round reference | 8 rounds |
| Minimum padded length | 64 bytes |
| Leaf span | 1,024 bytes |
| Internal-node arity | 2 |
| Leaf level | 0 |
| Tree encoding version | 2 |
| Padding format version | 1 |

FCH-256 and FCH-512 use the same 512-bit state throughout the tree. They
separate only during output finalization.

## 3. Integer and record conventions

- All additions on state words are modulo 2^64.
- All structural integers are unsigned 64-bit little-endian values.
- Type and domain labels are exactly eight ASCII bytes, interpreted as one
  little-endian 64-bit word.
- A fixed record is 128 bytes. Any unlisted word is zero.
- A leaf-data record has an actual length from 8 through 128 bytes; the unused
  part of its 128-byte compression buffer is zero.
- The compression block-length tweak carries the record's actual length.
- Format changes require a new tree encoding or padding version.

Checked implementations reject values that cannot be represented without
overflow in both the format and the host's `size_t` calculations.

## 4. Input padding

For a message `M` of `L` bytes:

1. append `0x80`;
2. append zero bytes as needed;
3. append `L × 8` as an unsigned 64-bit little-endian integer; and
4. extend the result to at least 64 bytes.

The padded length is therefore:

```text
P_len = max(64, L + 9)
```

Padding does not round the input to a leaf boundary. Only the final leaf may be
shorter than 1,024 bytes. The 64-bit length field limits the format-level
message length to at most `2^61 - 1` bytes; a platform may impose a lower
limit.

## 5. Canonical tree schedule

Let `P` be the padded byte string. Starting at byte offset zero, divide `P`
into consecutive 1,024-byte leaves. The final leaf contains the remaining
bytes and is never empty.

For leaf index `i`:

```text
level       = 0
first_leaf  = i
leaf_count  = 1
byte_offset = i × 1024
byte_length = min(1024, P_len - byte_offset)
```

Let `Tree(a, n)` denote the tree over `n` consecutive leaves beginning at leaf
`a`:

```text
Tree(a, 1) = Leaf(a)

Tree(a, n), n > 1:
    k = largest power of two strictly less than n
    left  = Tree(a, k)
    right = Tree(a + k, n - k)
    return Node(left, right)
```

This produces a left-complete binary tree. The level of a range containing
`n` leaves is:

```text
level = ceil(log2(n)) = bit_length(n - 1)
```

Every node carries this descriptor:

```text
level
first_leaf
leaf_count
byte_offset
byte_length
```

For an internal node, the left child must contain exactly the largest power of
two strictly below the parent's leaf count. The right child starts at both:

```text
left.first_leaf + left.leaf_count
left.byte_offset + left.byte_length
```

The child leaf ranges and byte ranges must be adjacent, non-empty, and equal
the parent range when joined. Child order is fixed. No message byte is read to
derive this schedule.

## 6. Compression core

### 6.1 Initialization

The fixed IV is:

| Index | Value |
| ----- | ----- |
| 0 | `6A09E667F3BCC908` |
| 1 | `BB67AE8584CAA73B` |
| 2 | `3C6EF372FE94F82B` |
| 3 | `A54FF53A5F1D36F1` |
| 4 | `510E527FADE682D1` |
| 5 | `9B05688C2B3E6C1F` |
| 6 | `1F83D9ABFB41BD6B` |
| 7 | `5BE0CD19137E2179` |

To initialize an eight-word chaining state `h`, copy the IV and apply:

```text
h[0] ^= domain
h[1] ^= 0x4643482D41525831
h[2] ^= 8 << 56
h[7] ^= 0x434F52452D563031
```

### 6.2 Compression input and tweaks

Load the 128-byte record as sixteen little-endian message words `m[0..15]`.
Set `v[0..7]` to the current chaining state and `v[8..15]` to the IV, then
apply:

```text
v[12] ^= counter
v[13] ^= record_length
v[13] ^= 8 << 56
v[14] ^= domain
v[15] ^= flags
```

### 6.3 G function and rounds

For state indices `a, b, c, d` and message words `x, y`, G is:

```text
v[a] = v[a] + v[b] + x
v[d] = ROTR64(v[d] XOR v[a], 32)
v[c] = v[c] + v[d]
v[b] = ROTR64(v[b] XOR v[c], 24)
v[a] = v[a] + v[b] + y
v[d] = ROTR64(v[d] XOR v[a], 16)
v[c] = v[c] + v[d]
v[b] = ROTR64(v[b] XOR v[c], 63)
```

Each round applies four column G calls followed by four diagonal G calls, using
the BLAKE2b message-word order. The 16 schedules are:

```text
 0:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
 1: 14 10  4  8  9 15 13  6  1 12  0  2 11  7  5  3
 2: 11  8 12  0  5  2 15 13 10 14  3  6  7  1  9  4
 3:  7  9  3  1 13 12 11 14  2  6  5 10  4  0 15  8
 4:  9  0  5  7  2  4 10 15 14  1 11 12  6  8  3 13
 5:  2 12  6 10  0 11  8  3  4 13  7  5 15 14  1  9
 6: 12  5  1 15 14 13  4 10  0  7  6  3  9  2  8 11
 7: 13 11  7 14 12  1  3  9  5  0 15  4  8  6  2 10
 8:  6 15 14  9 11  3  0  8 12  2 13  7  1  4 10  5
 9: 10  2  8  4  7  6  1  5 15 11  9 14  3 12 13  0
10:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
11: 14 10  4  8  9 15 13  6  1 12  0  2 11  7  5  3
12: 11  8 12  0  5  2 15 13 10 14  3  6  7  1  9  4
13:  7  9  3  1 13 12 11 14  2  6  5 10  4  0 15  8
14:  9  0  5  7  2  4 10 15 14  1 11 12  6  8  3 13
15:  2 12  6 10  0 11  8  3  4 13  7  5 15 14  1  9
```

After the final round:

```text
h[i] = h[i] XOR v[i] XOR v[i + 8], for i = 0..7
```

The G function, IV, rotations, and schedules come from BLAKE2b components. FCH
uses different initialization, tweaks, domains, record formats, round count,
and tree construction and does not inherit BLAKE2b's security claims.

## 7. Domains, tags, and flags

### 7.1 Labels

| Role | Eight-byte label |
| ---- | ---------------- |
| Leaf header | `FCHLEAF2` |
| Leaf data | `FCHLDAT2` |
| Node header | `FCHNODE2` |
| Node child | `FCHCHLD2` |
| Output finalization | `FCHOUT02` |
| Leaf domain | `FCHLDM02` |
| Node domain | `FCHNDM02` |
| FCH-256 output domain | `FCHO2562` |
| FCH-512 output domain | `FCHO5122` |

Leaves use the same leaf domain whether or not a leaf later becomes the root.
Nodes follow the same rule. Root status is committed only by `FCHOUT02`, which
keeps completed prefix subtrees reusable.

### 7.2 Flags

| Role | Value |
| ---- | ----- |
| Leaf header | `0x0000000000000001` |
| Leaf data | `0x0000000000000002` |
| Node header | `0x0000000000000004` |
| Node child | `0x0000000000000008` |
| Output | `0x0000000000000010` |
| Final | `0x8000000000000000` |

The final bit is ORed with the role flag on the last record in a compression
sequence.

## 8. Tree records

### 8.1 Leaf header

Initialize a new state in `FCHLDM02`. Compress a full 128-byte header with
counter zero and the leaf-header flag:

| Word | Field |
| ---- | ----- |
| 0 | `FCHLEAF2` |
| 1 | Tree encoding version, 2 |
| 2 | Leaf domain, `FCHLDM02` |
| 3 | Leaf index / first leaf |
| 4 | Absolute byte offset |
| 5 | Byte length |
| 6 | Leaf span, 1,024 |
| 7 | Internal state width in words, 8 |
| 8 | Compression block size in bytes, 128 |
| 9 | Round count, 16 |
| 10 | Padding format version, 1 |
| 11 | Tree arity, 2 |
| 12–15 | Zero |

### 8.2 Leaf data

Absorb the leaf in chunks of at most 120 bytes. Each record begins with
`FCHLDAT2`, followed by the payload. The record length is `8 + payload_length`.
The counter is the cumulative number of bytes absorbed within that leaf. Use
the leaf-data flag, adding the final flag to the last data record.

Every leaf is non-empty after padding, so at least one leaf-data record is
processed.

### 8.3 Node header

An internal node must have the exact two canonical children defined in section
5. Initialize a new state in `FCHNDM02`, then compress this full 128-byte header
with counter zero and the node-header flag:

| Word | Field |
| ---- | ----- |
| 0 | `FCHNODE2` |
| 1 | Tree encoding version, 2 |
| 2 | Node domain, `FCHNDM02` |
| 3 | Node level |
| 4 | First leaf |
| 5 | Leaf count |
| 6 | Absolute byte offset |
| 7 | Byte length |
| 8 | Child count, 2 |
| 9 | Leaf span, 1,024 |
| 10 | Internal state width in words, 8 |
| 11 | Compression block size in bytes, 128 |
| 12 | Round count, 16 |
| 13–15 | Zero |

### 8.4 Child records

Compress the two children in index order as full 128-byte `FCHCHLD2` records:

| Word | Field |
| ---- | ----- |
| 0 | `FCHCHLD2` |
| 1 | Tree encoding version, 2 |
| 2 | Child index, 0 or 1 |
| 3 | Child level |
| 4 | Child first leaf |
| 5 | Child leaf count |
| 6 | Child absolute byte offset |
| 7 | Child byte length |
| 8–15 | Complete 512-bit child state |

Child counters are one and two. Both use the node-child flag; the second also
uses the final flag.

## 9. Output finalization

After the canonical root is computed, compress one full `FCHOUT02` record into
the root state:

| Word | Field |
| ---- | ----- |
| 0 | `FCHOUT02` |
| 1 | Tree encoding version, 2 |
| 2 | Requested output width in bits, 256 or 512 |
| 3 | Internal state width in bits, 512 |
| 4 | Compression block size in bytes, 128 |
| 5 | Round count, 16 |
| 6 | Original message length in bytes |
| 7 | Padded length in bytes |
| 8 | Total leaf count |
| 9 | Root level |
| 10 | Root first leaf, 0 |
| 11 | Root leaf count |
| 12 | Root byte offset, 0 |
| 13 | Root byte length, equal to padded length |
| 14 | Leaf span, 1,024 |
| 15 | Padding format version, 1 |

Use the output domain matching the variant, the requested output length in
bytes as the counter, and `output | final` as the flags.

- FCH-256 serializes state words 0–3 as 32 little-endian bytes.
- FCH-512 serializes state words 0–7 as 64 little-endian bytes.

The separate domains mean FCH-256 is not defined as the raw prefix of FCH-512.

## 10. Complete hash procedure

1. Validate the input length and pointer.
2. Apply the padding in section 4.
3. Create the leaf descriptors and states in increasing offset order.
4. Combine them according to `Tree(0, leaf_count)`.
5. Validate that the root covers the complete padded input.
6. Apply output finalization for the requested variant.
7. Serialize the requested state words little-endian.

Any validation, reader, or allocation failure is an error. Checked APIs clear
the destination buffer instead of returning a partial digest.

## 11. Streaming behavior

The current C streaming API appends update data to an anonymous temporary file.
Finalization exposes the stored bytes and virtual padding through a reader,
then runs the same canonical tree and output procedure as the one-shot API.

Tree scheduling does not read message contents. During finalization, padded
bytes are requested once in increasing leaf order; the former repeated reads
for content-derived split selection no longer exist. Application RAM is bounded
by fixed buffers and `O(log leaf_count)` recursive states, while temporary
storage is `O(L)`.

This is not yet a fully online tree implementation: leaf states are not
finalized during update, and temporary-file support is still required. An
active context has one owner, must not be copied or used concurrently, and
rejects updates or repeated finalization after its final call.

## 12. Analysis and implementation status

The automated suite includes:

- C/Python cross-checks and fixed vectors;
- exact record-layout, domain, endian, and canonical-position checks;
- content- and legacy-depth-independent schedule tests;
- boundary, length, avalanche, and reduced-round diffusion tests;
- bounded differential, linear, rotational, related-tweak, fixed-point,
  two-cycle, collision, and near-collision searches;
- 4,096 leaf and derived-node multicollision samples;
- 512 related candidates for a 16 KiB second-preimage screen;
- canonical tree acceptance and reordered, skewed, flat, shifted, forged, and
  negative-depth rejection checks;
- fifteen 256 KiB long-message variants;
- one-shot/streaming equivalence, lifecycle, reader-failure, fuzz, sanitizer,
  and stress paths; and
- Linux, macOS, and Windows CI builds.

These are bounded regression checks, not a security proof. The design still
requires independent cryptanalysis, especially for the full compression core,
tree multicollision bounds, long-message second-preimage arguments, and the
interaction between the core and encoded tree mode.

## 13. Rationale and compatibility

The 512-bit internal state keeps the generic birthday scale at 2^256 and avoids
a narrower tree path for FCH-256. A 128-byte record supplies sixteen message
words, all used in every round. Sixteen rounds leave an engineering margin
above the eight-round analysis reference.

The 1,024-byte leaf span limits record overhead while keeping subtrees small
enough for incremental and parallel processing. Binary, left-complete grouping
gives every length a single layout and preserves completed power-of-two
prefixes. Absolute byte and leaf positions prevent a valid state from being
silently moved to another location.

Tree encoding version 2 is incompatible with version 1 by design. Changes to
tags, domains, flags, record fields, padding, leaf span, tree schedule, state
width, or round count require a deliberate format revision and new vectors.
Allocation, buffering, and scheduling optimizations may change without a new
format only when all digest outputs remain identical.

### References

- [RFC 7693: The BLAKE2 Cryptographic Hash and Message Authentication Code](https://www.rfc-editor.org/rfc/rfc7693.html)
- [NIST SP 800-185: SHA-3 Derived Functions](https://csrc.nist.gov/pubs/sp/800/185/final)
- [The BLAKE3 Hashing Framework](https://www.ietf.org/archive/id/draft-aumasson-blake3-00.html)
- [Sakura: A Flexible Coding for Tree Hashing](https://eprint.iacr.org/2013/231)
