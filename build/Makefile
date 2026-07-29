CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
CPPFLAGS = -I../include
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

TESTS = test_consistency \
        test_boundaries \
        test_invariants \
        test_portability \
        test_vectors \
        test_avalanche \
        test_cryptanalysis \
        test_tree_attacks \
        test_hardening

EXTENDED_TESTS = test_length_variation \
                 test_depth_diffusion \
                 test_stress \
                 test_split_sensitivity

all: fch

fch: $(OBJ) ../tools/fch.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(OBJ) ../tools/fch.c $(LDFLAGS) $(LDLIBS)

test: $(TESTS)

test_consistency: ../tests/test_consistency.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_boundaries: ../tests/test_boundaries.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_invariants: ../tests/test_invariants.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_portability: ../tests/test_portability.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_vectors: ../tests/test_vectors.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_avalanche: ../tests/test_avalanche.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_ENABLE_REDUCED_ROUND_TESTS -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_cryptanalysis: ../tests/test_cryptanalysis.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_ENABLE_REDUCED_ROUND_TESTS -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_tree_attacks: ../tests/test_tree_attacks.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_hardening: ../tests/test_hardening.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_FUZZ_STANDALONE -o $@ $^ $(LDFLAGS) $(LDLIBS)

check: test
	@set -e; for test_bin in $(TESTS); do ./$$test_bin; done

test-extended: $(EXTENDED_TESTS)

test_length_variation: ../tests/test_length_variation.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_depth_diffusion: ../tests/test_depth_diffusion.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_DEBUG_HOOKS -DFCH_DEBUG_HOOK_EXTERNAL -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_stress: ../tests/test_stress.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

test_split_sensitivity: ../tests/test_split_sensitivity.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS) -lm

check-extended: test-extended
	./test_length_variation
	./test_depth_diffusion
	./test_stress --variant 16 --big-mb 1 --iters-big 1 --small-kb 4 --iters-small 16 --quiet
	./test_split_sensitivity

bench: bench_hash

bench_hash: ../bench/bench_hash.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

fuzz_hash: ../tests/test_hardening.c $(SRC)
	clang $(CPPFLAGS) -std=c11 -Wall -Wextra -Wpedantic -O1 -g \
		-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
		-o $@ $^ $(LDFLAGS) $(LDLIBS)

fuzz-smoke: fuzz_hash
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
		./fuzz_hash -runs=512 -max_len=8192 -seed=164981

clean:
	rm -f $(OBJ) fch $(TESTS) $(EXTENDED_TESTS) bench_hash fuzz_hash test_vectors_ubsan test_vectors_san

.PHONY: all test check test-extended check-extended bench fuzz-smoke clean
