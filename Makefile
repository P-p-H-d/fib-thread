BIN=fib-cilk
CC=gcc -std=c99
CFLAGS=-O2 -march=native -Wall

all: run-cilk run-openmp run-naive

fib-cilk: fib-cilk.c
	$(CC) $(CFLAGS) -fcilkplus fib-cilk.c -o fib-cilk

fib-openmp: fib-openmp.c
	$(CC) $(CFLAGS) -fopenmp fib-openmp.c -o fib-openmp

fib-naive: fib-naive.c
	$(CC) $(CFLAGS) fib-naive.c -o fib-naive -lpthread 

run-cilk: fib-cilk
	@echo Fibonacci by cilk+
	@perf stat ./fib-cilk

run-openmp: fib-openmp
	@echo Fibonacci by OPEN MP
	@perf stat ./fib-openmp

run-naive: fib-naive
	@echo Fibonacci by naive implementation
	@perf stat ./fib-naive

clean:
	rm -f fib-cilk fib-openmp fib-naive *~
