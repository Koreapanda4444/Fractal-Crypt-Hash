CC = gcc
CFLAGS = -Wall -Wextra -O2 -I../include

BENCH_LDFLAGS =

SRC = ../src/fch.c \
      ../src/fractal_process.c \
      ../src/fractal_split.c \
      ../src/leaf.c \
      ../src/combine.c \
      ../src/sbox.c \
      ../src/bitops.c

OBJ = $(SRC:.c=.o)

all: fch

fch: $(OBJ)
	$(CC) $(CFLAGS) -o fch $(OBJ)

clean:
	rm -f $(OBJ) fch test_avalanche test_consistency test_boundaries test_invariants bench_hash

bench: bench_hash

bench_hash: ../bench/bench_hash.c
	$(CC) $(CFLAGS) ../bench/bench_hash.c -o bench_hash $(BENCH_LDFLAGS)

test:
	$(CC) $(CFLAGS) \
		../tests/test_avalanche.c \
		../src/*.c \
		-o test_avalanche

	$(CC) $(CFLAGS) \
		../tests/test_consistency.c \
		../src/*.c \
		-o test_consistency

	$(CC) $(CFLAGS) \
		../tests/test_boundaries.c \
		../src/*.c \
		-o test_boundaries

	$(CC) $(CFLAGS) \
		../tests/test_invariants.c \
		../src/*.c \
		-o test_invariants
