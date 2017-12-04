#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>

#include "m-worker.h"

static int
cputime (void)
{
#if 0
  /* Wrong for multi thread code since it measures the time spent 
     by all threads not the real time */
  struct rusage rus;

  getrusage (0, &rus);
  return rus.ru_utime.tv_sec * 1000 + rus.ru_utime.tv_usec / 1000;
#else
  struct timeval tv;
  gettimeofday (&tv, NULL);
  return tv.tv_sec * 1000 + tv.tv_usec / 1000;  
#endif
}

/***************************************************************************/

worker_t w_g;

int fib(int n);

struct fib2_s {
  int x, n;
};

static void subfunc_1 (void *data) {
  struct fib2_s *f = data;
  f->x = fib (f->n );
}

/* Compute Fibonacci number using thread systems. */
int fib(int n)
{
  if (n < 2)
    return n;

  struct fib2_s f;
  worker_block_t b;

  worker_start(b);
  f.n = n - 2;
  worker_spawn (w_g, b, subfunc_1, &f);
  int y = fib (n-1);
  worker_sync(b);
  return f.x + y;
}

int main()
{
  worker_init(w_g, 0, 0, NULL);
  int n = 39;
  int start = cputime();
  int result = fib(n);
  int end = cputime();

  // Display our results
  double duration = (double)(end - start) / 1000;
  printf("Fibonacci number #%d is %d.\n", n, result);
  printf("Calculated in %.3f seconds with %lu workers.\n", duration,
         worker_count(w_g) );

  worker_clear(w_g);
  return 0;
}
