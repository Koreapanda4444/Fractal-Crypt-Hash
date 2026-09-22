CC = gcc
ANALYZER ?= gcc
PYTHON ?= python3
BENCHMARK_TOOL ?= ../tools/fch_benchmark.py
BASELINE ?= benchmark-baseline.json
CURRENT ?= benchmark-current.json
MAX_REGRESSION ?= 20
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
ANALYZEFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -O1
SANITIZER_CC ?= clang
SANITIZER_CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O1 -g \
                    -fsanitize=address,undefined \
                    -fno-omit-frame-pointer
SANITIZER_ASAN_OPTIONS ?= detect_leaks=0:abort_on_error=1
SANITIZER_UBSAN_OPTIONS ?= halt_on_error=1:print_stacktrace=1
CPPFLAGS = -I../include
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

OBJ = $(SRC:.c=.o)

ANALYZE_SOURCES = $(SRC) \
        ../tools/fch.c \
        ../bench/bench_hash.c \
        ../tests/fuzz_stream.c \
        ../tests/fuzz_padding.c \
        ../tests/fuzz_tree_combine.c \
        ../tests/fuzz_cli.c \
        ../tests/test_consistency.c \
        ../tests/test_boundaries.c \
        ../tests/test_tree_boundaries.c \
        ../tests/test_stream_equivalence.c \
        ../tests/test_invariants.c \
        ../tests/test_tree_encoding.c \
        ../tests/test_portability.c \
        ../tests/test_vectors.c \
        ../tests/test_tree_attacks.c \
        ../tests/test_hardening.c \
        ../tests/test_failures.c \
        ../tests/test_length_variation.c \
        ../tests/test_depth_diffusion.c \
        ../tests/test_stress.c \
        ../tests/test_split_sensitivity.c

ANALYZE_REDUCED_SOURCES = ../tests/test_avalanche.c \
        ../tests/test_cryptanalysis.c

TESTS = test_consistency \
        test_boundaries \
        test_tree_boundaries \
        test_stream_equivalence \
        test_invariants \
        test_tree_encoding \
        test_portability \
        test_vectors \
        test_avalanche \
        test_cryptanalysis \
        test_tree_attacks \
        test_hardening \
        test_failures

EXTENDED_TESTS = test_length_variation \
                 test_depth_diffusion \
                 test_stress \
                 test_split_sensitivity

FUZZ_TARGETS = fuzz_hash \
               fuzz_stream \
               fuzz_padding \
               fuzz_tree_combine \
               fuzz_cli

FUZZ_CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O1 -g \
               -fsanitize=fuzzer,address,undefined \
               -fno-omit-frame-pointer
FUZZ_RUNS ?= 1024
FUZZ_TIMEOUT ?= 10
FUZZ_ASAN_OPTIONS ?= detect_leaks=0:abort_on_error=1
FUZZ_UBSAN_OPTIONS ?= halt_on_error=1:print_stacktrace=1

DEPS = $(OBJ:.o=.d) \
       fch.d \
       $(addsuffix .d,$(TESTS)) \
       $(addsuffix .d,$(EXTENDED_TESTS)) \
       bench_hash.d \
       $(addsuffix .d,$(FUZZ_TARGETS))

define generate-dependencies
	@$(1) $(CPPFLAGS) $(2) -MM -MP -MT $@ $(filter %.c,$^) > $@.d.tmp
	@mv $@.d.tmp $@.d
endef

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -MF $(@:.o=.d) -MT $@ -c -o $@ $<

all: fch

fch: $(OBJ) ../tools/fch.c
	$(call generate-dependencies,$(CC),$(CFLAGS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(OBJ) ../tools/fch.c $(LDFLAGS) $(LDLIBS)

test: $(TESTS)

test_consistency: ../tests/test_consistency.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_boundaries: ../tests/test_boundaries.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_tree_boundaries: ../tests/test_tree_boundaries.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS) -DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL \
		-o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

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

test_avalanche: ../tests/test_avalanche.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS) -DFCH_ENABLE_REDUCED_ROUND_TESTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_ENABLE_REDUCED_ROUND_TESTS -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_cryptanalysis: ../tests/test_cryptanalysis.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS) -DFCH_ENABLE_REDUCED_ROUND_TESTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_ENABLE_REDUCED_ROUND_TESTS -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_tree_attacks: ../tests/test_tree_attacks.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS) -DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_hardening: ../tests/test_hardening.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS) -DFCH_FUZZ_STANDALONE)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_FUZZ_STANDALONE -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_failures: ../tests/test_failures.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS) -Dmalloc=fch_test_malloc -Dcalloc=fch_test_calloc)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-Dmalloc=fch_test_malloc -Dcalloc=fch_test_calloc \
		-o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

check-cli: fch ../tests/test_cli.sh
	sh ../tests/test_cli.sh ./fch

check: test check-cli
	@set -e; for test_bin in $(TESTS); do ./$$test_bin; done

sanitizer-check:
	$(MAKE) clean
	ASAN_OPTIONS="$(SANITIZER_ASAN_OPTIONS)" \
	UBSAN_OPTIONS="$(SANITIZER_UBSAN_OPTIONS)" \
		$(MAKE) check check-extended \
		CC="$(SANITIZER_CC)" CFLAGS="$(SANITIZER_CFLAGS)"

check-reference: fch
	$(PYTHON) ../tools/fch_reference.py --check-c ./fch

check-trails:
	$(PYTHON) ../tools/fch_trail_search.py

check-characteristics:
	$(PYTHON) ../tools/fch_characteristic_search.py

test-extended: $(EXTENDED_TESTS)

test_length_variation: ../tests/test_length_variation.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_depth_diffusion: ../tests/test_depth_diffusion.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS) -DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_stress: ../tests/test_stress.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

test_split_sensitivity: ../tests/test_split_sensitivity.c $(SRC)
	$(call generate-dependencies,$(CC),$(CFLAGS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

check-extended: test-extended
	./test_length_variation
	./test_depth_diffusion
	./test_stress --big-mb 1 --iters-big 1 --small-kb 4 --iters-small 16 --quiet
	./test_split_sensitivity

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

fuzz_hash: ../tests/test_hardening.c $(SRC)
	$(call generate-dependencies,clang,$(FUZZ_CFLAGS))
	clang $(CPPFLAGS) $(FUZZ_CFLAGS) \
		-o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

fuzz_stream: ../tests/fuzz_stream.c $(SRC)
	$(call generate-dependencies,clang,$(FUZZ_CFLAGS))
	clang $(CPPFLAGS) $(FUZZ_CFLAGS) \
		-o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

fuzz_padding: ../tests/fuzz_padding.c $(SRC)
	$(call generate-dependencies,clang,$(FUZZ_CFLAGS))
	clang $(CPPFLAGS) $(FUZZ_CFLAGS) \
		-o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

fuzz_tree_combine: ../tests/fuzz_tree_combine.c $(SRC)
	$(call generate-dependencies,clang,$(FUZZ_CFLAGS))
	clang $(CPPFLAGS) $(FUZZ_CFLAGS) \
		-o $@ $< $(SRC) $(LDFLAGS) $(LDLIBS)

fuzz_cli: ../tests/fuzz_cli.c ../tools/fch.c ../tools/fch_cli.h $(SRC)
	$(call generate-dependencies,clang,$(FUZZ_CFLAGS) -DFCH_CLI_NO_MAIN)
	clang $(CPPFLAGS) $(FUZZ_CFLAGS) -DFCH_CLI_NO_MAIN \
		-o $@ ../tests/fuzz_cli.c ../tools/fch.c $(SRC) \
		$(LDFLAGS) $(LDLIBS)

fuzz-smoke: $(FUZZ_TARGETS)
	ASAN_OPTIONS="$(FUZZ_ASAN_OPTIONS)" UBSAN_OPTIONS="$(FUZZ_UBSAN_OPTIONS)" \
		./fuzz_hash -runs=$(FUZZ_RUNS) -timeout=$(FUZZ_TIMEOUT) -max_len=65536 -seed=164981
	ASAN_OPTIONS="$(FUZZ_ASAN_OPTIONS)" UBSAN_OPTIONS="$(FUZZ_UBSAN_OPTIONS)" \
		./fuzz_stream -runs=$(FUZZ_RUNS) -timeout=$(FUZZ_TIMEOUT) -max_len=65536 -seed=271828
	ASAN_OPTIONS="$(FUZZ_ASAN_OPTIONS)" UBSAN_OPTIONS="$(FUZZ_UBSAN_OPTIONS)" \
		./fuzz_padding -runs=$(FUZZ_RUNS) -timeout=$(FUZZ_TIMEOUT) -max_len=4096 -seed=314159
	ASAN_OPTIONS="$(FUZZ_ASAN_OPTIONS)" UBSAN_OPTIONS="$(FUZZ_UBSAN_OPTIONS)" \
		./fuzz_tree_combine -runs=$(FUZZ_RUNS) -timeout=$(FUZZ_TIMEOUT) -max_len=4096 -seed=141421
	ASAN_OPTIONS="$(FUZZ_ASAN_OPTIONS)" UBSAN_OPTIONS="$(FUZZ_UBSAN_OPTIONS)" \
		./fuzz_cli -runs=$(FUZZ_RUNS) -timeout=$(FUZZ_TIMEOUT) -max_len=65536 -seed=173205

analyze:
	@set -e; for source in $(ANALYZE_SOURCES); do \
		$(ANALYZER) $(CPPFLAGS) $(ANALYZEFLAGS) -fanalyzer \
			-c "$$source" -o /dev/null; \
	done; for source in $(ANALYZE_REDUCED_SOURCES); do \
		$(ANALYZER) $(CPPFLAGS) $(ANALYZEFLAGS) -fanalyzer \
			-DFCH_ENABLE_REDUCED_ROUND_TESTS \
			-c "$$source" -o /dev/null; \
	done

clean:
	rm -f $(OBJ) $(DEPS) $(addsuffix .tmp,$(DEPS)) fch $(TESTS) $(EXTENDED_TESTS) bench_hash $(FUZZ_TARGETS) test_vectors_ubsan test_vectors_san

-include $(DEPS)

.PHONY: all test check-cli check sanitizer-check check-reference check-trails check-characteristics test-extended check-extended bench bench-check bench-baseline bench-compare bench-baseline-check timing-check fuzz-smoke analyze clean
