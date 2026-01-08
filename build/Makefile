CC = gcc
CFLAGS = -Wall -Wextra -O2 -I../include

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
	rm -f $(OBJ) fch
