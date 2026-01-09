# Fractal Crypt-Hash Specification

## 1. Input Padding

Given input message M of length L bytes:

1. Append byte 0x80
2. Append zero bytes
3. Append 64-bit little-endian value of (L × 8)
4. Ensure total length ≥ FCH_MIN_BLOCK_SIZE

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
- Determined by data pattern and recursion depth
- Split sizes weighted by input byte values
- Blocks cover the entire input without overlap

---

## 4. Leaf Compression

Leaf nodes apply:

- XOR
- ADD
- ROTATE
- 8×8 S-box substitution

After processing:

- State is folded to half size
- Produces reduced output for parent nodes

---

## 5. Tree Combine & Recompression

Internal nodes:

- Merge child states sequentially (order-dependent)
- Apply arithmetic mixing and rotation
- Apply S-box substitution
- Output restored to full state width

---

## 6. Output

- Root state serialized as little-endian bytes
- FCH-256: 32 bytes
- FCH-512: 64 bytes
