#include "types.h"
#include "stat.h"
#include "user.h"

#define LARGENUM 10000
#define THREADS  10
#define REP     3

xem_t xem;

void nop(){ }

void *
test_without_sem(void *arg)
{
  int denominator = (LARGENUM / THREADS);
  int id = (int)arg;
  for(int i = 0; i < LARGENUM; ++i) {
	asm volatile("call %P0"::"i"(nop));
	if(i == (i / denominator) * denominator) {
	  printf(1, "%d", id % THREADS);
	}
  }
  thread_exit(0);
  return 0;
}


void *
test_with_sem1(void *arg)
{
  int denominator = (LARGENUM / THREADS);
  int id = (int)arg;
  xem_wait(&xem);
  for(int i = 0; i < LARGENUM; ++i) {
    asm volatile("call %P0"::"i"(nop));
    if(i == (i / denominator) * denominator) {
      printf(1, "%d", id % THREADS);
    }
  }
  xem_unlock(&xem);
  thread_exit(0);
  return 0;
}

int
main(int argc, char *argv[])
{
  void *ret;
  thread_t t[THREADS];

  for(int u = 0; u < REP; u++) {
    printf(1, "1. Test without any synchronization\n");

    for(int i = 0; i < THREADS; ++i) {
      if(thread_create(&t[i], test_without_sem, (void*)(i)) < 0) {
      printf(1, "panic at thread create\n");
      exit();
      }
    }

    for(int i = 0; i < THREADS; ++i) {
      if(thread_join(t[i], &ret) < 0) {
        printf(1, "panic at thread join\n");
        exit();
      }
    }

    printf(1, "\nIts sequence could be mixed\n");
  }

  printf(1, "2. Test with synchronization of a binary semaphore\n");
  xem_init(&xem);
  xem_wait(&xem);

  for(int i = 0; i < THREADS; ++i) {
    if(thread_create(&t[i], test_with_sem1, (void*)(i)) < 0) {
      printf(1, "panic at thread create\n");
      exit();
    }
  }
  printf(1, "create done\n");

  xem_unlock(&xem);
  for(int i = 0; i < THREADS; ++i) {
    if(thread_join(t[i], &ret) < 0) {
      printf(1, "panic at thread join\n");
      exit();
    }
  }
  printf(1, "\nIts sequence must be sorted\n");
  
  exit();

}
