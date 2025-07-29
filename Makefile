CC = gcc
CFLAGS = -O3 -O2 -Wall -Wextra -fPIC

OBJ = mem_utils.o

# Threaded build
ifeq ($(THREADS),1)
    # Add the -pthread flag to both compiler and linker flags.
    # This is the most portable way to handle pthreads.
    CFLAGS += -pthread
endif

all: libmem_utils.a

mem_utils.o: mem_utils.c mem_utils.h
	$(CC) $(CFLAGS) -c mem_utils.c

libmem_utils.a: $(OBJ)
	ar rcs $@ $^

clean:
	rm -f *.o *.a