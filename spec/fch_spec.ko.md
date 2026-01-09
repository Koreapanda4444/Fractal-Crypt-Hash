# Fractal Crypt-Hash 사양(Specification)

## 1. 입력 패딩(Input Padding)

길이가 L 바이트인 입력 메시지 M에 대해:

1. 바이트 0x80을 추가
2. 0x00 바이트들을 추가
3. (L × 8)의 64-bit little-endian 값을 추가
4. 전체 길이가 FCH_MIN_BLOCK_SIZE 이상이 되도록 보장

---

## 2. 프랙탈 처리(Fractal Processing)

처리는 depth = 0에서 시작합니다.

각 노드에서:

- depth ≥ MAX_DEPTH 또는 length ≤ MIN_BLOCK_SIZE 이면:
  → 리프(leaf) 압축
- 그 외의 경우:
  → 가변 n-way 분할 후 재귀 처리

---

## 3. 가변 n-Way 분할(Variable n-Way Split)

- n ∈ [2, 6]
- 입력 패턴과 재귀 깊이에 의해 결정
- 입력 바이트 값에 의해 분할 크기 가중치가 결정
- 블록은 전체 입력을 겹치지 않게 완전히 덮음

---

## 4. 리프 압축(Leaf Compression)

리프 노드는 다음을 적용합니다:

- XOR
- ADD
- ROTATE
- 8×8 S-box 치환

처리 후:

- 상태(State)는 절반 크기로 fold
- 상위 노드에서 사용할 축소 출력 생성

---

## 5. 트리 결합 및 재압축(Tree Combine & Recompression)

내부 노드는:

- 자식 상태를 순차적으로 병합(순서 의존)
- 산술 혼합 및 회전 적용
- S-box 치환 적용
- 출력 상태 폭을 전체(state_words)로 복원

---

## 6. 출력(Output)

- 루트 상태를 little-endian 바이트로 직렬화
- FCH-256: 32 bytes
- FCH-512: 64 bytes
