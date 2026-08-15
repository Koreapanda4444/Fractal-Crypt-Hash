# Fractal Crypt-Hash (FCH)

Fractal Crypt-Hash(FCH)는 16라운드 ARX 압축 코어와 정규 재귀 트리를 결합한
암호학 연구용 해시 함수입니다. 압축 코어가 각 구간을 섞고, 1,024바이트
고정 리프와 위치가 결합된 이진 노드가 메시지의 변화를 루트까지 전달합니다.

FCH의 설계와 레퍼런스 구현은 분석과 검증을 위해 공개되어 있습니다. 아래의
보안 강도를 목표로 개발하고 있으며, 독립적인 공개 분석은 아직 진행 단계입니다.

English documentation: [README.md](README.md)

## 보안 목표

FCH는 공개된 결정적 비키 해시 함수입니다. 현재 설계는 각 출력 크기에
대응하는 고전적 일반 공격 비용을 목표로 합니다.

| 변형 | 출력 | 충돌 | 원상 | 제2원상 |
| ---- | ---- | ---- | ---- | ------- |
| FCH-256 | 256비트 | 2^128 | 2^256 | 2^256 |
| FCH-512 | 512비트 | 2^256 | 2^512 | 2^512 |

이 수치는 설계 목표입니다. 목표에 대한 분석 범위와 아직 확인해야 할 조건은
사양서에 정리되어 있습니다.

## 구조

FCH는 메시지를 재귀 트리 형태로 처리합니다.

1. 메시지를 패딩한 뒤 루트에서 처리를 시작합니다.
2. 패딩된 입력을 연속된 1,024바이트 리프로 나눕니다.
3. 리프 수에 맞는 하나의 정규 이진 트리를 만듭니다.
4. 자식 상태를 위치와 길이 정보와 함께 순서대로 압축합니다.
5. 루트 상태를 FCH-256 또는 FCH-512 전용 영역에서 마무리합니다.

트리 모양은 패딩된 길이만으로 정해집니다. 메시지 내용으로 자식 수나 경계를
고를 수 없으며, 완성된 2의 거듭제곱 크기 앞부분 서브트리는 뒤에 데이터가
추가돼도 같은 인코딩을 유지합니다.

두 변형 모두 512비트 내부 상태를 사용합니다. FCH-256은 별도의 출력
마무리 영역을 거치므로 FCH-512 결과의 앞부분을 단순히 잘라낸 값이 아닙니다.

### 주요 파라미터

| 항목 | 값 |
| ---- | -- |
| 워드 크기 | 64비트 |
| 내부 상태 | 512비트(8워드) |
| 압축 입력 | 128바이트(16워드) |
| 정식 라운드 | 16 |
| 축소 라운드 분석 기준 | 8라운드 |
| 트리 인코딩 | 버전 2 |
| 리프 범위 | 1,024바이트 |
| 내부 노드 자식 수 | 2개 |
| 트리 레벨 | 리프 수에 따라 결정 |

ARX G 함수와 IV, 회전값, 메시지 순열은 BLAKE2b의 구성요소를 바탕으로
합니다. FCH의 초기화 방식, tweak, 레코드 형식, feed-forward 문맥,
라운드 수와 트리 구조는 별도로 설계했습니다.

## API

```c
#include "fch.h"

uint8_t out256[32];
uint8_t out512[64];

int ok256 = fch_hash_256_checked(data, len, out256);
int ok512 = fch_hash_512_checked(data, len, out512);
```

checked 함수는 성공하면 `1`을 반환합니다. 입력이 잘못됐거나 길이를 지원하지
않거나 메모리 할당에 실패하면 `0`을 반환합니다. 기존 `void` 형태의 함수도
호환용으로 남아 있습니다.

### 스트리밍

스트리밍 API는 `update` 중 1,024바이트 리프가 완성되는 즉시 압축합니다.
컨텍스트에는 아직 완성되지 않은 리프 하나와 트리 레벨별 서브트리 상태 하나만
남습니다. `final`은 패딩과 마지막 리프를 처리한 뒤 보관된 상태를 루트로
합칩니다. 입력 전체를 저장하거나 다시 읽지 않고 임시 파일도 사용하지 않으며,
결과는 원샷 API와 같습니다.

```c
#include "fch_stream.h"

fch256_ctx ctx;
fch256_init(&ctx);
fch256_update(&ctx, chunk1, chunk1_len);
fch256_update(&ctx, chunk2, chunk2_len);
int ok = fch256_final_checked(&ctx, out256);
fch256_free(&ctx);
```

활성 컨텍스트는 하나의 호출 흐름에서만 소유해야 합니다. final 이후의 update와
final 재호출은 실패합니다.

## 명령줄 도구

빌드:

```sh
cd build
make all
```

파일 또는 표준입력 해시:

```sh
./fch -256 path/to/file
./fch -512 path/to/file
cat path/to/file | ./fch -256
```

### Python 기준 구현

`tools/fch_reference.py`는 Python 표준 라이브러리만으로 사양을 그대로 옮긴
읽기 쉬운 기준 구현입니다. C 소스와 코드를 공유하지 않으므로 두 구현의
결과를 독립적으로 비교할 수 있습니다.

```sh
python3 tools/fch_reference.py -256 path/to/file
python3 tools/fch_reference.py -512 path/to/file
```

C CLI를 빌드하고 경계값 및 재귀 트리 입력에서 두 구현을 비교하려면 다음
명령을 사용합니다.

```sh
cd build
make check-reference
```

## 테스트

저장소에는 다음 검사가 포함되어 있습니다.

- 결정성, 고정 출력, 경계값과 잘못된 입력
- avalanche 동작과 축소 라운드 확산
- 정규 트리 경계, 앞부분 안정성과 내용 독립성
- 영역 분리와 little-endian 직렬화의 이식성
- 제한된 차분·선형·고정점·주기·근접 충돌 탐색
- 멀티콜리전·제2원상·상태 이식·장문 트리 패턴
- 원샷/스트리밍 동일성, API 수명주기와 강제 할당 실패
- sanitizer와 libFuzzer 스모크 검사
- 8 MiB 입력의 제한 메모리 처리

일반 및 확장 테스트 실행:

```sh
cd build
make check
make check-extended
```

전체 스케일링 벤치마크 빌드 및 실행:

```sh
make bench
./bench_hash
```

CI와 같은 짧은 검사는 `make bench-check`로 실행합니다. CSV 출력에는 입력
크기, 스트리밍 청크 크기, 반복 횟수, 프로세서 시간, 처리량, 내부 최대 힙 사용량,
해시당 할당 횟수가 담깁니다. 두 출력 크기의 원샷·스트리밍 경로를 모두
측정하며 입력이 커져도 스트리밍 메모리가 일정하게 유지되는지 검사합니다.

Clang 기반의 제한된 libFuzzer 실행:

```sh
make fuzz-smoke
```

CI는 Linux에서 GCC와 Clang, macOS에서 Clang, Windows에서 UCRT64 GCC로
빌드와 테스트를 수행합니다. 32비트 x86 빌드도 실행하며, big-endian PowerPC는
QEMU에서 고정 벡터, 스트리밍, 구조 불변식과 실패 경로를 검사합니다. Linux
작업에는 AddressSanitizer와 UndefinedBehaviorSanitizer도 포함됩니다.

## 문서

- [알고리즘 사양서](spec/fch_spec.ko.md)
- [구현 노트](spec/implementation_notes.ko.md)
