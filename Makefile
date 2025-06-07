CC = gcc
CFLAGS = -O3 -O2 -Wall -Wextra


mem_adapter: mem_adapter.c mem_adapter.h
	$(CC) $(CFLAGS) -c mem_adapter.c -o mem_adapter.o

run: mem_adapter.o mem_adapter.h
	$(CC) main.c mem_adapter.o -o main.a