# Fractal Crypt-Hash (FCH)

Fractal Crypt-Hash (FCH)는 기존의 라운드 기반 압축 구조 대신
**프랙탈(자기유사) 재귀 구조**를 기반으로 확산을 유도하는 실험적 해시 함수입니다.

---

## 보안 주의사항 (필독)

FCH는 **연구/실험 목적**의 해시 설계 및 레퍼런스 구현입니다.
광범위한 공개 크립토분석이나 독립 보안 리뷰를 거치지 않았습니다.

따라서 아래와 같은 **실전 보안 목적**에는 사용하지 마세요(예시):

- 인증/서명/토큰/MAC 등
- 비밀번호 해싱/키 파생
- 공격자가 존재하는 환경에서의 무결성 검증
- 해시가 깨졌을 때 피해가 발생하는 모든 용도

이 저장소는 학습, 실험, 벤치마크, 토론을 위한 목적을 권장합니다.

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
- 노드 전체 입력에서 유도되는 가변 n-way(2–6) 분할
- 순서 의존(order-dependent) 트리 재결합
- 루트·내부 노드·리프·자식 상태의 명시적 영역 분리
- 전체 폭 리프 상태와 XOR / ADD / ROTATE / S-box 혼합
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

int ok256 = fch_hash_256_checked(data, len, out256);
int ok512 = fch_hash_512_checked(data, len, out512);
```

checked 함수는 성공 시 `1`, 잘못된 입력·메모리 할당 실패·지원하지 않는
입력 길이에서는 `0`을 반환합니다. 기존 `void` 함수는 호환용으로 유지됩니다.

### 버퍼드 스트리밍 API

FCH는 **버퍼드(buffered)** 스트리밍 API도 제공합니다. 즉, 데이터를 메모리에 누적한 뒤 `final` 단계에서 원샷 해싱을 수행합니다.

```c
#include "fch_stream.h"

fch256_ctx ctx;
fch256_init(&ctx);
fch256_update(&ctx, chunk1, chunk1_len);
fch256_update(&ctx, chunk2, chunk2_len);
int ok = fch256_final_checked(&ctx, out256);
fch256_free(&ctx);
```

### CLI

CLI 빌드:

```sh
cd build
make all
```

파일 해시:

```sh
./fch -256 path/to/file
./fch -512 path/to/file
```

표준입력(stdin) 해시:

```sh
cat path/to/file | ./fch -256
```

## 테스트

구현에는 다음 테스트가 포함됩니다:

- Avalanche 통계 테스트(길이/비트 확산)
- 결정성 테스트(동일 입력 → 동일 출력)
- 경계값/예외 입력 테스트
- 구조 불변성 테스트(split coverage)
- 이식성과 영역 분리 테스트
- 분할 설정 민감도 테스트

이 레퍼런스 구현은 결정성, 경계 조건, 구조적 불변성,
그리고 통계적 확산 동작에 대한 테스트를 포함합니다.

테스트 프로그램:

- `tests/test_avalanche.c`
- `tests/test_consistency.c`
- `tests/test_boundaries.c`
- `tests/test_invariants.c`
- `tests/test_portability.c`
- `tests/test_vectors.c`
- `tests/test_split_sensitivity.c`

빌드/실행:

```sh
cd build
make check
```

CI에서는 GCC와 Clang으로 전체 테스트를 실행하며,
AddressSanitizer와 UndefinedBehaviorSanitizer 검사도 수행합니다.

Windows에서 `make`가 없다면 `gcc`로 직접 컴파일할 수 있습니다:

```sh
gcc -Wall -Wextra -O2 -Iinclude tests/test_consistency.c src/*.c -o build/test_consistency.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_boundaries.c  src/*.c -o build/test_boundaries.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_invariants.c  src/*.c -o build/test_invariants.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_avalanche.c   src/*.c -o build/test_avalanche.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_vectors.c     src/*.c -o build/test_vectors.exe

gcc -Wall -Wextra -O2 -Iinclude tools/fch.c              src/*.c -o build/fch.exe

build\\fch.exe -256 README.ko.md
```
