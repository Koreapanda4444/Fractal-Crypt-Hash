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
| Fixed points and two-cycles | 4,096 samples for 4, 8, and 16-round cores, plus both complete hashes | No tested fixed point or two-cycle was found |
| Near collisions | All pairs among 2,048 64-byte messages | No exact collision; minimum distances were 90 bits for FCH-256 and 199 bits for FCH-512 |

The one-round result is intentionally reported as weak rather than hidden. It
shows that the harness distinguishes a clearly under-diffused core from later
rounds. Near-50% diffusion after two rounds is not a security margin on its own:
the eight-round reference remains the reduced-round boundary, and the deployed
core adds eight more rounds.

## Tree-mode results

| Check | Coverage | Result |
| ----- | -------- | ------ |
| Truncated multicollision screen | 4,096 leaf, node, and root states with 20-bit buckets | Expected truncated-prefix pairs occurred; no exact state collision was found |
| Canonical shape validation | Leaf counts 3 through 16 | All 14 canonical layouts accepted; 119 alternative partitions rejected |
| Malformed tree rejection | Reordering, forged ranges, gaps, overlaps, depth changes, and invalid children | Six malformed shapes, eight invalid replacements, and three graft attempts rejected or detected |
| Second-preimage screen | 512 candidates, eight mutation modes, 16 KiB target | No match; minimum distances were 103 bits for FCH-256 and 227 bits for FCH-512 |
| Long-message screen | Fifteen variants of a 256 KiB message | No collision; minimum distances were 108 bits for FCH-256 and 229 bits for FCH-512 |
| Depth diffusion | 128 bit changes through an 8 KiB tree | Leaf, intermediate-node, and root averages remained close to 50% |

The 20-bit bucket pairs in the multicollision screen are expected birthday
events in a deliberately truncated view. The relevant result is that none of
those pairs became an exact 512-bit state collision. This is only a bounded
screen and is not a multicollision-resistance proof.

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
| Long-message and multi-target security loss | Not yet quantified |

The argument narrows the remaining question: an attack cannot rely only on an
ambiguous tree representation, but it may still exploit the compression core,
the way records are absorbed, truncation, or generic tree-hash strategies.

## Implementation evidence

The security tests are backed by implementation checks that keep the analyzed
algorithm and the shipped code aligned:

- fixed vectors and 96 C/Python reference comparisons;
- one-shot and streaming equivalence across boundary and chunk patterns;
- explicit little-endian serialization checks, including big-endian CI;
- rejection of allocation, reader, overflow, and API-lifecycle failures;
- AddressSanitizer, UndefinedBehaviorSanitizer, and libFuzzer smoke runs;
- GCC path-sensitive static analysis with warnings treated as errors;
- an 8 MiB bounded-memory streaming test; and
- time, allocation-count, and peak-heap scaling checks in CI.

Passing these checks means the tested implementation behaved consistently. It
does not turn implementation coverage into a cryptographic proof.

## Open analysis

The most important remaining work is:

1. independent review of the specification, constants, and domain layout;
2. a quantitative reduction for collision and second-preimage preservation,
   including exact tree-size loss and long-message bounds beyond the
   conditional localization argument above;
3. automated differential and linear trail searches with tools such as
   MILP, SAT, or SMT, especially across reduced rounds;
4. dedicated rotational, additive-differential, rebound, meet-in-the-middle,
   and related-domain analysis of the ARX core;
5. full-tree study of multicollisions, expandable messages, herding,
   multi-target attacks, and state reuse between FCH-256 and FCH-512;
6. larger fuzzing campaigns, broader static analysis, timing review, and
   side-channel evaluation of optimized implementations; and
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
make bench-check
make fuzz-smoke
make analyze
```

`test_cryptanalysis` and `test_tree_attacks` print the measured bounds and
sample counts. Their pseudorandom inputs use fixed seeds, so the same source and
parameters produce the same analysis data.
