# Validation and Security Status

Status date: **2026-10-05 UTC**. The release status is **FCH research
candidate**. Tree encoding 2, padding 1, the 16-round core, public API,
and existing digests are preserved.

The validated implementation, tests, tools, build rules, and workflows are
pinned to source commit
[`45c7ddbdf53118a6dee19b1dc1fa9cfb261a52b9`](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/45c7ddbdf53118a6dee19b1dc1fa9cfb261a52b9).
The `release: prepare FCH research candidate` commit updates documentation
only; it has identical source and validation inputs. This record reports
completed runs on the pinned source, rather than claiming that a future
documentation commit has already run CI. For any later revision, consult
the Actions checks attached to that exact SHA.

The normative algorithm is in [fch_spec.md](fch_spec.md), implementation
choices in [implementation_notes.md](implementation_notes.md), and detailed
research evidence in [security_analysis.md](security_analysis.md).

## Claim categories

| Category | Meaning |
| -------- | ------- |
| Design target | Intended strength, without an established bound for the actual FCH construction |
| Format property | Follows from the specified encoding or canonical schedule; checked against implementation |
| Conditional analysis | Applies only under the explicitly stated ideal-map or attack model |
| Observed result | Reproducible output of a bounded experiment; no conclusion outside its scope |
| Local pass / CI pass | The named check completed and met its acceptance criteria in the recorded environment |
| Configured only | A workflow offers the check; no execution is claimed here |
| Not established | No sufficient proof or independent review supports the claim |

| Claim | Category and current limit |
| ----- | -------------------------- |
| FCH-256: collision `2^128`, preimage/second preimage `2^256` | Design targets |
| FCH-512: collision `2^256`, preimage/second preimage `2^512` | Design targets; typed-map second-preimage accounting is conditional |
| Injective padding and one canonical tree per padded length | Format properties; marker/length fields and deterministic split rule |
| Descriptor multiplicity `kappa = 1` in a fixed canonical target | Format property of the target schedule; not a real-compression security reduction |
| C/Python agreement and fixed KATs | Observed implementation agreement; both implementations may share a specification error |
| Full 16-round collision, preimage, and second-preimage strength | Not established |
| Multicollision, herding, grafting, long-message and multi-target resistance | Bounded screens only; asymptotic costs not established |
| Generic quantum query scales | Conditional model accounting; no concrete FCH circuit/resources or security proof |
| Constant-time or side-channel security | Not established; only content-timing/resource screens |
| Production suitability, independent review, standardization | Not established |

## Completed CI on the pinned source

| Workflow | Result | Completed scope |
| -------- | ------ | --------------- |
| [correctness, run 37268684831](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/actions/runs/37268684831) | **8 jobs passed** | Linux GCC/Clang, macOS Clang, Windows UCRT64 GCC and native binary stdin, 32-bit x86, big-endian PowerPC/QEMU, ASan/UBSan/leaks, GCC analyzer, Python reference and fixed interoperability |
| [research-verification, run 37268684884](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/actions/runs/37268684884) | **10 jobs passed; 1 skipped** | Native research/diffusion/stress/resource checks on Linux GCC/Clang, macOS, Windows and 32-bit x86; fixed reports; full-round characteristics; all-round Z3 trails; fuzz smoke; ASan/UBSan/leaks; GCC timing and benchmark profile |
| Scheduled/manual `fuzz-campaign` | Configured only; skipped on push | Four 600-second campaigns and preserved artifacts; no pass claimed |

The preceding step-14 research run exposed a macOS system-header collision
with the generic diffusion macro `MAX_INPUT`. The repair renames its ten
references to `FCH_DIFFUSION_MAX_INPUT`; the bound, samples, and thresholds
are unchanged. Both workflows above passed after the repair, including macOS.

## Local validation

| Environment item | Value |
| ---------------- | ----- |
| Platform | Ubuntu 24.04.3 LTS, x86-64 |
| Compiler | GCC 13.3.0; Clang/libFuzzer 18.1.3 extracted from official Ubuntu packages |
| Python / Z3 | Python 3.12.14 / z3-solver 4.13.1.0 |
| Normal flags | `-std=c11 -Wall -Wextra -Wpedantic -Werror -O2` |
| Runtime validation | Clang ASan/UBSan, frame pointers; local leak detection disabled because of ptrace |
| Leak evidence | Separate completed GitHub CI runs use `detect_leaks=1` |

Each implementation stage was validated before its own commit. The final
local checks below cover the completed source changes; the macro repair is
additionally validated by the completed cross-platform CI above.

| Check | Result and scope |
| ----- | ---------------- |
| `make check` | PASS: seven C correctness programs and CLI regression; six original fixed digests retained |
| `make check-reference` | PASS: 384 C/Python comparisons, three fixed seeds |
| `make check-interoperability` | PASS: 54 corpus messages, 1,296 C digests, 108 Python digests, 108 C CLI digests |
| `make check-extended` | PASS: existing stress program, including 8 MiB streaming through two chunk patterns |
| `check-research-native` / `check-diffusion` | PASS: retained attack screens and consolidated diffusion |
| All-round trail replay | PASS: rounds 1–16 with Z3 4.13.1.0 and 120,000 ms solver timeout |
| `make check-full-rounds` | PASS: exact match to 128-base, 1,024-difference, 1–16-round characteristic report |
| `make check-reduced-rounds` | PASS: unchanged 160-row report; ten round-one rows weak, 150 later rows pass |
| `make check-second-preimage-bounds` | PASS: unchanged conditional report; nine lengths and seven enumerated trees |
| `make analyze` | PASS: GCC analyzer over 25 library/CLI/benchmark/regression/research/fuzz translation units, warnings as errors |
| Clang ASan/UBSan | PASS: correctness, stress, native research and diffusion; local leaks excluded |
| Four seeded fuzz smokes | PASS: 1,024 requested executions per target, 4,096 total |
| Four 60-second fuzz campaigns | PASS: 599,160 recorded executions, no crash or ASan/UBSan failure; local leaks excluded |
| Benchmark/resource/timing | PASS: quick matrix, profile/comparison unit checks, full 48-case before/after comparison and content-timing screen |

The complete combined `check-all` passed during the regression/research split.
After later research extensions, the affected native, report, runtime,
reference, and benchmark checks were rerun. These passes do not certify
absence of memory bugs or attacks.

## Canonical interoperability

[interoperability-v1.tsv](../analysis/interoperability-v1.tsv) freezes **54
inputs and 108 expected digests** for FCH-256/FCH-512. Its SHA-256 is
`f7cd9f9339e2f932aef887b807de4df944113543f86909d677888c3e29b90636`.
Recipes, output sizes, rounds, padding/tree versions, and update sizes are
specified in the corpus and normative specification.

It covers empty and small text/binary inputs, minimum padding, 120-byte
leaf-data records, raw and padded 1,024-byte leaf edges, and multi-leaf tree
transitions across the 32-leaf boundary. C uses ten fixed chunk sizes
(1, 7, 64, 127, 128, 511, 1,023, 1,024, 1,025, 4,096 bytes), one cycling plan,
and empty updates before and after each message.

All expectations were cross-checked against the **pre-refactor step-6 C CLI**
at `baf8e2020b89a090946db607afbf540e7f319571` and the independent Python
reference before freezing. Current C one-shot/streaming, C CLI, and Python
match the same fixed corpus. Both validators reject deliberately corrupted
expected digests. Existing vectors and the separate 384 differential cases
remain. This is an in-repository interoperability corpus, not third-party
certification or a cryptographic proof.

## Bounded research and runtime observations

- Full-round replay: six encoded roles, 16 bases, 13,056 inverse windows and
  1,536 feed-forward comparisons all match.
- Known-internal-target MITM: retained 8-round 4/4 plus full 16-round splits
  1/15, 4/12, 8/8, 12/4, 15/1. Only the planted 12-bit candidate matches
  exactly; full internal target knowledge and shared variables prevent
  interpreting this as a digest-preimage shortcut.
- Canonical partitions: 3–33 leaves at absolute offsets 0, 1,024, 7,168;
  93 canonical layouts accepted and 1,581 alternatives rejected.
- Expanded characteristics: 131,072 pairs per round. At round 16 the
  independently aggregated minima are 448/1,024 state bits and 203/512
  compression-output bits. All state/output words are active. Round one is
  weak. Exact trajectory counts do not bound differential probabilities.
- The former output-minimum statistic was taken at the state-minimum witness;
  this aggregation error is fixed, explained in the security analysis, and
  covered in the existing Python analysis test.
- Existing reduced-round, fixed-point/cycle, near-collision, rebound,
  multicollision, herding, grafting, multi-target, and conditional
  second-preimage work remains; the new screens extend that work.
- Fuzz campaigns preserve 456 named initial seed files plus evolved corpora,
  metadata and logs. Local 60-second counts: hash 9,310; stream 8,598;
  padding 13,742; combine 567,510. A substantially longer scheduled/manual
  campaign is available but is not reported as executed.

## Benchmark change

The comparison uses the same recorded CPU, platform, GCC 13.3.0, and
`-Werror -O2` flags for step 6 versus the completed step-14 implementation.
The `baseline-v1` profile has 48 cases, fixed seed `c001d00d`, one warmup
and five interleaved measured trials; values are median processor-time
throughput, in decimal MB/s. All 48 cases stayed within the configured 20%
per-case regression limit, with identical heap/allocation counts.

| 8 MiB case | Step 6 MB/s | After step 14 MB/s | Observed change |
| ---------- | ----------- | ----------------- | --------------- |
| FCH-256 one-shot | 83.297 | 75.911 | −8.87% |
| FCH-512 one-shot | 82.696 | 75.621 | −8.56% |
| FCH-256 stream, 64 KiB updates | 85.222 | 79.286 | −6.97% |
| FCH-512 stream, 64 KiB updates | 85.221 | 79.016 | −7.28% |

This records a slowdown, not a performance improvement. The local captures
were not added as a portable baseline; absolute timings and small changes
depend on the host and load. At 8 MiB, one-shot requested peak heap is
8,388,681 bytes in two allocations; streaming is 8,208 bytes in one
allocation. No tracked implementation allocation remains live after hashing;
input buffers and allocator metadata are excluded from this accounting.

The local 64 KiB content-timing ratios were 1.027/1.007 for FCH-256/FCH-512
one-shot and 1.027/1.013 for streaming, below the 1.50 alarm.
Allocation/resource behavior matched across patterns. This is an observation,
not constant-time or side-channel security evidence.

## Commit and consolidation record

Steps 1–6 preceded this continuation; work resumed from the exact step-6 main
revision. Steps 7–14 were individually implemented, validated, committed,
and pushed. A separate repair addresses the macOS CI failure before the
documentation stage.

| Step | Commit | Change and reason |
| ---- | ------ | ----------------- |
| 1 | [e87b40f](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/e87b40f7e0463cccdf3862dec3beeacce9a70c56) `test: consolidate basic regression coverage` | Absorb all five consistency inputs into fixed-vector regression. |
| 2 | [8bb52fe](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/8bb52fedb6e98695e9959bb74ac81cf56bc1eaa7) `test: consolidate tree invariant coverage` | Preserve twelve boundary traces and canonical/content/prefix invariants in existing programs. |
| 3 | [9f5a743](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/9f5a7439843fbfc56d6ae5f839c8f207d2a6fa71) `analysis: consolidate diffusion experiments` | Merge four diffusion experiments; retain all 165 prior observation values and remove direct C inclusion. |
| 4 | [ad1516d](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/ad1516d0865b7ac6480ec0d7c89da1ce6c9c0fd6) `test: consolidate failure and stress coverage` | Move failures and 8 MiB streaming; retain the real fuzz entry point and structured stream cases. |
| 5 | [9287dea](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/9287dea0e4a4102ffe5aab1e493cd1ec7c68449f) `fuzz: remove redundant CLI fuzz harness` | Retire the fixed-mode CLI harness and its private shim; retain argument, binary-input, and error regression. |
| 6 | [baf8e20](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/baf8e2020b89a090946db607afbf540e7f319571) `tools: remove obsolete vector generation helper` | Retire implementation-derived vector printing; keep six original fixed digests. |
| 7 | [69ad18b](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/69ad18b052ec9af5218e956e7b372707bebedec3) `refactor: separate public and internal headers` | Move seven implementation headers to src; leave two public headers. |
| 8 | [daf5e3c](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/daf5e3c09fb4402c5b0d2d9f93f53e8d5a0cf6f2) `refactor: remove unused depth plumbing` | Remove unused internal depth guards; retain encoded levels and invalid-layout rejection. |
| 9 | [2ca1b09](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/2ca1b0972635c5eb8dc43fa210326d04c56e5f95) `refactor: deduplicate tree assembly internals` | Share tree workspace, leaf push, carry merges, root fold, and descriptor comparison in existing sources. |
| 10 | [15d2ef2](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/15d2ef21db46b338578cdebbd3bf42e7c271f7e2) `build: isolate generated build artifacts` | Confine generated compiler/binary/dependency/bytecode outputs to build. |
| 11 | [159e5bc](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/159e5bcab0f5cf68ad216f286983b83497368453) `build: separate regression and research checks` | Move two native research programs to analysis and separate correctness/research workflows. |
| 12 | [5b6a6d5](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/5b6a6d5ea747be3818b613064880226cfa98bddb) `analysis: strengthen reproducible security evaluation` | Add one driver for four existing targets; deterministic campaigns, metadata, runtime sanitizer/leak coverage. |
| 13 | [13014d3](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/13014d34c0ac4ef7e5d463cdbeeb0cd30ed2642b) `analysis: add canonical interoperability corpus` | Freeze 54 messages for both variants; absorb KAT checks into existing C and Python validators. |
| 14 | [bf44445](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/bf4444535157c16f377c257097345d4bd59ec08f) `analysis: extend implementation and attack validation` | Extend full-round replay, MITM and tree screens; fix independent minima and freeze the expanded report. |
| CI repair | [45c7ddb](https://github.com/Koreapanda4444/Fractal-Crypt-Hash/commit/45c7ddbdf53118a6dee19b1dc1fa9cfb261a52b9) `fix: avoid diffusion macro collision on macOS` | Rename the internal experiment macro; preserve parameters and observations. |
| 15 | `release: prepare FCH research candidate` (commit containing this record) | Align English/Korean README, specification, implementation, security and validation documentation with verified source. |

Retired original paths and retained coverage:

| Original path | Destination or removal reason |
| ------------- | ----------------------------- |
| `tests/test_consistency.c` | `tests/test_vectors.c`: deterministic regression inputs |
| `tests/test_tree_boundaries.c` | `tests/test_boundaries.c`: padded leaf/node/root counts and heights |
| `tests/test_split_sensitivity.c` | `tests/test_invariants.c`: schedule and completed-prefix checks; diffusion retained in the consolidated runner |
| `tests/test_avalanche.c`, `tests/test_patterns.c`, `tests/test_length_variation.c`, `tests/test_depth_diffusion.c` | `analysis/fch_diffusion.c`: preserve experiments, fixed seeds and thresholds without direct C inclusion |
| `tests/test_hardening.c` | Failures → `test_failures.c`; structured input → `test_stream_equivalence.c`; 8 MiB streaming → `test_stress.c`; actual libFuzzer body → `fuzz_hash.c`; redundant standalone smoke retired |
| `tests/fuzz_cli.c`, `tools/fch_cli.h` | Low-value fixed argument fuzzing removed; argument/error/binary-input coverage retained in `test_cli.sh` and Windows CI |
| `tools/gen_vectors.c` | Self-derived expected-value printer removed; fixed expectations and independent comparison retained |
| `include/params.h`, `fractal.h`, `leaf.h`, `combine.h`, `mix.h`, `bitops.h`, `debug_hooks.h` | Seven headers moved from `include/` to `src/`; internal interfaces separated from public API |
| `tests/test_cryptanalysis.c`, `tests/test_tree_attacks.c` | Moved to `analysis/fch_cryptanalysis.c`, `analysis/fch_tree_attacks.c`; source coverage and executable names retained |

No validation files were discarded in steps 7–15. New research files are
limited to the existing-harness driver and two fixed datasets: the canonical
corpus and expanded characteristic report. The analysis aggregation
counterexample and KAT checks were absorbed into existing tests.

## Remaining structure

| Layer | Files or role |
| ----- | ------------- |
| Public include | `fch.h`, `fch_stream.h` only |
| Core implementation | Eight C sources, seven internal headers; tree assembly shared in existing sources |
| Regular regression | `test_boundaries.c`, `test_stream_equivalence.c`, `test_invariants.c`, `test_tree_encoding.c`, `test_portability.c`, `test_vectors.c`, `test_failures.c`; `test_cli.sh` |
| Stress | `test_stress.c` |
| Fuzz | `fuzz_hash.c`, `fuzz_stream.c`, `fuzz_padding.c`, `fuzz_tree_combine.c` |
| Python tool validation | Existing benchmark, reduced-round/characteristic and second-preimage tests; shared `test_utils.h` |
| Native analysis | `fch_diffusion.c`, `fch_cryptanalysis.c`, `fch_tree_attacks.c` |
| Fixed analysis data | `interoperability-v1.tsv`, `full-round-characteristics-v1.json`, `reduced-round-v1.json`, `fch512-second-preimage-v1.json` |
| Tools | C CLI plus seven Python tools: reference, trail, characteristic, reduced-round, bound, benchmark, fuzz driver |
| Benchmark/build/CI | Existing `bench/bench_hash.c`, `build/Makefile`, separate correctness/research workflows |

## Open work and candidate limits

Independent review against a pinned revision remains necessary. Full-round
collision/preimage/second-preimage complexity, a reduction from the actual
ARX core to ideal typed maps, optimized higher-weight trail/rebound/MITM
analysis, and tight tree/long-message/multi-target bounds are unresolved.
Concrete quantum resources, extended external fuzzing, hardware-counter
timing, and side-channel evaluation also remain open. The in-repository
Python reference is independently coded; external implementations and
third-party corpus validation have not been established.

The candidate is reproducible enough for implementation comparison and
bounded research. It is not a production cryptography release or an assertion
that the design targets have been met.

## Reproduction

From `build/`, with Python, Z3 4.13.1.0 and Clang/libFuzzer available:

```sh
make clean
make check-all CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -Werror -O2"
make analyze timing-check
make sanitizer-check SANITIZER_TARGETS=check
make sanitizer-check SANITIZER_TARGETS="check-research-native check-diffusion check-extended"
make fuzz-smoke
make fuzz-campaign FUZZ_SECONDS=60
```

A longer scheduled/manual-style campaign uses `FUZZ_SECONDS=600`.
Default sanitizer/fuzz settings enable leaks; use `detect_leaks=0` only when
the host cannot run LeakSanitizer and record that limitation.

Performance comparison is machine-specific:

```sh
make clean
make bench-baseline BASELINE=benchmark-local.json
# Rebuild a later revision with the same environment and flags.
make bench-compare BASELINE=benchmark-local.json
```

Keep baseline records under `build/`; source-tree build artifacts are excluded.
