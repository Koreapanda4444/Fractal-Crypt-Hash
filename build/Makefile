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
        test_avalanche

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
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

check: test
	@set -e; for test_bin in $(TESTS); do ./$$test_bin; done

test-extended: $(EXTENDED_TESTS)

test_length_variation: ../tests/test_length_variation.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

test_depth_diffusion: ../tests/test_depth_diffusion.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DFCH_DEBUG_HOOKS -o $@ $^ $(LDFLAGS) $(LDLIBS)

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

clean:
	rm -f $(OBJ) fch $(TESTS) $(EXTENDED_TESTS) bench_hash test_vectors_ubsan test_vectors_san

.PHONY: all test check test-extended check-extended bench clean
