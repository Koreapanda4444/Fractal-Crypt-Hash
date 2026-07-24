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

At each node:

- If depth ≥ MAX_DEPTH or length ≤ MIN_BLOCK_SIZE:
  → Leaf compression
- Else:
  → Variable n-way split and recursive processing

---

## 4. Variable n-Way Split

- n ∈ [2, 6]
- A 64-bit split seed incorporates node length, depth, and every input byte
- n and each split weight are derived from the mixed seed
- Blocks cover the entire input without overlap

---

## 5. Leaf Compression

FCH-256 and FCH-512 both use a 512-bit internal tree state consisting of
eight 64-bit words. A leaf initializes this state with separate domains for
root leaves and internal leaves.

The leaf first compresses a parameter block containing its length, depth,
state width, minimum block size, and round count. It then reads leaf data in
128-byte blocks and compresses each block. The unused portion of the final
partial block is zero-filled, while the actual block length and cumulative
byte count are injected as separate tweaks. The last data block carries the
final flag.

### 5.1 ARX Compression Core

Each compression uses a 16-word working state. Its first eight 64-bit words
are initialized from the chaining state and its last eight words from a fixed
IV. The counter, actual block length, state width, domain, and flags are XORed
into the working state before 12 rounds. Each round applies four column G
functions followed by four diagonal G functions so every working word mixes.

G uses only 64-bit modular addition, XOR, and right rotation:

1. `a = a + b + x`, `d = ROTR64(d XOR a, 32)`
2. `c = c + d`, `b = ROTR64(b XOR c, 24)`
3. `a = a + b + y`, `d = ROTR64(d XOR a, 16)`
4. `c = c + d`, `b = ROTR64(b XOR c, 63)`

After 12 rounds, each chaining word is updated as
`h[i] XOR work[i] XOR work[i + 8]`.
The G structure and rotation distances follow the 64-bit ARX structure used
by BLAKE2b, but FCH has its own initialization, tweak layout, message mode,
and tree construction. FCH therefore does not inherit BLAKE2b's security
claims.

---

## 6. Tree Combine & Recompression

Internal nodes:

- use separate domains for the root node and other internal nodes
- first compress a parameter block with node length, depth, child count, and
  state width
- serialize each 512-bit child state together with child index, offset,
  length, and node metadata into one 128-byte block
- feed child blocks to the 12-round ARX core in order
- set the final flag on the last child block
- retain the full 512-bit state after combination

---

## 7. Output

- Compress the root state once more with the output size and an output domain
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
