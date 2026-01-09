# Fractal Crypt-Hash (FCH)

Fractal Crypt-Hash (FCH)는 기존의 라운드 기반 압축 구조 대신
**프랙탈(자기유사) 재귀 구조**를 기반으로 확산을 유도하는 실험적 해시 함수입니다.

---

## 개요

일반적인 해시 함수가 반복 라운드로 확산을 얻는 것과 달리,
FCH는 **재귀적 프랙탈 분해**를 통해 확산을 얻습니다.

입력의 작은 변화는 다음 경로로 전파됩니다:

- 리프(leaf) 노드에서의 국소 확산
- 가변 n-way 분할을 통한 재귀 전파
- 루트에서의 재압축(recompression)을 통한 전역 확산

---

## 특징

- 프랙탈 재귀 해시 구조
- 패턴 기반 가변 n-way(2–6) 분할
- 순서 의존(order-dependent) 트리 재결합
- 리프 레벨의 최소 비선형성(XOR / ADD / ROTATE / S-box)
- 내부 노드마다 재압축 수행
- 결정적(deterministic) 비키(non-keyed) 해시 함수

---

## 지원 변형

| 변형 | 출력 크기 | 내부 상태 |
| ---- | --------- | --------- |
| FCH-256 | 256 bits | 4 × uint64 |
| FCH-512 | 512 bits | 8 × uint64 |

---

## 사용법

```c
uint8_t out256[32];
uint8_t out512[64];

fch_hash_256(data, len, out256);
fch_hash_512(data, len, out512);
```

## 테스트

구현에는 다음 테스트가 포함됩니다:

- Avalanche 통계 테스트(길이/비트 확산)
- 결정성 테스트(동일 입력 → 동일 출력)
- 경계값/예외 입력 테스트
- 구조 불변성 테스트(split coverage)

이 레퍼런스 구현은 결정성, 경계 조건, 구조적 불변성,
그리고 통계적 확산 동작에 대한 테스트를 포함합니다.

테스트 프로그램:

- `tests/test_avalanche.c`
- `tests/test_consistency.c`
- `tests/test_boundaries.c`
- `tests/test_invariants.c`

빌드/실행:

```sh
cd build
make test
./test_consistency
./test_boundaries
./test_invariants
./test_avalanche
```

Windows에서 `make`가 없다면 `gcc`로 직접 컴파일할 수 있습니다:

```sh
gcc -Wall -Wextra -O2 -Iinclude tests/test_consistency.c src/*.c -o build/test_consistency.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_boundaries.c  src/*.c -o build/test_boundaries.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_invariants.c  src/*.c -o build/test_invariants.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_avalanche.c   src/*.c -o build/test_avalanche.exe
```
