#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <inttypes.h>
#include <sys/time.h>
#include <unistd.h>
#include <malloc.h>

#ifdef __sparc__
#  include <sys/types.h>
#  include <sys/processor.h>
#  include <sys/procset.h>
#endif

#include "ssmem.h"

/* ################################################################### *
 * GLOBALS
 * ################################################################### */


int num_allocs = 16;
int num_threads = 1;
int num_elements = 2048;
int duration = 1000;
double filling_rate = 0.5;
double update_rate = 0.1;
double put_rate = 0.1;
double get_rate = 0.9;
int print_vals_num = 0;
size_t  pf_vals_num = 8191;

int seed = 0;
__thread unsigned long * seeds;
uint32_t rand_max;
#define rand_min 1

static volatile int stop;

/* ################################################################### *
 * BARRIER
 * ################################################################### */

typedef struct barrier 
{
  pthread_cond_t complete;
  pthread_mutex_t mutex;
  int count;
  int crossing;
} barrier_t;

void barrier_init(barrier_t *b, int n) 
{
  pthread_cond_init(&b->complete, NULL);
  pthread_mutex_init(&b->mutex, NULL);
  b->count = n;
  b->crossing = 0;
}

void barrier_cross(barrier_t *b) 
{
  pthread_mutex_lock(&b->mutex);
  /* One more thread through */
  b->crossing++;
  /* If not all here, wait */
  if (b->crossing < b->count) {
    pthread_cond_wait(&b->complete, &b->mutex);
  } else {
    pthread_cond_broadcast(&b->complete);
    /* Reset for next time */
    b->crossing = 0;
  }
  pthread_mutex_unlock(&b->mutex);
}
barrier_t barrier, barrier_global;

#define PFD_TYPE 0

#if defined(COMPUTE_THROUGHPUT)
#  define START_TS(s)
#  define END_TS(s, i)
#  define ADD_DUR(tar)
#  define ADD_DUR_FAIL(tar)
#  define PF_INIT(s, e, id)
#elif PFD_TYPE == 0
#  define START_TS(s)				\
  {						\
    asm volatile ("");				\
    start_acq = getticks();			\
    asm volatile ("");
#  define END_TS(s, i)				\
    asm volatile ("");				\
    end_acq = getticks();			\
    asm volatile ("");				\
    }

#  define ADD_DUR(tar) tar += (end_acq - start_acq - correction)
#  define ADD_DUR_FAIL(tar)					\
  else								\
    {								\
      ADD_DUR(tar);						\
    }
#  define PF_INIT(s, e, id)
#else
#  define SSPFD_NUM_ENTRIES  pf_vals_num
#  define START_TS(s)      SSPFDI(s)
#  define END_TS(s, i)     SSPFDO(s, i & SSPFD_NUM_ENTRIES)

#  define ADD_DUR(tar) 
#  define ADD_DUR_FAIL(tar)
#  define PF_INIT(s, e, id) SSPFDINIT(s, e, id)
#endif

typedef struct thread_data
{
  uint8_t id;
} thread_data_t;



volatile uint64_t total_ops = 0;
uintptr_t* array;

void*
test(void* thread) 
{
  thread_data_t* td = (thread_data_t*) thread;
  uint8_t ID = td->id;
  set_cpu(the_cores[ID]);

  seeds = seed_rand();

  /* printf("[%2d] starting:: %d allocs\n", ID, num_allocs); */

  ssmem_allocator_t* alloc = (ssmem_allocator_t*) memalign(CACHE_LINE_SIZE, num_allocs * sizeof(ssmem_allocator_t));
  assert(alloc != NULL);

  int i;
  for (i = 0; i < num_allocs; i++)
    {
      ssmem_alloc_init(alloc + i, SSMEM_DEFAULT_MEM_SIZE, ID);
    }

  size_t ops = 0;
  barrier_cross(&barrier_global);

  while (stop == 0) 
    {
      ops++;
      int spot = (my_random(&(seeds[0]), &(seeds[1]), &(seeds[2])) % rand_max);
      int a    = (my_random(&(seeds[0]), &(seeds[1]), &(seeds[2])) % num_allocs);

      size_t* obj = (size_t*) ssmem_alloc(alloc + a, sizeof(uintptr_t));
      *obj = *(size_t*) array[spot];
      size_t* old = (size_t*) SWAP_U64((uint64_t*) (array + spot), (uint64_t) obj);

      ssmem_free(alloc + a, (void*) old);
    }


  __sync_fetch_and_add(&total_ops, ops);

  /* printf("[%2d] stoping...\n", ID); */
  barrier_cross(&barrier);
  if (ID == 0)
    {
      int i;
      for (i = 0; i < rand_max; i++)
	{
	  if (array[i] != 0)
	    {
	      size_t v = *(size_t*) array[i];
	      if (v != i)
		{
		  printf("!! %d = %zu\n", i, v);
		}
	    }
	}
    }
  barrier_cross(&barrier_global);
  ssmem_term();
  pthread_exit(NULL);
}

int
main(int argc, char **argv) 
{
  set_cpu(the_cores[0]);
    
  struct option long_options[] = {
    // These options don't set a flag
    {"help",                      no_argument,       NULL, 'h'},
    {"duration",                  required_argument, NULL, 'd'},
    {"initial-size",              required_argument, NULL, 'i'},
    {"num-threads",               required_argument, NULL, 'n'},
    {"range",                     required_argument, NULL, 'r'},
    {"update-rate",               required_argument, NULL, 'u'},
    {"num-buckets",               required_argument, NULL, 'b'},
    {"print-vals",                required_argument, NULL, 'v'},
    {"vals-pf",                   required_argument, NULL, 'f'},
    {NULL, 0, NULL, 0}
  };

  size_t initial = 8, range = 2048;

  int i, c;
  while(1) 
    {
      i = 0;
      c = getopt_long(argc, argv, "hAf:d:i:n:r:s:u:m:a:l:p:b:v:f:", long_options, &i);
		
      if(c == -1)
	break;
		
      if(c == 0 && long_options[i].flag == 0)
	c = long_options[i].val;
		
      switch(c) 
	{
	case 0:
	  /* Flag is automatically set */
	  break;
	case 'h':
	  printf("intset -- STM stress test "
		 "(linked list)\n"
		 "\n"
		 "Usage:\n"
		 "  intset [options...]\n"
		 "\n"
		 "Options:\n"
		 "  -h, --help\n"
		 "        Print this message\n"
		 "  -d, --duration <int>\n"
		 "        Test duration in milliseconds\n"
		 "  -i, --initial-size <int>\n"
		 "        Number of elements to insert before test\n"
		 "  -n, --num-threads <int>\n"
		 "        Number of threads\n"
		 "  -r, --range <int>\n"
		 "        Range of integer values inserted in set\n"
		 "  -p, --put-rate <int>\n"
		 "        Percentage of put update transactions (should be less than percentage of updates)\n"
		 "  -b, --num-buckets <int>\n"
		 "        Number of initial buckets (stronger than -l)\n"
		 "  -v, --print-vals <int>\n"
		 "        When using detailed profiling, how many values to print.\n"
		 "  -f, --val-pf <int>\n"
		 "        When using detailed profiling, how many values to keep track of.\n"
		 );
	  exit(0);
	case 'd':
	  duration = atoi(optarg);
	  break;
	case 'i':
	  initial = atoi(optarg);
	  break;
	case 'n':
	  num_threads = atoi(optarg);
	  break;
	case 'r':
	  range = atol(optarg);
	  break;
	case 'v':
	  print_vals_num = atoi(optarg);
	  break;
	case 'f':
	  pf_vals_num = pow2roundup(atoi(optarg)) - 1;
	  break;
	case '?':
	default:
	  printf("Use -h or --help for help\n");
	  exit(1);
	}
    }

  num_allocs = initial;

  printf("# allocs %d / range: %zu\n", num_allocs, range);

  struct timeval start, end;
  struct timespec timeout;
  timeout.tv_sec = duration / 1000;
  timeout.tv_nsec = (duration % 1000) * 1000000;
    
  stop = 0;
    

  array = (uintptr_t*) calloc(range, sizeof(uintptr_t));
  assert(array != NULL);
  uintptr_t* objs = (uintptr_t*) calloc(range, sizeof(uintptr_t));
  assert(objs != NULL);

  int j;
  for (j = 0; j < range; j++)
    {
      objs[j] = j;
      array[j] = (uintptr_t) (objs + j);
    }

  rand_max = range;


  /* Initialize the hashtable */

  pthread_t threads[num_threads];
  pthread_attr_t attr;
  int rc;
  void *status;
    
  barrier_init(&barrier_global, num_threads + 1);
  barrier_init(&barrier, num_threads);
    
  /* Initialize and set thread detached attribute */
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    
  thread_data_t* tds = (thread_data_t*) malloc(num_threads * sizeof(thread_data_t));

  long t;
  for(t = 0; t < num_threads; t++)
    {
      tds[t].id = t;
      rc = pthread_create(&threads[t], &attr, test, tds + t);
      if (rc)
	{
	  printf("ERROR; return code from pthread_create() is %d\n", rc);
	  exit(-1);
	}
        
    }
    
  /* Free attribute and wait for the other threads */
  pthread_attr_destroy(&attr);
    
  barrier_cross(&barrier_global);
  gettimeofday(&start, NULL);
  nanosleep(&timeout, NULL);

  stop = 1;
  barrier_cross(&barrier_global);
  gettimeofday(&end, NULL);
  duration = (end.tv_sec * 1000 + end.tv_usec / 1000) - (start.tv_sec * 1000 + start.tv_usec / 1000);
    
  for(t = 0; t < num_threads; t++) 
    {
      rc = pthread_join(threads[t], &status);
      if (rc) 
	{
	  printf("ERROR; return code from pthread_join() is %d\n", rc);
	  exit(-1);
	}
    }

  free(tds);

  free(array);
  free(objs);

  double throughput = total_ops * 1000.0 / duration;
  printf("#txs %-4d(%10.0f = %.3f M\n", num_threads, throughput, throughput / 1.e6);

  pthread_exit(NULL);
  return 0;
}
