# Validation and Security Status

This document states what has actually been checked, what is only configured
in CI, and what remains a security goal or open question. It is deliberately
separate from the normative algorithm in [fch_spec.md](fch_spec.md) and the
detailed evidence in [security_analysis.md](security_analysis.md).

Status date: **2026-09-22 UTC**. The record applies to the working tree through
roadmap item 15. That tree is based on commit
`1eca2595f3f59c5e75135cdb621617d14f9242c9`, but contains uncommitted roadmap
changes after that commit. The base SHA alone therefore does not identify the
validated bytes. External review must use the eventual commit or the delivered
archive and its checksum.

## How to read the status

| Label | Meaning |
| ----- | ------- |
| Design target | A security level the construction aims to meet; it is not a demonstrated result |
| Established format property | Follows from the specified encoding or canonical schedule and is checked against the implementation |
| Conditional analysis | Holds only in the stated ideal-map or attack model |
| Observed result | Reproducible output of a bounded test or search, not a proof outside that range |
| Local pass | Reproduced in the environment recorded below |
| CI configured | A workflow contains the check; this does not claim that the uncommitted snapshot ran on GitHub Actions |
| Not established | No proof, independent review, or sufficient experiment currently supports the claim |

## Security claim summary

| Claim | Current status | Evidence and limit |
| ----- | -------------- | ------------------ |
| FCH-256 targets: `2^128` collision, `2^256` preimage and second preimage | Design target | Generic ideal-output scales only; no reduction for the real compression function |
| FCH-512 targets: `2^256` collision, `2^512` preimage and second preimage | Design target | The descriptor-compatible `2^512` second-preimage map scale is conditional on the ideal typed-map model |
| Padding injectivity and one canonical tree for each padded length | Established format property | Specified length field, marker, fixed leaves, and deterministic split rule; boundary and encoding tests agree |
| One compatible state per descriptor in a fixed canonical target (`kappa = 1`) | Established format property | Descriptor uniqueness follows from role and range binding; representative trees are enumerated by the fixed profile |
| C and Python reference agreement | Observed result | 384 comparisons with three fixed seeds; both implementations can still share the same design error |
| Full 16-round collision, preimage, and second-preimage resistance | Not established | Bounded searches found no break, but do not determine full-domain complexity |
| Reduced-round behavior | Observed result | Round one is reported as weak; all 150 checked round/family results from rounds 2 through 16 pass the fixed thresholds |
| Long-message, multicollision, herding, grafting, and multi-target resistance | Observed result | Current deterministic screens found no exact match; their sample sizes do not prove asymptotic resistance |
| Quantum resistance | Conditional analysis | Only generic query and opportunity accounting is available; no reversible-circuit resource estimate or FCH-specific proof exists |
| Production suitability or standardization | Not established | No independent cryptographic review, standardization process, or production deployment review has been completed |

## Locally reproduced environment

| Item | Value |
| ---- | ----- |
| Operating system | Ubuntu 24.04.3 LTS, Linux 6.18.44, x86-64 |
| C compiler | GCC 13.3.0 |
| Python | 3.12.14 |
| Z3 | 4.13.1 |
| Sanitizers | GCC AddressSanitizer and UndefinedBehaviorSanitizer |
| Clang/libFuzzer | Not available in this local environment |
| Leak detection | Not run; the sanitizer profile sets `ASAN_OPTIONS=detect_leaks=0` |

The following results were reproduced on that environment. `PASS` means only
that the named command met its built-in acceptance criteria.

| Command or group | Local status | Reproduced scope |
| ---------------- | ------------ | ---------------- |
| `make check` | PASS | 13 regular C test programs plus CLI input and error handling |
| `make check-extended` | PASS | Four extended programs: length variation, depth diffusion, stress, and split sensitivity |
| `make check-reference` | PASS | 384 C/Python comparisons using three fixed seeds |
| `make check-trails` | PASS | Two fixed 8-bit families over rounds 1 through 8, with Z3 replay of reported witnesses |
| `make check-characteristics` | PASS | 1,024 single-bit candidates over 32 bases for rounds 1 through 8; round one weak, rounds 2 through 8 pass |
| `make check-reduced-rounds` | PASS | Exact match to the 160-row `reduced-round-v1` report; 10 round-one rows weak and 150 rows pass |
| `make check-second-preimage-bounds` | PASS | Nine length profiles and seven enumerated canonical trees; conditional accounting with `kappa = 1` |
| `make bench-check` | PASS | 32 quick one-shot and streaming cases, including heap and allocation invariants |
| `make bench-baseline-check` | PASS | Six unit tests for baseline schema and comparison logic; not a portable performance baseline |
| `make timing-check` | PASS | Four same-length content paths stayed below the configured 1.50 median-time ratio |
| `make analyze` | PASS | GCC path-sensitive analysis of 31 source and test translation units with warnings as errors |
| `make sanitizer-check SANITIZER_CC=gcc` | PASS | Regular and extended suites under combined ASan and UBSan; leak detection excluded |

## CI coverage versus this snapshot

The primary workflow defines nine job executions: three build-and-test matrix
entries plus sanitizer, static/timing, fuzz-smoke, Windows, 32-bit x86, and
big-endian jobs. The extended workflow defines three additional cryptanalysis
jobs. These paths are useful coverage, but the current roadmap working tree is
uncommitted and has not run in GitHub Actions.

| Environment or job | Repository configuration | This snapshot's evidence |
| ------------------ | ------------------------ | ------------------------ |
| Linux x86-64, GCC | Full regular/extended tests, benchmark, and reference analysis | Reproduced locally |
| Linux x86-64, Clang | Full regular/extended tests and benchmark | CI configured; not reproduced locally |
| macOS, Clang | Full regular/extended tests and benchmark | CI configured; not reproduced locally |
| Windows, UCRT64 GCC | Full suites, benchmark, and native binary-stdin check | CI configured; not reproduced locally |
| Linux 32-bit x86, GCC | Full suites and benchmark with `-m32` | CI configured; not reproduced locally |
| Big-endian PowerPC under QEMU | Eight selected functional and failure-path binaries | CI configured; not reproduced locally |
| Clang ASan/UBSan | Regular and extended suites | CI configured; local sanitizer pass used GCC |
| Five Clang libFuzzer targets | 1,024 runs per target with a 10-second per-input timeout | CI configured; not reproduced locally |
| Extended cryptanalysis | Native screens plus all-round trail and expanded characteristic jobs | CI configured; only the default local profiles were reproduced |

For a committed revision, the GitHub Actions checks attached to that exact SHA
are authoritative for CI status. Workflow presence by itself is never recorded
as a pass.

## Explicitly unverified

- No independent cryptographic review has been completed against a pinned
  revision.
- No proof connects the real ARX compression construction to the independent
  random typed maps used by the conditional bounds.
- The automated differential, linear, rebound, meet-in-the-middle, and tree
  searches cover fixed, bounded families rather than the full input space.
- Long-running coverage-guided fuzzing, LeakSanitizer or an equivalent leak
  checker, and hardware-counter timing analysis have not been completed for
  this snapshot.
- The timing screen is not a constant-time or side-channel certification.
- No concrete quantum gate, depth, qubit, memory, or error-correction estimate
  exists.
- There is no independent second implementation or third-party interoperability
  corpus beyond the in-repository Python reference.

## Reproduction

From `build/`, the locally reproduced set is:

```sh
make clean
make check
make check-extended
make check-reference
make check-trails
make check-characteristics
make check-reduced-rounds
make check-second-preimage-bounds
make bench-check
make bench-baseline-check
make timing-check
make analyze
make sanitizer-check SANITIZER_CC=gcc
```

`make fuzz-smoke` requires Clang with libFuzzer support. Performance values are
machine-specific; only deterministic outputs, resource invariants, schemas,
and environment-compatible comparisons should be treated as reproducible.
