# Fractal Crypt-Hash (FCH)

Fractal Crypt-Hash(FCH)는 16라운드 ARX 압축 코어와 정규 재귀 트리를 결합한
암호학 연구용 해시 함수입니다. 압축 코어가 각 구간을 섞고, 1,024바이트
고정 리프와 위치가 결합된 이진 노드가 메시지의 변화를 루트까지 전달합니다.

현재 리비전은 **FCH research candidate**입니다. 트리 인코딩 2, 패딩 1,
16라운드 코어와 기존 해시 결과를 유지합니다. 독립 암호 검토와 아래에 적은
정식 라운드의 보안 강도는 아직 확립하지 못했습니다.

English documentation: [README.md](README.md)

## 보안 목표

FCH는 공개된 결정적 비키 해시 함수입니다. 현재 설계는 각 출력 크기에
대응하는 고전적 일반 공격 비용을 목표로 합니다.

| 변형 | 출력 | 충돌 | 원상 | 제2원상 |
| ---- | ---- | ---- | ---- | ------- |
| FCH-256 | 256비트 | 2^128 | 2^256 | 2^256 |
| FCH-512 | 512비트 | 2^256 | 2^512 | 2^512 |

이 수치는 설계 목표입니다. 목표에 대한 분석 범위와 아직 확인해야 할 조건은
사양서에 정리되어 있습니다. [검증 및 보안 상태](spec/validation_status.ko.md)는
로컬 및 완료된 CI 통과, 제한된 관측 결과, 조건부 분석과 아직 확립하지 못한
항목을 구분합니다.

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

공개 헤더는 `include/fch.h`, `include/fch_stream.h` 두 개입니다. `src/`의
트리·압축·파라미터·연구 hook 헤더는 내부 구현 인터페이스입니다.

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

저장소 루트에서 실행합니다.

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

## 테스트와 연구 검증

`build/`에서 일반 correctness 검사를 실행합니다. C 프로그램 7개와 CLI
회귀 검사로 구성됩니다.

```sh
make check
make check-reference
make check-interoperability
```

고정 벡터, 입력·리프·트리 경계, 트리 레코드와 불변식, 원샷/스트리밍 동일성,
잘못된 입력, reader·할당 실패, 이식성과 CLI 동작을 확인합니다.
고정 [interoperability corpus](analysis/interoperability-v1.tsv)는 두 변형에 대한
입력 54개를 담습니다. C는 원샷과 스트리밍 계획 11개에서 digest 1,296개,
Python과 C CLI는 각각 expected digest 108개를 검사합니다. 별도 기준 구현
비교의 고정 시드 3개와 입력 384개도 유지합니다.

연구 분석과 stress는 별도로 실행합니다.

```sh
make check-extended
make check-research
```

`check-extended`는 기존 stress 프로그램에서 두 청크 패턴의 8 MiB 스트리밍도
검사합니다. `check-research`는 native 암호분석·트리 탐색, 통합 avalanche·길이·
패턴·트리 확산, trail·characteristic 탐색, 고정 축소 라운드 실험과 조건부
제2원상 계산을 실행합니다. trail 재검산에는 `z3-solver==4.13.1.0`이 필요합니다.
`check-research-native`, `check-diffusion`, `check-trails`,
`check-characteristics`로 개별 묶음도 실행할 수 있습니다.

고정 연구 프로필 재현:

```sh
make check-full-rounds
make check-reduced-rounds
make check-second-preimage-bounds
```

전 라운드 characteristic, 축소 라운드 160행, 조건부 FCH-512 상한 보고서와
정확히 일치해야 합니다. 확대한 characteristic 프로필은 1~16라운드 각각에서
단일 비트 차분 1,024개와 기준 입력 128개를 평가하며, 상태와 출력의 최소값을
각각 집계합니다. 1라운드는 약하므로 정량 통과 기준에서 제외합니다.
정식 16라운드에서 관측한 최소값은 작업 상태 448비트, 압축 출력 203비트이며
보안 하한이 아닙니다. 프로필 변경은 검토가 필요하며 regression을 숨기기 위해
expected 결과를 갱신해서는 안 됩니다.

Linux CI와 같은 AddressSanitizer·UndefinedBehaviorSanitizer 검사입니다.
기본 설정은 누수 검사를 포함합니다.

```sh
make sanitizer-check SANITIZER_TARGETS=check
make sanitizer-check SANITIZER_TARGETS="check-research-native check-diffusion check-extended"
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

같은 시스템에서 다시 비교할 수 있는 기준선을 저장하고 이후 빌드와 비교하려면:

```sh
make bench-baseline BASELINE=benchmark-local.json
make bench-compare BASELINE=benchmark-local.json
```

`baseline-v1` 프로필은 고정 입력 시드와 전체 측정 조합을 사용하고, 한 번의
워밍업 뒤 다섯 번 측정한 프로세서 시간의 중앙값을 저장합니다. JSON에는 소스
리비전, 운영체제, CPU, 컴파일러와 플래그, 처리량, 최대 힙과 할당 횟수도 함께
기록됩니다. 비교는 기록된 실행 환경이 같을 때만 진행하며 메모리·할당 프로필
변화를 거부하고, 기본적으로 각 조합에서 20%를 넘는 처리량 하락을 실패로
판정합니다. 다른 한계는 `MAX_REGRESSION`으로 지정할 수 있습니다.

Clang과 sanitizer로 기존 libFuzzer 대상 4개를 실행합니다.

```sh
make fuzz-smoke
make fuzz-campaign FUZZ_SECONDS=300
```

스모크는 대상마다 실행 1,024회를 요청하고 campaign은 지정한 초 동안 각 대상을
실행합니다. 이름이 있는 결정적 경계 corpus와 고정 시드를 사용하며, 입력·crash·
로그·바이너리와 corpus hash·리비전·컴파일러 플래그·sanitizer 설정을
`build/fuzz/`에 기록합니다. ASan·UBSan·누수 검사가 기본으로 켜집니다.
예약 또는 수동 research CI는 대상마다 600초로 구성되어 있으며 일반 push에서는
건너뜁니다. 실제 실행한 campaign과 한계는
[검증 기록](spec/validation_status.ko.md)에 따로 적었습니다.

`correctness` CI는 Linux GCC/Clang, macOS Clang, Windows UCRT64 GCC의 바이너리
stdin, 32비트 x86, QEMU big-endian PowerPC에서 C 회귀 7개를 검사합니다.
ASan·UBSan·누수, GCC 정적 분석과 C/Python 상호운용성도 포함합니다.
별도의 `research-verification` CI는 native 연구, stress, benchmark, 내용별
실행 시간, 고정 프로필, 전 라운드 trail 재검산과 fuzz/runtime sanitizer를
실행합니다. `make check-all`은 로컬 correctness·reference·상호운용성·stress·
연구·benchmark를 합쳐 실행하며 fuzz와 timing은 명시적인 개별 명령으로 둡니다.

## 문서

- [알고리즘 사양서](spec/fch_spec.ko.md)
- [검증 및 보안 상태](spec/validation_status.ko.md)
- [보안 분석](spec/security_analysis.ko.md)
- [구현 노트](spec/implementation_notes.ko.md)
