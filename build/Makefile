CC = gcc
ANALYZER ?= gcc
PYTHON ?= python3
BENCHMARK_TOOL ?= ../tools/fch_benchmark.py
BASELINE ?= benchmark-baseline.json
CURRENT ?= benchmark-current.json
MAX_REGRESSION ?= 20
REDUCED_ROUND_TOOL ?= ../tools/fch_reduced_round_analysis.py
REDUCED_ROUND_BASELINE ?= ../analysis/reduced-round-v1.json
SECOND_PREIMAGE_TOOL ?= ../tools/fch_second_preimage_bounds.py
SECOND_PREIMAGE_BASELINE ?= ../analysis/fch512-second-preimage-v1.json
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
ANALYZEFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O1
SANITIZER_TARGETS ?= check check-extended
SANITIZER_CC ?= clang
SANITIZER_CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O1 -g \
                    -fsanitize=address,undefined \
                    -fno-omit-frame-pointer
SANITIZER_ASAN_OPTIONS ?= detect_leaks=1:abort_on_error=1
SANITIZER_UBSAN_OPTIONS ?= halt_on_error=1:print_stacktrace=1
CPPFLAGS = -I../include -I../src
DEPFLAGS ?= -MMD -MP
LDFLAGS =
LDLIBS =

SRC = ../src/fch.c \
      ../src/fractal_process.c \
      ../src/fractal_split.c \
      ../src/leaf.c \
      ../src/combine.c \
      ../src/mix.c \
      ../src/bitops.c \
      ../src/fch_stream.c

OBJ = $(patsubst ../src/%.c,obj/%.o,$(SRC))

export PYTHONDONTWRITEBYTECODE = 1

ANALYZE_SOURCES = $(SRC) \
        ../tools/fch.c \
        ../bench/bench_hash.c \
        ../tests/fuzz_stream.c \
        ../tests/fuzz_padding.c \
        ../tests/fuzz_tree_combine.c \
        ../tests/test_boundaries.c \
        ../tests/test_stream_equivalence.c \
        ../tests/test_invariants.c \
        ../tests/test_tree_encoding.c \
        ../tests/test_portability.c \
        ../tests/test_vectors.c \
        ../analysis/fch_tree_attacks.c \
        ../tests/fuzz_hash.c \
        ../tests/test_failures.c \
        ../tests/test_stress.c

ANALYZE_REDUCED_SOURCES = ../analysis/fch_diffusion.c \
        ../analysis/fch_cryptanalysis.c

TESTS = test_boundaries \
        test_stream_equivalence \
        test_invariants \
        test_tree_encoding \
        test_portability \
        test_vectors \
        test_failures

RESEARCH_TESTS = test_cryptanalysis test_tree_attacks

EXTENDED_TESTS = test_stress

FUZZ_TARGETS = fuzz_hash \
               fuzz_stream \
               fuzz_padding \
               fuzz_tree_combine

FUZZ_CC ?= clang
FUZZ_SECONDS ?= 300
FUZZ_CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O1 -g \
               -fsanitize=fuzzer,address,undefined \
               -fno-omit-frame-pointer
FUZZ_RUNS ?= 1024
FUZZ_TIMEOUT ?= 10
FUZZ_ASAN_OPTIONS ?= detect_leaks=1:abort_on_error=1
FUZZ_UBSAN_OPTIONS ?= halt_on_error=1:print_stacktrace=1

DEPS = $(OBJ:.o=.d) \
       fch.d \
       $(addsuffix .d,$(TESTS)) \
       $(addsuffix .d,$(RESEARCH_TESTS)) \
       $(addsuffix .d,$(EXTENDED_TESTS)) \
       fch_diffusion.d \
       bench_hash.d \
       $(addsuffix .d,$(FUZZ_TARGETS))

define generate-dependencies
	@$(1) $(CPPFLAGS) $(2) -MM -MP -MT $@ $(filter %.c,$^) > $@.d.tmp
	@mv $@.d.tmp $@.d
endef

obj:
	mkdir -p $@

obj/%.o: ../src/%.c | obj
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -MT $@ -c -o $@ $<

all: fch

fch: $(OBJ) ../tools/fch.c
	$(call generate-dependencies,$(CC),$(CFLAGS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(OBJ) ../tools/fch.c $(LDFLAGS) $(LDLIBS)

test: $(TESTS)

test_boundaries: ../tests/test_boundaries.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS) -DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_stream_equivalence: ../tests/test_stream_equivalence.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_invariants: ../tests/test_invariants.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_tree_encoding: ../tests/test_tree_encoding.c ../src/leaf.c ../src/combine.c ../src/fractal_split.c ../src/bitops.c
	$(call generate-dependencies,$(CC),$(CFLAGS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< ../src/leaf.c ../src/combine.c ../src/fractal_split.c ../src/bitops.c $(LDFLAGS) $(LDLIBS)

test_portability: ../tests/test_portability.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_vectors: ../tests/test_vectors.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

fch_diffusion: ../analysis/fch_diffusion.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS) -DFCH_ENABLE_REDUCED_ROUND_TESTS -DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_ENABLE_REDUCED_ROUND_TESTS \
		-DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL \
		-o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

check-diffusion: fch_diffusion
	./fch_diffusion

test_cryptanalysis: ../analysis/fch_cryptanalysis.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS) -DFCH_ENABLE_REDUCED_ROUND_TESTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_ENABLE_REDUCED_ROUND_TESTS -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_tree_attacks: ../analysis/fch_tree_attacks.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS) -DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_failures: ../tests/test_failures.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS) -Dmalloc=fch_test_malloc -Dcalloc=fch_test_calloc)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-Dmalloc=fch_test_malloc -Dcalloc=fch_test_calloc \
		-o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

check-cli: fch ../tests/test_cli.sh
	sh ../tests/test_cli.sh ./fch

check: test check-cli
	@set -e; for test_bin in $(TESTS); do ./$$test_bin; done

check-research-native: $(RESEARCH_TESTS)
	@set -e; for test_bin in $(RESEARCH_TESTS); do ./$$test_bin; done

check-research: check-research-native check-diffusion check-trails check-characteristics check-reduced-rounds check-second-preimage-bounds

check-all: check check-extended check-reference check-interoperability check-research bench-check bench-baseline-check

sanitizer-check:
	$(MAKE) clean
	ASAN_OPTIONS="$(SANITIZER_ASAN_OPTIONS)" \
	UBSAN_OPTIONS="$(SANITIZER_UBSAN_OPTIONS)" \
		$(MAKE) $(SANITIZER_TARGETS) \
		CC="$(SANITIZER_CC)" CFLAGS="$(SANITIZER_CFLAGS)"

check-interoperability: fch test_vectors
	./test_vectors ../analysis/interoperability-v1.tsv
	$(PYTHON) ../tools/fch_reference.py --check-c ./fch --check-corpus ../analysis/interoperability-v1.tsv

check-reference: fch
	$(PYTHON) ../tools/fch_reference.py --check-c ./fch

check-trails:
	$(PYTHON) ../tools/fch_trail_search.py

check-characteristics:
	$(PYTHON) ../tools/fch_characteristic_search.py

reduced-round-report:
	$(PYTHON) $(REDUCED_ROUND_TOOL) capture \
		--output "$(REDUCED_ROUND_BASELINE)"

check-reduced-rounds:
	$(PYTHON) -m unittest discover \
		-s ../tests -p 'test_reduced_round_analysis.py'
	$(PYTHON) $(REDUCED_ROUND_TOOL) check \
		--baseline "$(REDUCED_ROUND_BASELINE)"

second-preimage-report:
	$(PYTHON) $(SECOND_PREIMAGE_TOOL) capture \
		--output "$(SECOND_PREIMAGE_BASELINE)"

check-second-preimage-bounds:
	$(PYTHON) -m unittest discover \
		-s ../tests -p 'test_second_preimage_bounds.py'
	$(PYTHON) $(SECOND_PREIMAGE_TOOL) check \
		--baseline "$(SECOND_PREIMAGE_BASELINE)"

test-extended: $(EXTENDED_TESTS)

test_stress: ../tests/test_stress.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

check-extended: test-extended
	./test_stress --big-mb 1 --iters-big 1 --small-kb 4 --iters-small 16 --quiet

bench: bench_hash

bench_hash: ../bench/bench_hash.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS) -Dmalloc=fch_bench_malloc -Dcalloc=fch_bench_calloc -Dfree=fch_bench_free)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-Dmalloc=fch_bench_malloc -Dcalloc=fch_bench_calloc \
		-Dfree=fch_bench_free -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

bench-check: bench_hash
	./bench_hash --quick

bench-baseline: bench_hash
	$(PYTHON) $(BENCHMARK_TOOL) capture \
		--binary ./bench_hash \
		--compiler "$(CC)" \
		--cflags "$(CFLAGS)" \
		--output "$(BASELINE)"

bench-compare: bench_hash
	$(PYTHON) $(BENCHMARK_TOOL) compare \
		--binary ./bench_hash \
		--compiler "$(CC)" \
		--cflags "$(CFLAGS)" \
		--baseline "$(BASELINE)" \
		--output-current "$(CURRENT)" \
		--max-regression "$(MAX_REGRESSION)"

bench-baseline-check:
	$(PYTHON) -m unittest discover \
		-s ../tests -p 'test_benchmark_baseline.py'

timing-check: bench_hash
	./bench_hash --timing-check

fuzz_hash: ../tests/fuzz_hash.c $(SRC)
	$(call generate-dependencies,$(FUZZ_CC),$(FUZZ_CFLAGS))
	$(FUZZ_CC) $(CPPFLAGS) $(FUZZ_CFLAGS) \
		-o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

fuzz_stream: ../tests/fuzz_stream.c $(SRC)
	$(call generate-dependencies,$(FUZZ_CC),$(FUZZ_CFLAGS))
	$(FUZZ_CC) $(CPPFLAGS) $(FUZZ_CFLAGS) \
		-o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

fuzz_padding: ../tests/fuzz_padding.c $(SRC)
	$(call generate-dependencies,$(FUZZ_CC),$(FUZZ_CFLAGS))
	$(FUZZ_CC) $(CPPFLAGS) $(FUZZ_CFLAGS) \
		-o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

fuzz_tree_combine: ../tests/fuzz_tree_combine.c $(SRC)
	$(call generate-dependencies,$(FUZZ_CC),$(FUZZ_CFLAGS))
	$(FUZZ_CC) $(CPPFLAGS) $(FUZZ_CFLAGS) \
		-o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

fuzz-smoke: $(FUZZ_TARGETS)
	ASAN_OPTIONS="$(FUZZ_ASAN_OPTIONS)" UBSAN_OPTIONS="$(FUZZ_UBSAN_OPTIONS)" \
		$(PYTHON) ../tools/fch_fuzz.py --mode smoke --runs $(FUZZ_RUNS) \
		--timeout $(FUZZ_TIMEOUT) --compiler "$(FUZZ_CC)" --cflags "$(FUZZ_CFLAGS)"

fuzz-campaign: $(FUZZ_TARGETS)
	ASAN_OPTIONS="$(FUZZ_ASAN_OPTIONS)" UBSAN_OPTIONS="$(FUZZ_UBSAN_OPTIONS)" \
		$(PYTHON) ../tools/fch_fuzz.py --mode campaign --runs 0 \
		--seconds $(FUZZ_SECONDS) --timeout $(FUZZ_TIMEOUT) \
		--compiler "$(FUZZ_CC)" --cflags "$(FUZZ_CFLAGS)"

analyze:
	@set -e; for source in $(ANALYZE_SOURCES); do \
		$(ANALYZER) $(CPPFLAGS) $(ANALYZEFLAGS) -fanalyzer \
			-c "$$source" -o /dev/null; \
	done; for source in $(ANALYZE_REDUCED_SOURCES); do \
		$(ANALYZER) $(CPPFLAGS) $(ANALYZEFLAGS) -fanalyzer \
			-DFCH_ENABLE_REDUCED_ROUND_TESTS \
			-c "$$source" -o /dev/null; \
	done

BINARIES = fch $(TESTS) $(RESEARCH_TESTS) $(EXTENDED_TESTS) fch_diffusion bench_hash $(FUZZ_TARGETS)

clean:
	rm -f $(OBJ) $(DEPS) $(addsuffix .tmp,$(DEPS)) $(BINARIES) $(addsuffix .exe,$(BINARIES)) test_vectors_ubsan test_vectors_san

-include $(DEPS)

.PHONY: check-interoperability check-research-native check-research check-all all test check-cli check check-diffusion sanitizer-check check-reference check-trails check-characteristics reduced-round-report check-reduced-rounds second-preimage-report check-second-preimage-bounds test-extended check-extended bench bench-check bench-baseline bench-compare bench-baseline-check timing-check fuzz-smoke fuzz-campaign analyze clean
