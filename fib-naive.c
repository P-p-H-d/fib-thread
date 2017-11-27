#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>

#define NESTED_FUNCTION 0

#if defined(_WIN32)
# include <windows.h>
#elif (defined(__APPLE__) && defined(__MACH__)) \
  || defined(__DragonFly__) || defined(__FreeBSD__) \
  || defined(__NetBSD__) || defined(__OpenBSD__)
# include <sys/param.h>
# include <sys/sysctl.h>
# define USE_SYSCTL
#else
# include <unistd.h>
#endif

/* Return the number of CPU of the system */
int get_cpu_count(void)
{
#if defined(_WIN32)
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  return sysinfo.dwNumberOfProcessors;
#elif defined(USE_SYSCTL)
  int nm[2];
  int count = 0;
  size_t len = sizeof (count);
  nm[0] = CTL_HW;
  nm[1] = HW_NCPU;
  sysctl(nm, 2, &count, &len, NULL, 0);
  return MAX(1, count);
#elif defined (_SC_NPROCESSORS_ONLN)
  return sysconf(_SC_NPROCESSORS_ONLN);
#else
  return 1;
#endif
}

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
/**********************************************************************/

#define MAX_THREAD 32

typedef enum {
  TERMINATE_THREAD = -1,
  WAITING_FOR_DATA = 0,
  THREAD_RUNNING = 1
} workstate_t;

typedef struct {
  int num_spawn;
  int num_terminated_spawn;
} spawn_block_t[1];

typedef struct {
  pthread_t       idx;
  pthread_mutex_t mutex;
  pthread_cond_t  cond;
  workstate_t working;
  volatile int * num_spawn_ptr;
  void * data;
  void (*func) (void *data);
} mt_comm_t;

typedef struct {
  int is_initialized;
  int num_thread;
  size_t size;
  pthread_mutex_t master_mutex;
  pthread_cond_t  master_cond;
  mt_comm_t comm[MAX_THREAD];
} mt_t;


/* Define shared data between thread */
mt_t mt_g;

/* Code of the main function of each working thread */
static void *mt_thread_main (void *arg)
{
  mt_comm_t *mc = arg;

  pthread_mutex_lock(&mc->mutex);

  while (1) {
    /* Wait for data to process... */
    /* If not working, sleep until wake up */
    if (mc->working <= WAITING_FOR_DATA) {
      /* Check for terminate condition */
      if (mc->working == TERMINATE_THREAD) {
        break;
      }
      /* Wait for next cycle */
      pthread_cond_wait(&mc->cond, &mc->mutex);
      continue;
    }
    pthread_mutex_unlock(&mc->mutex);

    /* Execute thread */
    (*mc->func) (mc->data);

    /* Unwork thread */
    pthread_mutex_lock(&mc->mutex);
    mc->working = WAITING_FOR_DATA;

    /* Enter Signal terminaison block */
    pthread_mutex_lock (&mt_g.master_mutex);

    /* Signal terminaison */
    *mc->num_spawn_ptr += 1;
    pthread_cond_broadcast(&mt_g.master_cond);
    pthread_mutex_unlock (&mt_g.master_mutex);

  }
  pthread_mutex_unlock(&mc->mutex);
  return NULL;
}


/* TODO: Support failure in initialization */
void thread_init(int num_thread, size_t size)
{
  int rc;

  /* Initialize global memory for MT handling */
  memset(&mt_g, 0, sizeof mt_g);
  mt_g.size = size;

  /* Initialize global mutex */
  rc = pthread_mutex_init(&mt_g.master_mutex, NULL);
  if (rc != 0)
   abort();
  rc = pthread_cond_init(&mt_g.master_cond, NULL);
  if (rc != 0)
    abort();

  /* Initialize threads */
  mt_g.num_thread = num_thread - 1;
  for (int i = 0 ; i < num_thread-1; i++) {
    rc = pthread_mutex_init(&mt_g.comm[i].mutex, NULL);
    if (rc != 0)
      abort();
    rc = pthread_cond_init(&mt_g.comm[i].cond, NULL);
    if (rc != 0)
      abort();
    rc = pthread_create (&mt_g.comm[i].idx,
                         NULL, mt_thread_main, &mt_g.comm[i]);
    if (rc != 0)
      abort();
  }
  mt_g.is_initialized = 1;
}

int thread_quit(void)
{
  int rc;
  int previous = mt_g.num_thread+1;
  /* If the thread system has been initialized */
  if (mt_g.is_initialized != 0) {
    for(int i = 0; i < mt_g.num_thread ; i++) {
      mt_comm_t *mc = &mt_g.comm[i];
      /* Request terminaison */
      rc = pthread_mutex_lock (&mc->mutex);
      assert (rc == 0);
      assert (mc->working == WAITING_FOR_DATA);
      mc->working = TERMINATE_THREAD; /* Request terminaison */
      pthread_cond_signal(&mc->cond);
      pthread_mutex_unlock (&mc->mutex);

      /* Join it to terminate it */
      rc = pthread_join(mt_g.comm[i].idx, NULL);
      assert (rc == 0);

      /* mutex_destroy needs mutex to be unlocked */
      rc = pthread_mutex_destroy(&mt_g.comm[i].mutex);
      assert (rc == 0);
      rc = pthread_cond_destroy(&mt_g.comm[i].cond);
      assert (rc == 0);
    }
    /* mutex_destroy needs mutex to be unlocked */
    rc = pthread_mutex_destroy(&mt_g.master_mutex);
    assert (rc == 0);
    rc = pthread_cond_destroy(&mt_g.master_cond);
    assert (rc == 0);

    mt_g.is_initialized = 0;
    mt_g.num_thread = 0;
  }
  return previous;
}


void spawn_start(spawn_block_t block)
{
  block->num_spawn = 0;
  block->num_terminated_spawn = 0;
}

void spawn (spawn_block_t block, void (*func)(void *data), void *data)
{
  for(int i = 0; i < mt_g.num_thread ; i++) {
    mt_comm_t *mc = &mt_g.comm[i];

    /* If the thread is not working */
    if (mc->working == WAITING_FOR_DATA) {
      pthread_mutex_lock (&mc->mutex);
      /* If the thread is still not working */
      if ( (mc->working != WAITING_FOR_DATA)) {
        pthread_mutex_unlock (&mc->mutex);
        continue;
      }

      /* Setup data for the thread */
      mc->working = THREAD_RUNNING;
      mc->func = func;
      mc->data = data;
      mc->num_spawn_ptr = &block->num_terminated_spawn;
      block->num_spawn +=1;

      /* Signal to thread that some work are available */
      pthread_cond_signal(&mc->cond);
      pthread_mutex_unlock (&mc->mutex);
      return ;
    }
  }
  /* No thread available. Call the function ourself */
  (*func) (data);
}

void spawn_sync(spawn_block_t block)
{
  /* If the number of spawns is greated than the number
     of terminated spawns, some spawns are still working.
     So wait for terminaison */
  if (block->num_spawn > block->num_terminated_spawn) {
    pthread_mutex_lock (&mt_g.master_mutex);
    while (1) {
      if (block->num_spawn == block->num_terminated_spawn)
        break;
      pthread_cond_wait(&mt_g.master_cond, &mt_g.master_mutex);
    }
    pthread_mutex_unlock (&mt_g.master_mutex);
  }
}

/***************************************************************************/

int fib(int n);

struct fib2_s {
  int x, n;
};

#if NESTED_FUNCTION == 0
static void subfunc_1 (void *data) {
  struct fib2_s *f = data;
  f->x = fib (f->n );
}
#endif

/* Compute Fibonacci number using thread systems. */
int fib(int n)
{
  if (n < 2)
    return n;

  struct fib2_s f;
  spawn_block_t b;

  spawn_start(b);
  f.n = n - 2;
#if NESTED_FUNCTION == 0
  spawn (b, subfunc_1, &f);
#else
  void subfunc_2 (void *data) {
    f.x = fib(f.n);
  }
  spawn (b, subfunc_2, &f);
#endif
  int y = fib (n-1);
  spawn_sync(b);
  return f.x + y;
}

int main()
{
  int worker = get_cpu_count();
  thread_init(worker, 0);
  int n = 39;
  int start = cputime();
  int result = fib(n);
  int end = cputime();


  // Display our results
  double duration = (double)(end - start) / 1000;
  printf("Fibonacci number #%d is %d.\n", n, result);
  printf("Calculated in %.3f seconds using %d workers.\n",
         duration, worker);

  return 0;
}
