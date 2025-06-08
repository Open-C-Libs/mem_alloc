CC = gcc
CFLAGS = -O3 -O2 -Wall -Wextra -fPIC

OBJ = mem_alloc.o

# Threaded build
ifeq ($(THREADS),1)
    # Add the -pthread flag to both compiler and linker flags.
    # This is the most portable way to handle pthreads.
    CFLAGS += -pthread
endif

all: libmem_alloc.a

mem_alloc.o: mem_alloc.c mem_alloc.h
	$(CC) $(CFLAGS) -c mem_alloc.c

libmem_alloc.a: $(OBJ)
	ar rcs $@ $^

clean:
	rm -f *.o *.a