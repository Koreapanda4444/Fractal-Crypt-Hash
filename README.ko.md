# Fractal Crypt-Hash (FCH)

Fractal Crypt-Hash (FCH)는 **16라운드 ARX 압축 코어**와
**프랙탈(자기유사) 재귀 구조**를 결합해 확산을 유도하며,
암호학적 해시 후보를 목표로 하는 실험적 해시 설계입니다.

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

## 보안 목표

FCH는 공개된 결정적 비키 해시 함수로서, 고전적 일반 공격에 대해
다음 수준의 저항성을 목표로 합니다.

| 변형 | 충돌 저항 | 원상 저항 | 제2원상 저항 |
| ---- | --------- | --------- | ------------- |
| FCH-256 | 2^128 | 2^256 | 2^256 |
| FCH-512 | 2^256 | 2^512 | 2^512 |

이 수치는 **설계 목표**이며 현재 구현이 달성했다는 주장이나 보안 보장이 아닙니다.
현재 FCH의 상태는 **미검증 암호학적 해시 후보**입니다. 통계적 확산과 avalanche
테스트 통과만으로 충돌·원상·제2원상 저항성이 입증되지는 않습니다.

더 강한 보안 표현을 사용하려면 안정된 사양, 구조 및 축소형에 대한 공격 분석,
명시적인 보안 여유, 목표보다 빠른 알려진 공격의 부재, 독립적인 공개 검토가
필요합니다. 세부 목표와 판정 기준은 `spec/fch_spec.ko.md`에 정의합니다.

현재 라운드 정책은 정식 16라운드와 축소형 비교 기준 8라운드 사이에
8라운드의 운용상 간격을 둡니다. 이는 보수적인 시험 기준일 뿐,
8라운드의 암호학적 보안 여유가 증명됐다는 뜻은 아닙니다.

---

## 개요

FCH는 고정 라운드 압축 코어의 국소 확산과
**재귀적 프랙탈 분해**의 구조적 확산을 함께 사용합니다.

입력의 작은 변화는 다음 경로로 전파됩니다:

- 리프(leaf) 노드에서의 국소 확산
- 가변 n-way 분할을 통한 재귀 전파
- 루트에서의 재압축(recompression)을 통한 전역 확산

---

## 특징

- 프랙탈 재귀 해시 구조
- 128바이트 블록 기반 16라운드 64비트 ARX 압축 코어
- 축소 라운드 비교 기준보다 8라운드 높은 운용상 간격
- 두 변형 모두 512비트 내부 트리 상태 사용
- 노드 전체 입력을 처리하는 영역 분리 512비트 분할 파생
- rejection sampling 기반 분기 수와 128~255 범위의 제한된 분할 가중치
- 순서 의존(order-dependent) 트리 재결합
- 루트·내부 노드·리프·자식·출력 변형의 명시적 영역 분리
- 버전·형식 태그·고정 필드를 갖는 정규 트리 인코딩
- 고정 회전값을 사용하는 ADD / ROTATE / XOR 혼합
- 각 리프 블록과 내부 노드 자식마다 재압축 수행
- 결정적(deterministic) 비키(non-keyed) 해시 함수

---

## 지원 변형

| 변형 | 출력 크기 | 내부 상태 |
| ---- | --------- | --------- |
| FCH-256 | 256 bits | 8 × uint64 |
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

### 제한 메모리 스트리밍 API

FCH 스트리밍 API는 입력 청크를 익명 임시 파일에 기록하고, `final` 단계에서
고정 크기 버퍼로 다시 읽습니다. 따라서 전체 입력 크기에 비례해 RAM 사용량이
증가하지 않으며 원샷 API와 같은 해시를 생성합니다. 임시 파일을 사용할 수
있어야 하고 원샷 API보다 느릴 수 있습니다.

```c
#include "fch_stream.h"

fch256_ctx ctx;
fch256_init(&ctx);
fch256_update(&ctx, chunk1, chunk1_len);
fch256_update(&ctx, chunk2, chunk2_len);
int ok = fch256_final_checked(&ctx, out256);
fch256_free(&ctx);
```

final 이후의 추가 update와 final 재호출은 거부됩니다.
CLI의 파일 및 표준입력 처리도 같은 제한 메모리 경로를 사용합니다.

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
- 4·8·12·16라운드 축소형 코어 확산 비교
- 결정성 테스트(동일 입력 → 동일 출력)
- 경계값/예외 입력 테스트
- 구조 불변성 테스트(split coverage)
- 분할 파생 확산·리더 위치 독립성·균형 제한 테스트
- 이식성과 영역 분리 테스트
- 분할 설정 민감도 테스트
- 제한된 차분 및 축소 라운드 저중량 탐색
- 선형 상관·고정점·2주기·근접 충돌 탐색

이 레퍼런스 구현은 결정성, 경계 조건, 구조적 불변성,
그리고 통계적 확산 동작에 대한 테스트를 포함합니다.

암호분석 테스트는 CI에서 실행할 수 있도록 결정적이고 제한된 범위로
구성됩니다. 통과는 시험한 단순 구별자와 탐색에서 문제가 발견되지 않았다는
뜻일 뿐, 충돌·원상·전체 설계의 안전성을 입증하지 않습니다.

테스트 프로그램:

- `tests/test_avalanche.c`
- `tests/test_consistency.c`
- `tests/test_boundaries.c`
- `tests/test_invariants.c`
- `tests/test_portability.c`
- `tests/test_vectors.c`
- `tests/test_split_sensitivity.c`
- `tests/test_cryptanalysis.c`

빌드/실행:

```sh
cd build
make check
make check-extended
```

CI에서는 GCC와 Clang으로 전체 테스트를 실행하며,
AddressSanitizer와 UndefinedBehaviorSanitizer 검사도 수행합니다.

Windows에서 `make`가 없다면 `gcc`로 직접 컴파일할 수 있습니다:

```sh
gcc -Wall -Wextra -O2 -Iinclude tests/test_consistency.c src/*.c -o build/test_consistency.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_boundaries.c  src/*.c -o build/test_boundaries.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_invariants.c  src/*.c -o build/test_invariants.exe
gcc -Wall -Wextra -O2 -DFCH_ENABLE_REDUCED_ROUND_TESTS -Iinclude tests/test_avalanche.c src/*.c -o build/test_avalanche.exe
gcc -Wall -Wextra -O2 -DFCH_ENABLE_REDUCED_ROUND_TESTS -Iinclude tests/test_cryptanalysis.c src/*.c -o build/test_cryptanalysis.exe
gcc -Wall -Wextra -O2 -Iinclude tests/test_vectors.c     src/*.c -o build/test_vectors.exe

gcc -Wall -Wextra -O2 -Iinclude tools/fch.c              src/*.c -o build/fch.exe

build\\fch.exe -256 README.ko.md
```
