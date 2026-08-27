# FCH Security Analysis

This report records the analysis currently available for tree encoding version
2 and the 16-round FCH compression core. It is meant to make the evidence,
limits, and open work visible in one place. The Korean version is available in
[security_analysis.ko.md](security_analysis.ko.md).

The target strengths remain those defined by the specification:

| Variant | Collision | Preimage | Second preimage |
| ------- | --------- | -------- | --------------- |
| FCH-256 | 2^128 | 2^256 | 2^256 |
| FCH-512 | 2^256 | 2^512 | 2^512 |

These are design targets. The results below are deterministic, bounded tests;
they are useful for finding regressions and weak reduced-round behavior, but do
not prove the target costs or replace independent cryptanalysis.

## Scope

The analysis assumes a public, non-keyed hash. An attacker may choose messages,
evaluate the function freely, and use the full specification and source code.
Results apply to the exact parameters in `fch_spec.md`: a 512-bit state,
16-round ARX compression, fixed 1,024-byte leaves, canonical binary tree
encoding version 2, and separate output-finalization domains.

Any change to the rounds, constants, domains, records, padding, leaf span, or
tree schedule requires this report to be rerun and reviewed.

## Why the current structure was chosen

| Component | Intended effect |
| --------- | --------------- |
| 512-bit internal state | Avoid an internal-state collision bound below the FCH-512 collision target |
| 16 full rounds | Keep an eight-round margin above the current eight-round analysis reference |
| Typed 128-byte records | Separate headers, payloads, child states, and finalization data |
| Domain and flag words | Keep leaves, nodes, root finalization, and output sizes from sharing the same compression context |
| Canonical binary tree | Give every padded length one tree and remove content-controlled splits |
| Position and range fields | Bind a subtree to its level, leaf interval, byte interval, and child order |
| Length-bearing padding | Distinguish message boundaries and bind the original bit length |

This structure removes several forms of ambiguity by construction. It does not
by itself establish collision, preimage, or second-preimage resistance; those
properties still depend on the compression core and the complete tree mode.

## Compression-core results

The current deterministic run produced the following results:

| Check | Search size | Result |
| ----- | ----------- | ------ |
| Round avalanche | 256 single-bit trials at 4, 8, 12, and 16 rounds | Average changed bits stayed between 49.86% and 50.06% |
| Differential bias | 2,048 samples at four input-bit positions for 8 and 16 rounds | 49.97% and 49.99% average; maximum per-bit bias 3.42% and 3.27% |
| Linear correlation | 8,192 inputs and 32 masks for 8 and 16 rounds | Maximum absolute correlation 3.32% and 2.27% |
| Low-weight trails | 24,576 candidates at every round from 1 through 16 | One round was weak; at 8 and 16 rounds the minimum output weights were 212 and 210 of 512 bits |
| Rotation-related patterns | Six 4,096-candidate pattern sets | One round was weak; tested sets had all eight state words active from round 2 onward |
| Rotational pairs | 3,072 pairs at 1, 2, 4, 8, and 16 rounds over six word rotations | No exact relation; round averages stayed between 49.95% and 50.02% |
| Additive differentials | 2,048 pairs at 1, 2, 4, 8, and 16 rounds over four modular input differences | One round was weak; from round 2 all output words were active and the maximum bit bias was 4.44% |
| Projected differential probability | Eight XOR characteristics with 4,096 samples each at 1, 2, 4, 8, and 16 rounds | The largest observed 16-bit projection probability was 0.0977%; no zero output difference occurred |
| Related contexts | 4,096 pairs per round over eight counter, domain, and flag relations | One round was weak; from round 2 all output words were active and the maximum bit bias was 2.95% |
| Rebound-style inbound screen | 4,095 nonzero 12-bit message differences at 4, 8, and 16 rounds, split after 2, 4, and 8 rounds | Minimum middle-state weights were 373, 455, and 460 of 1,024 bits; no candidate was at or below 256 bits |
| Meet-in-the-middle screen | 4,096 candidates over an 8-round core split 4+4 with a 24-bit middle-state projection | One projected pair and one exact pair occurred; the known 12-bit target was the only exact match |
| Fixed points and two-cycles | 4,096 samples for 4, 8, and 16-round cores, plus both complete hashes | No tested fixed point or two-cycle was found |
| Near collisions | All pairs among 2,048 64-byte messages | No exact collision; minimum distances were 90 bits for FCH-256 and 199 bits for FCH-512 |

The one-round result is intentionally reported as weak rather than hidden. It
shows that the harness distinguishes a clearly under-diffused core from later
rounds. Near-50% diffusion after two rounds is not a security margin on its own:
the eight-round reference remains the reduced-round boundary, and the deployed
core adds eight more rounds.

### Automated reduced-round trail search

`tools/fch_trail_search.py` examines two fixed 8-bit input families. One
replaces the low byte of message word 0 and applies XOR difference `0x01`; the
other replaces the high byte of word 15 and applies `0x80`. For each family and
round count, all 256 byte values are evaluated. The search records the lightest
512-bit output difference and checks every nonzero input-byte mask against
every single output bit with an exact Walsh transform.

| Rounds | Word 0 minimum weight | Word 15 minimum weight | Word 0 maximum correlation | Word 15 maximum correlation |
| ------ | --------------------- | ---------------------- | -------------------------- | --------------------------- |
| 1 | 178 | 4 | 100.00% | 100.00% |
| 2 | 232 | 222 | 28.13% | 37.50% |
| 3 | 226 | 217 | 29.69% | 31.25% |
| 4 | 231 | 223 | 30.47% | 29.69% |

No zero-output difference occurred in these eight exhaustive searches. All
eight output words were active from round 2 onward. The 1-round result again
exposes a weak trail instead of treating early diffusion as security evidence.

The concrete evaluator is checked against the 16-round Python reference before
each run. A second implementation models the same ARX operations as 64-bit Z3
bit vectors and replays every reported minimum-weight witness. CI rejects a
disagreement between the two models. These are exact results only inside the
declared 8-bit families; they do not bound wider differentials, multi-bit
linear masks, or the 8- and 16-round cores.

### Rotational and additive screens

The rotational-pair screen rotates every 64-bit message word by 1, 8, 16, 24,
32, or 63 bits and compares the resulting compression output with the same
rotation applied to the original output. Across 3,072 pairs per reported round,
no exact rotational relation occurred. Every output word was active, the
minimum distance was at least 210 bits, and the largest deviation of a
per-rotation average from 50% was 0.21%.

The additive screen changes message words 0, 5, 10, and 15 with four modular
differences chosen to exercise short carries, a 32-bit boundary, the top bit,
and repeated-byte carries. One round remained visibly weak: its average was
20.74%, its minimum distance was 14 bits, and some pairs activated only four
output words. From round 2 onward, the average stayed between 49.93% and
50.02%, every pair activated all eight output words, and the minimum distance
was at least 206 bits. No tested pair produced a zero output difference.

These checks cover the compression function with its fixed IV, domains,
counters, and flags. Those constants help break simple word-wise rotational
symmetry, so this result does not exclude internal rotational characteristics,
deeper related-tweak attacks, or high-probability additive trails outside the
sampled differences.

### Differential probability and related contexts

The empirical probability screen uses four single-bit and four two-bit XOR
input characteristics. Each characteristic is sampled 4,096 times, and each
512-bit output difference is mapped through four fixed 16-bit projections. The
largest bucket contained four samples, or 0.0977%, at rounds 2, 4, 8, and 16;
the one-round maximum was three samples, or 0.0732%. No pair produced a zero
512-bit output difference.

This is a reproducible screen for conspicuous differential concentration. It
does not estimate the probability of a complete 512-bit characteristic, and a
16-bit projection cannot provide an upper bound for an untested full-state
trail.

The related-context screen keeps the 128-byte block fixed while changing one of
eight public compression contexts. The pairs cover adjacent and high-bit
counters, leaf, node, and output domains and flags, FCH-256 versus FCH-512
output contexts, a one-bit domain relation, and a combined change. One round
was weak with a 43.14% average, a 76-bit minimum distance, and 22.78% maximum
bit bias. From round 2 onward, averages stayed between 49.92% and 49.99%, every
output word was active, minimum distance was at least 211 bits, and maximum bit
bias was at most 2.95%. No related pair produced the same output.

Domains, counters, and flags are fixed by valid FCH encodings rather than
chosen through the public hash API. This test checks separation between those
internal contexts; it is not a related-key proof or a bound on rebound and
meet-in-the-middle attacks.

### Rebound and meet-in-the-middle screens

The reduced-round test build now exposes preparation, forward-round, and
inverse-round operations for the internal 1,024-bit work state. The inverse was
checked over a complete eight-round path and a four-round window beginning at
round 2. Returning to the exact starting state is required, and the forward
state is also checked against the normal compression output. These functions
are compiled only with `FCH_ENABLE_REDUCED_ROUND_TESTS` and do not change the
production hash interface.

The rebound-style screen changes 12 message bits spread across the first and
last block bytes and enumerates all 4,095 nonzero differences. It measures the
full work-state difference at 2+2, 4+4, and 8+8 round splits. The lightest
middle states had weights 373, 455, and 460 of 1,024 bits. Every middle and end
state activated all 16 words, no difference was zero, and no middle state had
weight at or below 256 bits. The corresponding minimum end-state weights were
455, 460, and 462 bits.

The meet-in-the-middle screen uses a deliberately small 12-bit candidate space
and an eight-round core split after round 4. It stores 4,096 forward middle
states, reverses four rounds from a known full internal target for another
4,096 candidates, and first matches a 24-bit projection before checking all
1,024 bits. The run produced one projected pair and one exact pair, recovering
only the planted candidate.

This recovery is a consistency check, not a preimage attack on FCH. It assumes
the complete internal target state and all but 12 message bits are known. The
same candidate must be evaluated on both sides because every round injects all
16 message words, so this construction costs 4,096 forward and 4,096 backward
evaluations without an independent early/late variable split. The rebound
screen likewise enumerates a fixed difference family rather than solving an
optimized inbound phase. Neither result bounds stronger rebound,
meet-in-the-middle, splice, or output-only attacks.

## Tree-mode results

| Check | Coverage | Result |
| ----- | -------- | ------ |
| Truncated multicollision screen | 4,096 leaf, node, and root states with 20-bit buckets | Expected truncated-prefix pairs occurred; no exact state collision was found |
| Canonical shape validation | Leaf counts 3 through 16 | All 14 canonical layouts accepted; 119 alternative partitions rejected |
| Malformed tree rejection | Reordering, forged ranges, gaps, overlaps, depth changes, and invalid children | Six malformed shapes, eight invalid replacements, and three graft attempts rejected or detected |
| Second-preimage screen | 512 candidates, eight mutation modes, 16 KiB target | No match; minimum distances were 103 bits for FCH-256 and 227 bits for FCH-512 |
| Long-message screen | Fifteen variants of a 256 KiB message | No collision; minimum distances were 108 bits for FCH-256 and 229 bits for FCH-512 |
| Expandable-message splice screen | 96 short/long pairs sharing a 2 KiB suffix, with one to four inserted leaves | No root or digest match; minimum distances were 216 root bits, 108 FCH-256 bits, and 225 FCH-512 bits |
| Herding convergence screen | 256 distinct one-leaf prefixes followed by the same 3 KiB suffix | No exact root or digest collision; minimum digest distances were 95 and 207 bits |
| Multi-target screen | 256 candidates compared with 64 targets, for 16,384 digest comparisons | No match; minimum distances were 91 and 204 bits |
| FCH-256/FCH-512 reuse screen | 256 messages from 0 to 8 KiB | All pre-output roots were shared as designed, while no FCH-256 output matched either 256-bit half of FCH-512 |
| Depth diffusion | 128 bit changes through an 8 KiB tree | Leaf, intermediate-node, and root averages remained close to 50% |

The 20-bit bucket pairs in the multicollision screen are expected birthday
events in a deliberately truncated view. The relevant result is that none of
those pairs became an exact 512-bit state collision. This is only a bounded
screen and is not a multicollision-resistance proof.

The expandable-message screen keeps the prefix and final 2 KiB suffix fixed,
then inserts between one and four full leaves before that suffix. None of the
96 short/long pairs reused a raw root or either digest. The herding screen uses
256 different first leaves followed by one fixed 3 KiB continuation. Its
20-bit root projection had no repeated bucket in this run, and no complete
root or digest converged.

The multi-target screen hashes 64 independent 4 KiB targets and checks 256
mutated candidates against every target. This gives 16,384 comparisons for
each output size. No candidate matched any target. These are fixed,
deterministic searches; they do not construct compression-function collisions,
optimize bridge blocks, or measure the asymptotic cost of expandable-message,
diamond, herding, or multi-target attacks.

FCH-256 and FCH-512 intentionally start output finalization from the same tree
root. The output record then separates them by output size and domain. Across
256 messages, every pre-output root was identical between variants, but the
FCH-256 digest never equaled either 256-bit half of the FCH-512 digest. The
minimum distances were 107 and 106 bits, with averages of 49.52% and 49.69%.
This confirms the implemented domain separation on the sampled messages; it is
not a proof that attacks cannot reuse work across the two variants.

## Conditional tree-security argument

The tree mode can be separated into three typed maps:

- `Leaf(descriptor, bytes)` produces a 512-bit leaf state;
- `Node(descriptor, left descriptor, left state, right descriptor, right
  state)` produces a 512-bit internal state; and
- `Output(variant, root descriptor, root state, original length, padded
  length)` produces the requested digest.

These names describe the complete record sequences, including their domains,
flags, counters, and final bits. For FCH-256, `Output` includes truncation to
256 bits. For FCH-512, it returns the complete finalized state.

The following three properties come directly from the format:

1. **Padding is injective.** The final eight bytes encode the original bit
   length, and the preceding `0x80` separates the message from the zero fill.
   Two valid padded strings can be equal only when their original lengths and
   messages are equal.
2. **The tree is unique.** The padded length fixes the leaf count. The rule
   that assigns the largest power of two below `n` to the left child fixes
   every split recursively. Induction on `n` therefore gives one tree and one
   descriptor for every subtree range.
3. **Records have an unambiguous role and position.** Leaf, node, child, and
   output records have different tags, domains, and flags. Descriptors commit
   to level, leaf range, byte range, and child index. Reordering, moving, or
   regrouping a state changes at least one encoded input field.

These are encoding properties. They do not assume that different records
produce different states; that is the cryptographic property that still has
to be analyzed.

### Collision localization

Assume two distinct messages `M` and `M'` produce the same digest for one
variant. Compare the complete inputs to their `Output` maps.

- If those inputs differ, the pair is already a collision in the finalized
  output map. For FCH-256 this includes collisions caused by the intended
  256-bit truncation.
- If the output inputs are equal, their root descriptors and root states are
  equal. Compare the two canonical trees from the root downward. Whenever two
  different child tuples produce the same parent state, they give a collision
  in `Node`. If all compared node inputs are equal, the comparison eventually
  reaches the first differing leaf. Padding injectivity guarantees that such
  a leaf exists, and equal leaf states then give a collision in `Leaf`.

Consequently, alternate shapes, child reordering, subtree grafting, and
message-boundary ambiguity do not create a free way to obtain the same digest.
A tree-hash collision must localize to the finalized output map or to a typed
leaf or node map. This conclusion is conditional on those maps resisting the
corresponding attacks.

### Second-preimage localization

Fix a target message `M`. Any distinct `M'` with the same digest follows the
same cases: it either forms a second preimage for the finalized output map or
forces a collision at the first divergent leaf or node. The canonical schedule
prevents an attacker from presenting a different parse of the target tree as
the same encoded computation.

This is not yet a quantitative second-preimage bound. A complete reduction
must account for the number of leaves and internal nodes, adaptive queries,
multicollision construction, expandable-message and herding strategies, and
possible reuse between the two output variants. The current argument also does
not establish preimage resistance.

### Boundary of the argument

| Statement | Status |
| --------- | ------ |
| Padding and canonical-tree uniqueness | Established by the format |
| Rejection of alternate shapes and positions | Established by the format and implementation checks |
| Localization of a digest collision or second preimage | Conditional on the typed maps |
| Independence created by domains and tags | Design assumption, not a proof |
| The numerical targets in the specification | Not established by this argument |
| Long-message and multi-target security loss | Bounded screens only; not quantified |

The argument narrows the remaining question: an attack cannot rely only on an
ambiguous tree representation, but it may still exploit the compression core,
the way records are absorbed, truncation, or generic tree-hash strategies.

## Implementation evidence

The security tests are backed by implementation checks that keep the analyzed
algorithm and the shipped code aligned:

- fixed vectors and 96 C/Python reference comparisons;
- exhaustive 8-bit reduced-round searches with Z3 witness replay;
- one-shot and streaming equivalence across boundary and chunk patterns;
- explicit little-endian serialization checks, including big-endian CI;
- rejection of allocation, reader, overflow, and API-lifecycle failures;
- AddressSanitizer, UndefinedBehaviorSanitizer, and three-seed libFuzzer smoke runs;
- GCC path-sensitive static analysis over 25 source and test translation units with warnings treated as errors;
- an 8 MiB bounded-memory streaming test; and
- scaling plus same-length content timing, allocation-count, and peak-heap checks in CI.

Passing these checks means the tested implementation behaved consistently. It
does not turn implementation coverage into a cryptographic proof.

### Fuzzing, static analysis, and timing review

The standalone hardening test now runs 1,024 pseudorandom cases up to 64 KiB
and 140 structured cases at 35 boundary lengths. The structured inputs cover
zeros, ones, an index-derived sequence, and an alternating pattern. CI also
runs the sanitizer-backed libFuzzer target for 2,048 cases under each of three
fixed seeds with a 64 KiB maximum input, for 6,144 requested runs in total.

The GCC path-sensitive analyzer previously covered the eight library sources
and the command-line tool. It now also checks the benchmark and 15 test
translation units, including the two reduced-round tests under their required
build flag. Expanding the scope found an allocation-failure leak in the split
sensitivity test; that path now frees either successful allocation before
returning. The library code was unchanged by this fix.

The timing check hashes four different 64 KiB content patterns through both
one-shot and 1 KiB streaming paths for FCH-256 and FCH-512. Each pattern is
measured in seven interleaved trials of 16 hashes. It requires identical
allocation counts and peak heap use for every same-length pattern and rejects
a maximum-to-minimum median time ratio above 1.50. The current run passed all
four paths; its largest ratio was 1.316.

This is a regression screen for obvious content-dependent behavior, not a
constant-time certification. FCH is an unkeyed hash and its message is assumed
public. The check does not cover keyed constructions, compiler-generated
instruction differences, cache and branch hardware counters, electromagnetic
or power leakage, or a future optimized implementation.

## Open analysis

The most important remaining work is:

1. independent review of the specification, constants, and domain layout;
2. a quantitative reduction for collision and second-preimage preservation,
   including exact tree-size loss and long-message bounds beyond the
   conditional localization argument above;
3. expand the automated trail search to wider input spaces, multi-bit output
   masks, and rounds 5 through 8 with MILP, SAT, or SMT;
4. replace the projected empirical probabilities with full-state
   characteristic searches and quantitative bounds, then extend the bounded
   rebound and meet-in-the-middle screens to optimized inbound solving,
   independent neutral variables, and output-only targets;
5. turn the bounded full-tree screens into quantitative multicollision,
   expandable-message, herding, multi-target, and cross-variant bounds;
6. long-running external fuzzing, hardware-counter timing studies, and
   side-channel evaluation of future optimized implementations; and
7. a separate quantum attack model before making quantum security targets.

Negative results from the bundled searches should be treated as starting
points for these tasks, not as evidence that stronger attacks do not exist.

## Reproducing the checks

From the `build` directory:

```sh
make clean
make check
make check-extended
make check-reference
make check-trails
make bench-check
make timing-check
make fuzz-smoke
make analyze
```

`test_cryptanalysis` and `test_tree_attacks` print the measured bounds and
sample counts. Their pseudorandom inputs use fixed seeds, so the same source and
parameters produce the same analysis data.
