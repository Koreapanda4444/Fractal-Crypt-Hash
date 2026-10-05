# 검증 및 보안 상태

기준일은 **2026-10-05 UTC**, 현재 상태는 **FCH research candidate**입니다.
트리 인코딩 2, 패딩 1, 16라운드 코어, 공개 API와 기존 digest를 유지합니다.

검증한 구현·테스트·도구·빌드·워크플로의 정확한 기준점은
[`45c7ddbdf53118a6dee19b1dc1fa9cfb261a52b9`](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/45c7ddbdf53118a6dee19b1dc1fa9cfb261a52b9)입니다.
`release: prepare FCH research candidate` 커밋은 문서만 바꾸므로 소스와 검증
입력이 같습니다. 이 기록은 고정한 소스에서 완료한 실행을 보고하며, 이후의 문서
커밋이 이미 CI를 통과했다고 미리 주장하지 않습니다. 다른 리비전의 상태는 해당
SHA에 연결된 Actions 검사를 확인해야 합니다.

규범 알고리즘은 [fch_spec.ko.md](fch_spec.ko.md), 구현 선택은
[implementation_notes.ko.md](implementation_notes.ko.md), 상세 연구 근거는
[security_analysis.ko.md](security_analysis.ko.md)에 있습니다.

## 주장 구분

| 분류 | 의미 |
| ---- | ---- |
| 설계 목표 | 실제 FCH 구조에 대한 경계가 확립되지 않은 목표 강도 |
| 형식 성질 | 지정된 인코딩·정규 스케줄에서 나오며 구현과 대조한 성질 |
| 조건부 분석 | 명시한 이상적 함수 또는 공격 모델에서만 적용 |
| 관측 결과 | 범위가 제한된 재현 실험이며 범위 밖의 결론을 주지 않음 |
| 로컬 통과 / CI 통과 | 기록한 환경에서 해당 검사가 완료되고 판정 기준을 만족 |
| 구성만 됨 | 실행 경로가 있지만 여기서 실행을 주장하지 않음 |
| 미확립 | 충분한 증명이나 독립 검토가 없음 |

| 주장 | 현재 분류와 한계 |
| ---- | ---------------- |
| FCH-256: 충돌 `2^128`, 원상·제2원상 `2^256` | 설계 목표 |
| FCH-512: 충돌 `2^256`, 원상·제2원상 `2^512` | 설계 목표, 역할별 함수 제2원상 계산은 조건부 |
| 패딩 단사성과 패딩 길이마다 하나인 정규 트리 | 마커·길이 필드와 결정적 분할 규칙의 형식 성질 |
| 고정 정규 표적의 구조 정보 중복도 `kappa = 1` | 표적 스케줄의 형식 성질, 실제 압축 함수의 보안 환원이 아님 |
| C/Python 및 고정 KAT 일치 | 관측한 구현 일치, 두 구현이 같은 사양 오류를 공유할 수 있음 |
| 정식 16라운드 충돌·원상·제2원상 강도 | 미확립 |
| 멀티콜리전·herding·grafting·장문·다중 표적 저항성 | 제한된 탐색, 점근적 비용 미확립 |
| 일반 양자 쿼리 규모 | 조건부 모델 계산, FCH 회로 자원이나 보안 증명 없음 |
| 상수 시간 또는 부채널 안전성 | 미확립, 내용별 시간·자원 관측만 있음 |
| 실서비스 적합성·독립 검토·표준화 | 미확립 |

## 고정 소스에서 완료한 전체 CI

| 워크플로 | 결과 | 완료한 범위 |
| -------- | ---- | ----------- |
| [correctness, 실행 37268684831](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/actions/runs/37268684831) | **8개 작업 통과** | Linux GCC/Clang, macOS Clang, Windows UCRT64 GCC와 바이너리 stdin, 32비트 x86, big-endian PowerPC/QEMU, ASan·UBSan·누수, GCC analyzer, Python 기준 구현·고정 상호운용성 |
| [research-verification, 실행 37268684884](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/actions/runs/37268684884) | **10개 작업 통과, 1개 건너뜀** | Linux GCC/Clang·macOS·Windows·32비트 x86의 native 연구·diffusion·stress·자원 검사, 고정 보고서, 정식 라운드 characteristic, 전 라운드 Z3 trail, fuzz smoke, ASan·UBSan·누수, GCC timing·benchmark 프로필 |
| 예약·수동 `fuzz-campaign` | 구성만 됨, push에서 건너뜀 | 대상 4개에 각 600초와 artifact 보관, 통과로 기록하지 않음 |

직전 14단계 research CI에서는 일반 이름 `MAX_INPUT`이 macOS 시스템 헤더와
충돌했습니다. 참조 10개를 `FCH_DIFFUSION_MAX_INPUT`으로 바꿨으며 입력 한계,
표본과 임계값은 그대로입니다. 수정 이후 위 두 워크플로가 macOS를 포함해 통과했습니다.

## 로컬 검증

| 환경 항목 | 값 |
| --------- | -- |
| 플랫폼 | Ubuntu 24.04.3 LTS, x86-64 |
| 컴파일러 | GCC 13.3.0, 공식 Ubuntu 패키지에서 추출한 Clang/libFuzzer 18.1.3 |
| Python / Z3 | Python 3.12.14 / z3-solver 4.13.1.0 |
| 일반 플래그 | `-std=c11 -Wall -Wextra -Wpedantic -Werror -O2` |
| Runtime 검증 | Clang ASan·UBSan과 frame pointer, ptrace 때문에 로컬 누수 검사 비활성 |
| 누수 근거 | 별도로 완료한 GitHub CI에서 `detect_leaks=1` 사용 |

각 구현 단계는 검증 후 개별 커밋으로 만들었습니다. 아래 최종 로컬 검사는 완료한
소스 변경을 다루며, macro 수정은 위의 실제 cross-platform CI로 추가 검증했습니다.

| 검사 | 결과와 범위 |
| ---- | ----------- |
| `make check` | PASS: C correctness 7개와 CLI 회귀, 기존 고정 digest 6개 유지 |
| `make check-reference` | PASS: C/Python 비교 384회, 고정 시드 3개 |
| `make check-interoperability` | PASS: corpus 입력 54개, C digest 1,296개, Python·C CLI 각각 108개 |
| `make check-extended` | PASS: 기존 stress와 청크 패턴 2개의 8 MiB 스트리밍 |
| `check-research-native` / `check-diffusion` | PASS: 유지한 공격 탐색과 통합 확산 |
| 전 라운드 trail 재검산 | PASS: 1~16라운드, Z3 4.13.1.0, solver 제한시간 120,000 ms |
| `make check-full-rounds` | PASS: 기준 입력 128개·차분 1,024개·1~16라운드 characteristic 고정 보고서와 일치 |
| `make check-reduced-rounds` | PASS: 기존 160행 보고서와 일치, 1라운드 10행 약함·이후 150행 통과 |
| `make check-second-preimage-bounds` | PASS: 기존 조건부 보고서와 일치, 길이 9개·열거한 트리 7개 |
| `make analyze` | PASS: library·CLI·benchmark·회귀·연구·fuzz 번역 단위 25개, 경고를 오류로 처리 |
| Clang ASan·UBSan | PASS: correctness·stress·native 연구·diffusion, 로컬 누수 제외 |
| 고정 시드 fuzz smoke 4개 | PASS: 대상별 요청 1,024회, 총 4,096회 |
| 대상별 60초 fuzz campaign 4개 | PASS: 총 599,160회, crash·ASan·UBSan 오류 없음, 로컬 누수 제외 |
| Benchmark·자원·시간 | PASS: quick matrix, 프로필·비교 단위 검사, 전후 48개 조합 비교와 내용별 시간 검사 |

correctness/research 분리 단계에서 전체 `check-all`이 통과했습니다. 후속 연구
확장 뒤에는 영향받은 native·보고서·runtime·reference·benchmark 검사를 다시
실행했습니다. 통과가 메모리 오류나 공격의 부재를 인증하지는 않습니다.

## 정식 interoperability corpus

[interoperability-v1.tsv](../analysis/interoperability-v1.tsv)는 FCH-256/FCH-512의
**입력 54개와 expected digest 108개**를 고정합니다. SHA-256은
`f7cd9f9339e2f932aef887b807de4df944113543f86909d677888c3e29b90636`입니다.
입력 recipe·출력 크기·라운드·패딩 및 트리 버전·update 크기는 corpus와 사양서에
정의되어 있습니다.

빈 입력, 작은 텍스트·바이너리, 최소 패딩, 120바이트 leaf data 레코드, 패딩 전후
1,024바이트 leaf 경계, 32개 leaf를 넘는 다중 leaf/tree 전환을 포함합니다.
C는 고정 청크 크기 10개(1·7·64·127·128·511·1,023·1,024·1,025·4,096바이트)와
이를 순환하는 계획 하나, 데이터 전후의 빈 update를 사용합니다.

고정 전에 **리팩토링 전 6단계 C CLI**
`baf8e2020b89a090946db607afbf540e7f319571`와 독립적으로 작성한 Python reference로
모든 기대값을 대조했습니다. 현재 C 원샷·스트리밍·CLI와 Python이 같은 고정 corpus에
일치하며 두 validator는 의도적으로 손상한 기대값을 거부합니다. 기존 벡터와 별도
차등 입력 384개도 유지합니다. 저장소 내부의 상호운용성 corpus이며 외부 인증이나
암호학적 증명이 아닙니다.

## 제한된 연구와 runtime 관측

- 정식 라운드 재검산: 역할 6개·기준 입력 16개, 역산 구간 13,056개와
  feed-forward 비교 1,536개 모두 일치.
- 알려진 내부 표적 MITM: 기존 8라운드 4/4와 정식 16라운드의
  1/15·4/12·8/8·12/4·15/1 유지·추가. 정확한 일치는 심은 12비트 후보뿐이며
  전체 내부 표적을 알고 같은 변수를 양쪽에서 쓰므로 digest 원상 지름길이 아님.
- 정규 분할: 리프 3~33개와 절대 오프셋 0·1,024·7,168에서 정규 배치 93개
  허용, 대체 분할 1,581개 거부.
- 확대 characteristic: 라운드당 131,072쌍. 16라운드의 독립 최소값은 작업 상태
  448/1,024비트·압축 출력 203/512비트이며 모든 워드 활성화. 1라운드는 약함.
  동일 경로 개수는 차분 확률의 상한이 아님.
- 이전 출력 최소 통계는 상태가 최소인 증거에서 가져왔음. 집계 오류를 수정하고
  보안 분석에 설명했으며 기존 Python 분석 테스트에서 검증.
- 기존 축소 라운드·고정점/주기·근접 충돌·rebound·멀티콜리전·herding·grafting·
  다중 표적·조건부 제2원상 작업을 유지하고 새 검사는 그 연장선에서 수행.
- Fuzz는 이름 있는 초기 seed 파일 456개와 진화한 corpus·메타데이터·로그를 보관.
  로컬 60초 실행 횟수는 hash 9,310·stream 8,598·padding 13,742·combine 567,510.
  훨씬 긴 예약·수동 campaign은 실행 가능하게 구성했지만 실제 실행으로 기록하지 않음.

## Benchmark 변화

6단계와 완료한 14단계 구현을 기록된 동일 CPU·플랫폼·GCC 13.3.0·`-Werror -O2`
환경에서 비교했습니다. `baseline-v1`은 조합 48개, 고정 시드 `c001d00d`,
워밍업 1회·교차 측정 5회의 중앙 프로세서 시간으로 처리량을 구합니다.
단위는 decimal MB/s입니다. 모든 조합이 설정된 20% 하락 한계 안에 있었고
힙·할당 횟수는 동일했습니다.

| 8 MiB 조합 | 6단계 MB/s | 14단계 후 MB/s | 관측 변화 |
| ---------- | ---------- | -------------- | --------- |
| FCH-256 원샷 | 83.297 | 75.911 | −8.87% |
| FCH-512 원샷 | 82.696 | 75.621 | −8.56% |
| FCH-256 스트림, 64 KiB update | 85.222 | 79.286 | −6.97% |
| FCH-512 스트림, 64 KiB update | 85.221 | 79.016 | −7.28% |

성능 향상이 아니라 관측된 하락을 기록합니다. 로컬 capture를 다른 시스템에 적용할
기준선으로 저장소에 추가하지 않았으며 절대 시간과 작은 차이는 호스트·부하에
의존합니다. 8 MiB에서 원샷 최대 요청 힙은 8,388,681바이트·할당 2회, 스트리밍은
8,208바이트·할당 1회입니다. 해시 후 추적한 내부 할당은 남지 않습니다.
입력 버퍼와 allocator 메타데이터는 이 집계에서 제외합니다.

로컬 64 KiB 내용별 시간 비율은 FCH-256/FCH-512 원샷 1.027/1.007,
스트리밍 1.027/1.013이며 경보선 1.50 미만입니다. 패턴별 자원 사용도 같습니다.
관측 결과이며 상수 시간이나 부채널 안전성의 근거로 확장하지 않습니다.

## 커밋과 통합 기록

1~6단계는 이번 후속 작업 전에 완료했으며 정확한 6단계 main에서 재개했습니다.
7~14단계는 개별 구현·검증·커밋·push를 거쳤고, 문서 단계 전에 macOS CI 오류를
별도 커밋으로 고쳤습니다.

| 단계 | 커밋 | 변경과 이유 |
| ---- | ---- | ----------- |
| 1 | [e87b40f](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/e87b40f7e0463cccdf3862dec3beeacce9a70c56) `test: consolidate basic regression coverage` | consistency 입력 5개를 기존 vector 회귀에 흡수. |
| 2 | [8bb52fe](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/8bb52fedb6e98695e9959bb74ac81cf56bc1eaa7) `test: consolidate tree invariant coverage` | 경계 trace 12개와 정규·내용 독립·접두사 불변식을 기존 프로그램에 유지. |
| 3 | [9f5a743](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/9f5a7439843fbfc56d6ae5f839c8f207d2a6fa71) `analysis: consolidate diffusion experiments` | 확산 실험 4개 통합, 이전 관측값 165개 유지, C 직접 include 제거. |
| 4 | [ad1516d](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/ad1516d0865b7ac6480ec0d7c89da1ce6c9c0fd6) `test: consolidate failure and stress coverage` | 실패·8 MiB 스트리밍 이동, 실제 fuzz 진입점과 구조 stream 사례 유지. |
| 5 | [9287dea](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/9287dea0e4a4102ffe5aab1e493cd1ec7c68449f) `fuzz: remove redundant CLI fuzz harness` | 고정 모드 CLI harness·전용 shim 제거, 인자·바이너리 입력·오류 회귀 유지. |
| 6 | [baf8e20](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/baf8e2020b89a090946db607afbf540e7f319571) `tools: remove obsolete vector generation helper` | 구현에서 기대값을 만드는 출력 helper 제거, 기존 고정 digest 6개 유지. |
| 7 | [69ad18b](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/69ad18b052ec9af5218e956e7b372707bebedec3) `refactor: separate public and internal headers` | 내부 헤더 7개를 src로 이동하고 공개 헤더 2개 유지. |
| 8 | [daf5e3c](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/daf5e3c09fb4402c5b0d2d9f93f53e8d5a0cf6f2) `refactor: remove unused depth plumbing` | 쓰이지 않는 내부 depth guard 제거, 실제 level과 잘못된 배치 거부 유지. |
| 9 | [2ca1b09](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/2ca1b0972635c5eb8dc43fa210326d04c56e5f95) `refactor: deduplicate tree assembly internals` | 기존 소스에서 workspace·leaf push·carry merge·root fold·구조 비교 공용화. |
| 10 | [15d2ef2](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/15d2ef21db46b338578cdebbd3bf42e7c271f7e2) `build: isolate generated build artifacts` | 컴파일러·바이너리·dependency·bytecode 생성물을 build로 한정. |
| 11 | [159e5bc](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/159e5bcab0f5cf68ad216f286983b83497368453) `build: separate regression and research checks` | native 연구 2개를 analysis로 이동, correctness·research CI 분리. |
| 12 | [5b6a6d5](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/5b6a6d5ea747be3818b613064880226cfa98bddb) `analysis: strengthen reproducible security evaluation` | 기존 대상 4개용 driver 하나, 결정적 campaign·메타데이터·sanitizer·누수 검사. |
| 13 | [13014d3](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/13014d34c0ac4ef7e5d463cdbeeb0cd30ed2642b) `analysis: add canonical interoperability corpus` | 두 변형 입력 54개 고정, 기존 C·Python validator에 KAT 흡수. |
| 14 | [bf44445](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/bf4444535157c16f377c257097345d4bd59ec08f) `analysis: extend implementation and attack validation` | 정식 라운드 재검산·MITM·트리 확장, 독립 최소값 집계 수정과 고정 보고서. |
| CI 수정 | [45c7ddb](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/45c7ddbdf53118a6dee19b1dc1fa9cfb261a52b9) `fix: avoid diffusion macro collision on macOS` | 실험 내부 macro 이름만 바꾸고 파라미터·관측값 유지. |
| 15 | `release: prepare FCH research candidate` (이 문서가 포함된 커밋) | 영문·국문 README·사양·구현·보안·검증 문서를 검증한 소스와 일치시킴. |

제거한 기존 경로와 유지한 검증:

| 기존 경로 | 목적지 또는 제거 이유 |
| --------- | -------------------- |
| `tests/test_consistency.c` | `tests/test_vectors.c`의 결정성 회귀 입력 |
| `tests/test_tree_boundaries.c` | `tests/test_boundaries.c`의 패딩 leaf·node·root 개수와 높이 |
| `tests/test_split_sensitivity.c` | `tests/test_invariants.c`의 스케줄·완성된 접두사, 확산은 통합 runner에 유지 |
| `tests/test_avalanche.c`, `tests/test_patterns.c`, `tests/test_length_variation.c`, `tests/test_depth_diffusion.c` | `analysis/fch_diffusion.c`에 실험·고정 시드·임계값 유지, C 직접 include 제거 |
| `tests/test_hardening.c` | 실패 → `test_failures.c`, 구조 입력 → `test_stream_equivalence.c`, 8 MiB → `test_stress.c`, 실제 libFuzzer body → `fuzz_hash.c`, 중복 자체 smoke 제거 |
| `tests/fuzz_cli.c`, `tools/fch_cli.h` | 효용이 낮은 고정 인자 fuzz 제거, 인자·오류·바이너리 입력은 `test_cli.sh`와 Windows CI에 유지 |
| `tools/gen_vectors.c` | 구현에서 기대값을 만드는 출력 helper 제거, 고정 기대값·독립 비교 유지 |
| `include/params.h`, `fractal.h`, `leaf.h`, `combine.h`, `mix.h`, `bitops.h`, `debug_hooks.h` | 헤더 7개를 `include/`에서 `src/`로 이동해 공개 API와 분리 |
| `tests/test_cryptanalysis.c`, `tests/test_tree_attacks.c` | `analysis/fch_cryptanalysis.c`, `analysis/fch_tree_attacks.c`로 이동, 소스 검증과 바이너리 이름 유지 |

7~15단계에서 검증 파일을 폐기하지 않았습니다. 새 연구 파일은 기존 harness용
driver와 고정 dataset 2개(corpus·확대 characteristic 보고서)로 한정했습니다.
집계 반례와 KAT 검사는 기존 테스트에 흡수했습니다.

## 남은 구조

| 계층 | 파일 또는 역할 |
| ---- | -------------- |
| 공개 include | `fch.h`, `fch_stream.h`만 유지 |
| 코어 구현 | C 소스 8개·내부 헤더 7개, 기존 소스에서 트리 조립 공유 |
| 일반 회귀 | `test_boundaries.c`, `test_stream_equivalence.c`, `test_invariants.c`, `test_tree_encoding.c`, `test_portability.c`, `test_vectors.c`, `test_failures.c`, `test_cli.sh` |
| Stress | `test_stress.c` |
| Fuzz | `fuzz_hash.c`, `fuzz_stream.c`, `fuzz_padding.c`, `fuzz_tree_combine.c` |
| Python 도구 검증 | 기존 benchmark·축소 라운드/characteristic·제2원상 테스트, 공용 `test_utils.h` |
| Native analysis | `fch_diffusion.c`, `fch_cryptanalysis.c`, `fch_tree_attacks.c` |
| 고정 analysis 데이터 | `interoperability-v1.tsv`, `full-round-characteristics-v1.json`, `reduced-round-v1.json`, `fch512-second-preimage-v1.json` |
| 도구 | C CLI와 Python 7개: reference·trail·characteristic·축소 라운드·상한·benchmark·fuzz driver |
| Benchmark·build·CI | 기존 `bench/bench_hash.c`, `build/Makefile`, correctness·research 워크플로 |

## 미해결 연구와 candidate 한계

고정 리비전의 독립 검토가 필요합니다. 정식 라운드 충돌·원상·제2원상 비용,
실제 ARX 코어에서 이상적 역할별 함수로의 환원, 최적화한 고가중치 trail·rebound·
MITM, 트리·장문·다중 표적의 엄밀한 비용 경계는 미해결입니다.
구체적인 양자 자원, 장시간 외부 fuzz, 하드웨어 카운터 timing과 부채널 평가도
남아 있습니다. Python reference는 독립적으로 코딩했지만 외부 구현과 제3자
corpus 검증은 확립하지 못했습니다.

구현 비교와 제한된 연구를 재현할 수 있는 candidate입니다. production crypto
release가 아니며 설계 목표를 달성했다고 주장하지 않습니다.

## 재현 방법

Python·Z3 4.13.1.0·Clang/libFuzzer가 있는 환경에서 `build/` 기준:

```sh
make clean
make check-all CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -Werror -O2"
make analyze timing-check
make sanitizer-check SANITIZER_TARGETS=check
make sanitizer-check SANITIZER_TARGETS="check-research-native check-diffusion check-extended"
make fuzz-smoke
make fuzz-campaign FUZZ_SECONDS=60
```

더 긴 예약·수동 방식 campaign은 `FUZZ_SECONDS=600`을 사용합니다.
기본 sanitizer·fuzz 설정은 누수를 켭니다. 호스트가 LeakSanitizer를 실행할 수
없을 때만 `detect_leaks=0`을 사용하고 그 한계를 기록합니다.

성능 비교는 같은 시스템에서 실행합니다.

```sh
make clean
make bench-baseline BASELINE=benchmark-local.json
# 이후 리비전을 같은 환경과 플래그로 다시 빌드합니다.
make bench-compare BASELINE=benchmark-local.json
```

기준선 파일은 `build/` 안에 보관하며 source tree의 빌드 산출물은 제외합니다.
