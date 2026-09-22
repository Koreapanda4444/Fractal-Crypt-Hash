# FCH Security Analysis

This report records the analysis currently available for tree encoding version
2 and the 16-round FCH compression core. It is meant to make the evidence,
limits, and open work visible in one place. The Korean version is available in
[security_analysis.ko.md](security_analysis.ko.md).

The classical target strengths remain those defined by the specification:

| Variant | Collision | Preimage | Second preimage |
| ------- | --------- | -------- | --------------- |
| FCH-256 | 2^128 | 2^256 | 2^256 |
| FCH-512 | 2^256 | 2^512 | 2^512 |

These are classical design targets. The results below are deterministic,
bounded tests; they are useful for finding regressions and weak reduced-round
behavior, but do not prove the target costs or replace independent
cryptanalysis. Quantum query scales are stated separately below.

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
| Full-state characteristics | All 1,024 single-bit message differences over 32 bases through rounds 1 to 8 | One round was weak; from round 2 every state and output word was active, and no exact full-state trajectory repeated across bases |
| Integrated round profile | 256 diffusion pairs, 1,024 XOR-differential pairs, 896 rotational pairs, and 896 structured outputs at every round from 1 through 16 | One round was weak; all 150 checked round/family results passed from round 2 onward |
| Rotation-related patterns | Six 4,096-candidate pattern sets | One round was weak; tested sets had all eight state words active from round 2 onward |
| Rotational pairs | 3,072 pairs at 1, 2, 4, 8, and 16 rounds over six word rotations | No exact relation; round averages stayed between 49.95% and 50.02% |
| Additive differentials | 2,048 pairs at 1, 2, 4, 8, and 16 rounds over four modular input differences | One round was weak; from round 2 all output words were active and the maximum bit bias was 4.44% |
| Projected differential probability | Eight XOR characteristics with 4,096 samples each at 1, 2, 4, 8, and 16 rounds | The largest observed 16-bit projection probability was 0.0977%; no zero output difference occurred |
| Related contexts | 4,096 pairs per round over eight counter, domain, and flag relations | One round was weak; from round 2 all output words were active and the maximum bit bias was 2.95% |
| Rebound-style inbound screen | 4,095 nonzero 12-bit message differences at 4, 8, and 16 rounds, split after 2, 4, and 8 rounds | Minimum middle-state weights were 373, 455, and 460 of 1,024 bits; no candidate was at or below 256 bits |
| Meet-in-the-middle screen | 4,096 candidates over an 8-round core split 4+4 with a 24-bit middle-state projection | One projected pair and one exact pair occurred; the known 12-bit target was the only exact match |
| Output-only attack screen | 65,536 candidates at 4, 8, and 16 rounds with planted and unplanted 512-bit targets | Only the planted candidate matched exactly; no unplanted preimage or full collision occurred, and every nonmatch was at least 201 bits away |
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
| 5 | 227 | 227 | 27.34% | 29.69% |
| 6 | 233 | 234 | 26.56% | 28.13% |
| 7 | 231 | 229 | 26.56% | 28.91% |
| 8 | 224 | 223 | 28.13% | 29.69% |

The same outputs are checked with 896 structured multi-bit masks. Half are
contiguous 2-, 4-, and 8-bit fields; the other half select the same bit from
non-overlapping groups of 2, 4, or 8 output words. The linear screen compares
the parity of every output mask with all 255 nonzero input-byte masks. The
differential screen projects the 128 distinct XOR pairs in each family and
records the largest bucket. The table reports the maximum over both families
for each round.

| Rounds | 2-bit maximum bucket | 4-bit maximum bucket | 8-bit maximum bucket | Maximum multi-bit correlation |
| ------ | -------------------- | -------------------- | -------------------- | ----------------------------- |
| 1 | 100.00% | 100.00% | 100.00% | 100.00% |
| 2 | 69.53% | 32.81% | 6.25% | 33.59% |
| 3 | 40.63% | 16.41% | 4.69% | 29.69% |
| 4 | 38.28% | 15.63% | 4.69% | 30.47% |
| 5 | 41.41% | 14.84% | 3.91% | 28.91% |
| 6 | 39.06% | 17.97% | 4.69% | 30.47% |
| 7 | 37.50% | 14.06% | 4.69% | 30.47% |
| 8 | 38.28% | 14.06% | 3.91% | 27.34% |

The second round retains a visible 2-bit concentration in one input family.
From round 3 onward, none of the tested projections was constant. These values
are maxima selected after testing hundreds of masks, so they are not directly
comparable with the expected frequency of one preselected random bucket.

No zero-output difference occurred in these sixteen exhaustive searches. All
eight output words were active from round 2 onward. The 1-round result again
exposes a weak trail instead of treating early diffusion as security evidence.

The concrete evaluator is checked against the 16-round Python reference before
each run. A second implementation models the same ARX operations as 64-bit Z3
bit vectors and replays every reported minimum-weight witness. CI rejects a
disagreement between the two models. These are exact results only inside the
declared 8-bit families and structured output masks; they do not bound wider
input differences, unstructured or higher-weight output masks, or arbitrary
characteristics of the 8- and 16-round cores.

### Full-state characteristic search

`tools/fch_characteristic_search.py` extends the concrete search from two
8-bit families to every single-bit difference in the complete 1,024-bit
message block. It applies each of the 1,024 differences to 32 deterministic
base messages and records the full 1,024-bit working-state difference after
every round from 1 through 8. This gives 32,768 message pairs per round.

An exact characteristic here is the entire sequence of working-state
differences from the first round through the reported round. The final column
counts how many of the 32 bases produced the most common exact sequence for one
input difference.

| Rounds | Minimum state weight | Active state words | Minimum output weight | Active output words | Largest exact count |
| ------ | -------------------- | ------------------ | --------------------- | ------------------- | ------------------- |
| 1 | 4 | 4 | 4 | 4 | 20/32 |
| 2 | 357 | 16 | 223 | 8 | 1/32 |
| 3 | 449 | 16 | 245 | 8 | 1/32 |
| 4 | 445 | 16 | 233 | 8 | 1/32 |
| 5 | 443 | 16 | 225 | 8 | 1/32 |
| 6 | 445 | 16 | 245 | 8 | 1/32 |
| 7 | 445 | 16 | 249 | 8 | 1/32 |
| 8 | 446 | 16 | 252 | 8 | 1/32 |

The one-round result confirms a sparse deterministic path for some message
bits. From round 2 onward, every tested pair activated all 16 working-state
words and all eight compression-output words. No two bases produced the same
complete characteristic prefix for a fixed input difference at those rounds.

The evaluator is checked against the 16-round Python reference before the
search starts. These results are an empirical screen of single-bit input
differences and 32 fixed bases. They are not probabilities or upper bounds for
arbitrary, chosen, or higher-weight characteristics, and they do not replace a
solver-assisted search. The CI thresholds of 256 state bits, 128 output bits,
full word activation, and no repeated trajectory from round 2 are conservative
regression alarms rather than claimed security bounds.

### Integrated round-by-round experiments

`tools/fch_reduced_round_analysis.py` puts four experiment classes on the same
fixed 1-through-16-round axis. The diffusion screen applies 64 stratified bit
positions to four bases, covering low, 31st, 32nd, and high bits in every
message word. The differential screen applies eight single-, multi-bit,
cross-word, full-word, and alternating-word XOR differences to 128 bases. It
records exact output differences and four fixed 12-bit projections. The
rotational screen compares seven rotations over 128 bases, including the odd
7-bit distance not used by the earlier screen. The structural screen evaluates
128 distinct messages in each of seven families: random, sparse zero, sparse
one, repeated word, repeated byte, counter, and alternating patterns.

Every pair experiment records the complete 1,024-bit working-state relation and
the 512-bit feed-forward output relation. The structural experiment records
per-bit and per-word balance, projected buckets, and complete-output
uniqueness. The selected summary below shows the largest projection or bias
after searching the declared sets.

| Rounds | Diffusion output average | Differential 12-bit maximum | Rotational average deviation | Structural maximum bit bias |
| ------ | ------------------------ | --------------------------- | ---------------------------- | --------------------------- |
| 1 | 17.832947% | 100.000000% | 0.320435% | 24.218750% |
| 2 | 49.947357% | 2.343750% | 0.225830% | 14.062500% |
| 4 | 49.906158% | 1.562500% | 0.234985% | 14.843750% |
| 8 | 50.074005% | 1.562500% | 0.244141% | 16.406250% |
| 12 | 49.820709% | 1.562500% | 0.329590% | 17.187500% |
| 16 | 50.007629% | 2.343750% | 0.350952% | 15.625000% |

The one-round diffusion set contained a four-bit output difference, and one
chosen differential fixed an entire tested 12-bit projection, so round one is
explicitly marked `WEAK`. Across rounds 2 through 16, the diffusion output
average stayed between 49.794769% and 50.247192%; every pair activated all 16
working-state words and all eight output words. The lowest observed state and
output weights were 395 of 1,024 and 233 of 512 bits. No exact differential
repeated within a 128-base chosen-difference set, no exact rotational relation
occurred, and every structural family produced 128 distinct outputs. The
largest later 12-bit bucket contained 3/128 samples; the largest structural
bit and word-mean biases were 19.531250% and 1.843262%.

The profile, thresholds, and all 160 result rows are stored in
`analysis/reduced-round-v1.json`. `make check-reduced-rounds` runs focused unit
tests, validates the stored schema, regenerates the results, and requires an
exact match. These thresholds detect an implementation or experiment drift;
they are not probability estimates, security bounds, or evidence that
untested differential, rotational, or structural families are absent. The
maxima also reflect selection over many bits, projections, and families and
must not be read as single preselected statistical tests.

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
optimized inbound phase.

### Output-only attack search

The output-only screen removes the known-internal-state assumption. It varies
two fixed message bytes to enumerate a complete 16-bit space and observes only
the 512-bit compression output. The same 65,536 candidates are tested against
a planted target from inside the space and an unplanted target that differs in
a third byte. The planted case confirms that the search can recover an exact
match; the unplanted case checks for an unexpected preimage in the declared
space.

The search also groups every candidate by the first 24 output bits and checks
each group for complete 512-bit collisions. Hamming distance is measured from
both targets without reading or reconstructing the 1,024-bit work state.

| Rounds | Planted exact matches | Unplanted exact matches | 24-bit collision pairs | Largest bucket | Full collisions | Minimum target distance |
| ------ | --------------------- | ----------------------- | ---------------------- | -------------- | --------------- | ----------------------- |
| 4 | 1 | 0 | 129 | 2 | 0 | 207 |
| 8 | 1 | 0 | 143 | 2 | 0 | 201 |
| 16 | 1 | 0 | 104 | 2 | 0 | 207 |

In each planted case, the only exact match was the original candidate. The
24-bit pairs are expected truncation collisions and none extended to the full
output. This is exhaustive only for the fixed 16-bit family. It does not show
a shortcut over enumeration, estimate full-domain preimage or collision cost,
or bound stronger rebound, meet-in-the-middle, splice, and output-only attacks.

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

The localization argument does not assign a cost by itself. The following
section adds a conservative union bound under an explicit ideal-map model. It
does not prove that the real compression construction satisfies that model.

### Quantitative conditional bounds

For a message of `L` bytes, define:

```text
P = max(64, L + 9)
N = ceil(P / 1024)
b = P - 1024(N - 1)
D = ceil(log2(N))
T = 2N - 1
```

`N` is the number of leaves, `b` is the final leaf length, `D` is the root
level, and `T` counts all leaf and internal-node map evaluations in the target
tree. Because every internal node has two children, a tree with `N` leaves has
exactly `N - 1` internal nodes.

The exact number of compression calls for one complete hash is:

```text
C(L) = 13N - 11 + ceil(b / 120)
```

Each full leaf costs one header and nine data records, the final leaf costs one
header and `ceil(b / 120)` data records, every internal node costs three
records, and output finalization costs one record.

The formulas for `N`, `T`, and `C(L)` are format facts. The security accounting
below is conditional: it treats `Leaf` and `Node` as independent random maps to
512 bits on distinct, well-formed typed inputs, and each `d`-bit `Output`
variant as an independent random map, where `d` is 256 or 512. It also assumes
there is no shortcut inside the compression construction. Domains and tags
motivate this model but do not prove it.

Let `H` be the number of distinct finalized-output inputs evaluated by an
attacker and `Q` the total number of distinct `Leaf` and `Node` inputs it
evaluates. For a fixed target, its already known tree states are not included
in `Q`. Define `kappa` as the greatest number of target states compatible with
the role and complete descriptor of any one attacker query. A direct union
bound gives the following advantages, with every right-hand side capped at 1:

```text
Adv_collision(H, Q) <= H(H - 1) / 2^(d + 1)
                       + Q(Q - 1) / 2^513

Adv_second-preimage(H, Q; kappa)
    <= H / 2^d + kappa Q / 2^512

Adv_preimage(H) <= H / 2^d
```

For `r` distinct targets, let `kappa_r` be the maximum number of their states
that share the role and descriptor of one query. The corresponding
multi-target bound is:

```text
Adv_multi-target(H, Q; r, kappa_r)
    <= rH / 2^d + kappa_r Q / 2^512
```

Every subtree in one canonical target has a unique tuple of role, level, leaf
range, and byte range. At the first divergent node in two equal-root
computations, the candidate and target descriptors must also be equal.
Consequently `kappa = 1` for descriptor-compatible substitution into one
canonical target, and `kappa_r <= r` for `r` targets. Replacing `kappa` with
the raw state count `T` discards the encoded position constraints and is only
a descriptor-relaxed bound; it is not the bound for the format implemented
here.

These expressions give the following conditional scales. They are not
constructive attacks:

- digest collisions reach the birthday scale near `H = 2^(d/2)`;
- internal-state collisions reach it near `Q = 2^256`;
- a fixed-target second preimage requires about `H = 2^d` through finalization
  or about `Q = 2^512` through descriptor-compatible tree maps; and
- `r` targets change those scales to about `2^d / r` and
  `2^512 / kappa_r`.

Tree size still affects complete-message query count. If `q` distinct
candidates have the target's canonical geometry, then `H <= q` and `Q <= qT`.
For FCH-512 this gives

```text
Adv_second-preimage(q) <= q(T + 1) / 2^512.
```

The unit-advantage union-bound scale is therefore about
`q = 2^512 / (T + 1)` **complete-message candidates**. Those candidates
perform up to `q(T + 1)` typed-map evaluations, so the corresponding primitive
map scale remains about `2^512`. With straightforward full hashing, the serial
compression-call proxy is `qC(L)`. The reduction in candidate count is not a
reduction to `2^512 / T` primitive evaluations.

| Original length `L` | Leaves `N` | Tree maps `T` | Calls `C(L)` | `log2(q)` complete candidates | `log2(H + Q)` typed maps | `log2(qC(L))` serial proxy |
| ------------------- | ---------- | ------------- | ------------ | ----------------------------- | ------------------------- | ---------------------------- |
| 0 bytes | 1 | 1 | 3 | 511.00 | 512.00 | 512.58 |
| 1 KiB | 2 | 3 | 16 | 510.00 | 512.00 | 514.00 |
| 1 MiB | 1,025 | 2,049 | 13,315 | 501.00 | 512.00 | 514.70 |
| 1 GiB | 1,048,577 | 2,097,153 | 13,631,491 | 491.00 | 512.00 | 514.70 |
| `2^61 - 1` bytes | `2^51 + 1` | `2^52 + 1` | 29,273,397,577,908,227 | about 460.00 | 512.00 | about 514.70 |

The `q` column is an algebraic union-bound scale and is meaningful only when
the candidate domain contains that many distinct messages; it is not an
attack. For FCH-256, the `q / 2^256` output term remains larger than the
`qT / 2^512` internal term at every format-valid length. For FCH-512, the
conditional descriptor-compatible typed-map scale is length independent at
`2^512`, while the number and cost of complete-message queries remain
length dependent.

Long-message second-preimage shortcuts are known for iterated Merkle-Damgard
hashes; see Kelsey and Schneier, [Second Preimages on n-bit Hash Functions for
Much Less than 2^n Work](https://www.schneier.com/wp-content/uploads/2016/02/paper-preimages.pdf).
That construction depends on an iterative chain and is not claimed to apply
directly to FCH's canonical, position-bound tree. Position binding blocks the
specific free interchange of chain states, but no reduction rules out a
different long-message attack on FCH.

`tools/fch_second_preimage_bounds.py` reproduces the table, checks padding and
tree boundaries through the format maximum, and enumerates representative
canonical trees to confirm descriptor uniqueness. The fixed report is stored
in `analysis/fch512-second-preimage-v1.json`; run
`make check-second-preimage-bounds` from `build/` to validate it. This is an
accounting regression check, not cryptanalytic evidence. Likewise, the
512-candidate second-preimage screen above is an observed bounded result and
does not establish any exponent in this section.

### Boundary of the argument

| Statement | Status |
| --------- | ------ |
| Padding and canonical-tree uniqueness | Established by the format |
| Rejection of alternate shapes and positions | Established by the format and implementation checks |
| Unique descriptors and `kappa = 1` for one canonical target | Established by the format; representative trees are regression-tested |
| Localization of a digest collision or second preimage | Established as a logical implication of the encoding |
| Resistance of the localized typed maps | Not established for the real compression function |
| Independence created by domains and tags | Design assumption, not a proof |
| The numerical targets in the specification | Recovered only as conditional ideal-map cost scales |
| 512-candidate and long-message screens | Observed bounded results, not asymptotic bounds |
| Long-message and multi-target tightness | Open; the parameterized conditional bounds are given above |

The argument narrows the remaining question: an attack cannot rely only on an
ambiguous tree representation, but it may still exploit the compression core,
the way records are absorbed, truncation, or generic tree-hash strategies.

## Quantum security scope

This section defines an attack model and generic baselines. It does not claim
that FCH has been proved secure against quantum attacks.

### Model and cost unit

FCH is still treated as a public, unkeyed hash. The attacker is given coherent
superposition access to a reversible implementation of the complete hash or,
where stated, one of the typed `Leaf`, `Node`, and `Output` maps. The figures
below count quantum oracle queries only. They do not count logical or physical
gates, circuit depth, qubits, quantum memory, error correction, or the cost of
constructing a reversible FCH implementation.

One query to a complete hash must reversibly perform its compression calls and
uncompute temporary state. Its real cost therefore depends on the message
length and on the reversible circuit, even when it counts as one query in this
model. No such circuit or resource estimate is available yet.

The baselines treat the typed maps as independent random functions. The domain
tags are intended to separate them, but do not prove that model. There is also
no reduction for FCH in the quantum random-oracle model and no quantum
structural analysis of the ARX core or tree mode.

### Generic query scales

[Grover's search algorithm](https://arxiv.org/abs/quant-ph/9605043) gives a
square-root speedup for an unstructured target search. A generic preimage, or a
second preimage for one fixed finalized output, therefore takes about
`2^(d/2)` quantum queries for a `d`-bit ideal output.

The [Brassard-Høyer-Tapp collision
algorithm](https://arxiv.org/abs/quant-ph/9705002) gives the generic
`2^(d/3)` query scale for collisions in a `d`-bit ideal output. Its query count
does not describe the full implementation cost: the original algorithm uses
substantial storage and gives a time-space tradeoff.

| Variant | Generic collision queries | Generic preimage queries | Fixed second-preimage queries |
| ------- | ------------------------- | ------------------------ | ----------------------------- |
| FCH-256 | about `2^85.33` | about `2^128` | about `2^128` complete-hash queries |
| FCH-512 | about `2^170.67` | about `2^256` | about `2^256` by direct complete-hash search; an optimistic opportunity scale of `2^256 / sqrt(T + 1)`; or `2^256` descriptor-compatible typed-map queries |

These are ideal-map query scales, not measured attack costs or security proofs.
The 512-bit internal maps also have a generic collision scale of about
`2^(512/3) = 2^170.67` queries. Thus FCH-256 does not have a 128-bit generic
quantum collision exponent: its 256-bit output gives about 85.33 bits. FCH-512
has a generic collision exponent of about 170.67 bits in this model.
The complete-message opportunity scale in the final column is explained
below. Unlike direct Grover search on the final digest, it is not an attack
guaranteed by generic search alone.

### Tree targets and long messages

For one typed-map query, the role and complete descriptor select at most one
state in a canonical target. Allowing the descriptor itself to vary creates
one matching target per descriptor, but expands the searched domain by the
same factor. The marked fraction therefore remains `2^-512`. Grover search on
this descriptor-compatible typed-map domain has the conditional scale

```text
Q_descriptor-map = 2^256.
```

A complete-message query is a different unit. One candidate computes `T`
tree maps and one output map, so the classical union bound is at most
`(T + 1) / 2^512` for FCH-512. If a sufficiently large search family actually
realizes that many independently searchable marked opportunities, amplitude
amplification would have the optimistic scale

```text
Q_complete-hash = 2^256 / sqrt(T + 1).
```

The union bound alone does not establish that marked density or construct such
a family; direct Grover search on the finalized FCH-512 digest remains the
generic `2^256` complete-hash route. At the format maximum, `T = 2^52 + 1`, so
the optimistic expression is about `2^230` **complete-hash oracle queries**,
not typed-map queries. If realized, each query would contain `C(L)` compression
calls and would have to be implemented reversibly. Multiplying only as a
serial-call proxy gives an exponent near `284.70`, before accounting for
uncomputation, circuit depth, qubits, or memory. It is neither a gate estimate
nor a concrete attack. FCH-256 remains governed by its
approximately `2^128` output search because the internal 512-bit term is
larger at every valid length.

For `r` distinct target digests, generic output search becomes
`2^(d/2) / sqrt(r)`. For typed maps, replace `r` by `kappa_r`, the number of
target states sharing one query's role and descriptor; the scale is
`2^256 / sqrt(kappa_r)`, with `kappa_r <= r`, when the corresponding target
set is coherently searchable. A complete-message opportunity calculation may
again count many events per query, but realizing that bound requires a search
family and every query must evaluate its whole tree. These expressions must be
capped when the target set is no longer sparse.

No NIST post-quantum category is assigned from these exponents. Such a label
would require concrete gate, depth, memory, and failure-cost estimates as well
as analysis of FCH itself, not just black-box query complexity.

## Implementation evidence

The security tests are backed by implementation checks that keep the analyzed
algorithm and the shipped code aligned:

- fixed vectors and 96 C/Python reference comparisons;
- exhaustive 8-bit reduced-round searches with Z3 witness replay;
- one-shot and streaming equivalence across boundary and chunk patterns;
- explicit little-endian serialization checks, including big-endian CI;
- rejection of allocation, reader, overflow, and API-lifecycle failures;
- AddressSanitizer, UndefinedBehaviorSanitizer, and five focused libFuzzer smoke targets;
- GCC path-sensitive static analysis over 31 source and test translation units
  with warnings treated as errors;
- an 8 MiB bounded-memory streaming test; and
- scaling plus same-length content timing, allocation-count, and peak-heap checks in CI.

Passing these checks means the tested implementation behaved consistently. It
does not turn implementation coverage into a cryptographic proof.

### Fuzzing, static analysis, and timing review

The standalone hardening test now runs 1,024 pseudorandom cases up to 64 KiB
and 140 structured cases at 35 boundary lengths. The structured inputs cover
zeros, ones, an index-derived sequence, and an alternating pattern. CI also
runs separate sanitizer-backed libFuzzer targets for core hashing, streaming
partitions, padding boundaries, canonical tree combination, and CLI input
handling. Each target receives 1,024 runs under its own fixed seed, for 5,120
requested runs in total, with a 10-second timeout on each individual input.
The padding target maps compact control inputs onto message lengths through
16,385 bytes so the marker and length-field transitions around minimum-padding
and tree-leaf boundaries are exercised directly.

The GCC path-sensitive analyzer previously covered the eight library sources
and the command-line tool. It now also checks the benchmark and 21 test
translation units, including four focused fuzz targets and two reduced-round
tests under their required build flag. Expanding the scope found an
allocation-failure leak in the split sensitivity test; that path now frees
either successful allocation before returning. The library code was unchanged
by this fix.

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

## Independent review checklist

An external review must name one exact commit. Record the commit SHA, platform,
compiler, and tool versions before starting. If the target changes, the review
continues to apply only to the recorded commit until the affected work is
checked again.

FCH has not completed an independent cryptographic review merely because this
checklist exists or the bundled tests pass. Completion requires a reviewer who
was not involved in the design to examine a fixed target and report the scope,
methods, findings, and limits of that review.

### Review map

Read the documents in this order: `fch_spec.md`, `implementation_notes.md`, and
this report. Then trace each claim into the implementation and its tests.

| Area | Primary files |
| ---- | ------------- |
| Parameters, rounds, constants, and compression | `include/params.h`, `src/mix.c`, `src/bitops.c` |
| Typed records, leaves, nodes, and canonical splitting | `src/leaf.c`, `src/combine.c`, `src/fractal_split.c`, `src/fractal_process.c` |
| Public API, finalization, and streaming | `src/fch.c`, `src/fch_stream.c`, `include/fch.h`, `include/fch_stream.h` |
| Reference model and automated searches | `tools/fch_reference.py`, `tools/fch_trail_search.py`, `tools/fch_characteristic_search.py`, `tools/fch_reduced_round_analysis.py`, `tools/fch_second_preimage_bounds.py`, `analysis/`, `tests/` |

The Python reference is useful for comparison, but it was developed in the
same repository and is not an independent specification. Agreement between C
and Python can still preserve a shared design error.

### Questions the review must challenge

1. **Compression core:** look for differential, linear, rotational, additive,
   rebound, meet-in-the-middle, fixed-point, invariant-subspace, and symmetry
   properties. State whether a result covers the full 16 rounds or only a
   reduced-round variant, and assess whether the eight-round margin is credible.
2. **Encoding:** verify injectivity of padding and every typed record. Check that
   roles, child order, levels, byte and leaf ranges, original length, output
   size, and finalization cannot be confused or omitted.
3. **Tree mode:** challenge the collision and second-preimage localization
   argument, descriptor-compatible target counting, the `kappa = 1` result,
   and the separation between complete-message and typed-map costs. Consider multicollisions, expandable messages,
   herding, grafting, long messages, multi-target attacks, and cross-variant
   reuse.
4. **Output:** inspect feed-forward, truncation, FCH-256/FCH-512 separation, and
   any path that exposes or reuses a pre-output state more cheaply than the
   stated model assumes.
5. **Implementation:** compare every serialized field with the specification.
   Check integer limits, allocation failures, ownership, aliasing, undefined
   behavior, endianness, one-shot and streaming equivalence, and large-input
   behavior on more than one compiler and architecture.
6. **Quantum scope:** test the reversible-oracle assumptions, the cost hidden by
   one query, coherent target membership, memory requirements, and whether a
   structural quantum attack invalidates the generic black-box baselines.

Passing an existing test is not a reason to close a question. A reviewer should
change the input families, seeds, projections, round splits, compiler, and
platform where that can expose assumptions built into the current harness.

### Evidence and reporting

A cryptanalytic finding should identify the exact function and attack model,
give time, data, memory, and query costs, and separate full-round results from
reduced-round evidence. An implementation finding should include the smallest
reproducer available, the command and environment used, and the expected and
observed behavior. A failed proof argument should name the unsupported step
even when no practical attack is known.

Use the following fields so findings can be compared and retested:

```text
ID:
Title:
Target commit:
Severity:
Affected claim:
Files or functions:
Model and assumptions:
Reproduction or derivation:
Impact:
Suggested fix:
Status:
```

| Severity | Meaning in this project |
| -------- | ----------------------- |
| Critical | A reproducible full-round break far below a stated target, or an exploitable implementation flaw that defeats the hash result |
| High | A credible full-round shortcut, encoding ambiguity, or structural flaw with direct security impact |
| Medium | A substantial reduced-round weakness, proof gap, or implementation divergence that changes the claimed margin |
| Low | A hardening, portability, documentation, or reproducibility defect without a demonstrated cryptographic break |
| Informational | A useful observation, negative result, or recommendation with no current security impact |

Review completion requires a pinned target, an explicit list of files and
claims examined, reproduction of the relevant test suite, disposition of every
finding as open, fixed, accepted, or rejected with a reason, and retesting of
all fixes. The final report must also state what was not reviewed.

## Open analysis

The most important remaining work is:

1. complete the independent review checklist against a pinned commit and
   publish the resulting scope, findings, dispositions, and unresolved limits;
2. a formal reduction from the real compression construction to the ideal
   typed-map model, including adaptive-query tightness, descriptor-compatible
   target counting, and whether complete-message query savings can become a
   primitive-work shortcut;
3. expand the automated trail search to wider input spaces and unstructured or
   higher-weight output masks, then use MILP, SAT, or SMT to search general
   5- through 8-round characteristics beyond the two fixed 8-bit families;
4. extend the new fixed chosen-difference experiments to solver-assisted
   searches and quantitative bounds, extend rebound and meet-in-the-middle
   analysis to optimized inbound solving and independent neutral variables,
   and move the output-only screen beyond its fixed 16-bit family;
5. construct attacks or tighter bounds for multicollision, expandable-message,
   herding, multi-target, and cross-variant settings, and determine whether the
   union bounds above are tight;
6. long-running external fuzzing, hardware-counter timing studies, and
   side-channel evaluation of future optimized implementations; and
7. reversible-circuit gate, depth, qubit, and ancilla estimates; memory-aware
   collision tradeoffs; quantum structural attacks on the compression and tree
   modes; and a proof or reduction in the quantum random-oracle model.

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
make check-characteristics
make check-reduced-rounds
make check-second-preimage-bounds
make bench-check
make bench-baseline-check
make timing-check
make fuzz-smoke
make analyze
```

`test_cryptanalysis` and `test_tree_attacks` print the measured bounds and
sample counts. Their pseudorandom inputs use fixed seeds, so the same source and
parameters produce the same analysis data.
