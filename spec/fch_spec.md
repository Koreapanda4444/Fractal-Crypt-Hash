# Fractal Crypt-Hash Specification

## 0. Scope & Non-Goals

This specification describes the **deterministic behavior** of the FCH reference design.
It does **not** claim cryptographic security (collision resistance / preimage resistance / etc.),
and it should not be used as a basis to deploy FCH in production security contexts.

FCH is intended for research, experimentation, benchmarking, and discussion.

## 1. Input Padding

Given input message M of length L bytes:

1. Append byte 0x80
2. Append zero bytes
3. Append 64-bit little-endian value of (L × 8)
4. Ensure total length ≥ FCH_MIN_BLOCK_SIZE

Inputs whose byte length cannot be represented safely by the 64-bit bit-length
field are rejected by the checked API.

---

## 2. Fractal Processing

Processing starts at depth = 0.

At each node:

- If depth ≥ MAX_DEPTH or length ≤ MIN_BLOCK_SIZE:
  → Leaf compression
- Else:
  → Variable n-way split and recursive processing

---

## 3. Variable n-Way Split

- n ∈ [2, 6]
- A 64-bit split seed incorporates node length, depth, and every input byte
- n and each split weight are derived from the mixed seed
- Blocks cover the entire input without overlap

---

## 4. Leaf Compression

Leaf nodes apply:

- XOR
- ADD
- ROTATE
- 8×8 S-box substitution
- Four cross-lane finalization passes

All 64-bit rotations reduce the rotation count modulo 64. S-box substitution
operates on bytes from least-significant to most-significant order, independent
of host byte order.

After processing:

- Root leaves and internal leaves use different domain constants
- Length, depth, and state width are mixed into the state
- The full state width is retained for parent nodes

---

## 5. Tree Combine & Recompression

Internal nodes:

- Merge child states sequentially (order-dependent)
- Mix node length, depth, child count, and state width
- Mix each child's index, offset, and length
- Apply arithmetic mixing and rotation
- Apply S-box substitution
- Retain the full state width

---

## 6. Output

- Root state serialized as little-endian bytes
- FCH-256: 32 bytes
- FCH-512: 64 bytes

Allocation or validation failure does not produce an alternative tree. The
checked API reports failure and clears the output buffer.
