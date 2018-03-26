BIN=fib-cilk
CC=gcc -std=c99
CFLAGS=-O2 -march=native -Wall
WORKER_PATH_FLAGS=-I../mlib

all: run-cilk run-openmp run-naive run-worker

fib-cilk: fib-cilk.c
	$(CC) $(CFLAGS) -fcilkplus fib-cilk.c -o fib-cilk

fib-openmp: fib-openmp.c
	$(CC) $(CFLAGS) -fopenmp fib-openmp.c -o fib-openmp

fib-naive: fib-naive.c
	$(CC) $(CFLAGS) fib-naive.c -o fib-naive -lpthread 

fib-worker: fib-worker.c
	$(CC) $(CFLAGS) fib-worker.c $(WORKER_PATH_FLAGS) -o fib-worker -lpthread

fib-worker2: fib-worker2.c
	$(CC) $(CFLAGS) fib-worker2.c $(WORKER_PATH_FLAGS) -o fib-worker2 -lpthread

run-cilk: fib-cilk
	@echo Fibonacci by cilk+
	@perf stat ./fib-cilk

run-openmp: fib-openmp
	@echo Fibonacci by OPEN MP
	@perf stat ./fib-openmp

run-naive: fib-naive
	@echo Fibonacci by naive implementation
	@perf stat ./fib-naive

run-worker: fib-worker
	@echo Fibonacci by m-worker implementation
	@perf stat ./fib-worker

run-worker2: fib-worker2
	@echo Fibonacci by m-worker implementation
	@perf stat ./fib-worker2

clean:
	rm -f fib-cilk fib-openmp fib-naive fib-worker fib-worker2 *~
