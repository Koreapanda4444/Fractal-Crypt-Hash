# ROADMAP (Research)

This roadmap focuses on making FCH a stronger **research/portfolio** project (not a production cryptographic primitive).

## Near-term (1–2 weeks)

- Add fixed test vectors (known input -> known output) for both FCH-256 and FCH-512
- Add a streaming API (`init/update/final`) to avoid hashing only via one-shot buffers
- Add a small CLI tool (`fch`) that can hash files/stdin and print hex
- Add fuzzing harnesses (libFuzzer/AFL-style entry points) and run with sanitizers

## Mid-term (1–2 months)

- Differential testing against an independent implementation (second implementation or “spec interpreter”)
- Automated search tools for:
  - collisions in reduced rounds/parameters
  - near-collisions
  - structural weaknesses related to splitting patterns
- Benchmark suite that reports throughput across input sizes and compares variants

## Long-term

- Formalize a threat model and a narrow set of claims for academic-style evaluation
- Invite third-party review / cryptanalysis attempts and track findings publicly
