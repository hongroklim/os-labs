#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// Lock table
struct {
  struct spinlock guard;
  xem_t *xtable[NPROC];
  struct spinlock ltable[NPROC];
} xemlock;

struct spinlock*
lock_of(xem_t *xem)
{
  return &xemlock.ltable[xem->lockidx];
}

void
xem_acquire(xem_t *xem)
{
  acquire(lock_of(xem));
}

void
xem_release(xem_t *xem)
{
  release(lock_of(xem));
}

void
xem_cond_wait(xem_t *xem)
{
  void *chan;
#ifdef XEMDEBUG
  cprintf("[xem_cond_wait] (%d) start\n", myproc()->pid);
#endif

  // Enqueue
  if(xem->front == -1) xem->front = 0;
  xem->rear = (xem->rear + 1) % NPROC;
  xem->queue[xem->rear] = myproc()->pid;

  // Get value
  chan = (void*)xem->queue[xem->rear];

#ifdef XEMDEBUG
  cprintf("[xem_cond_wait] (%d) release\n", myproc()->pid);
#endif

  // Prevent lost-wakeup
  if(xem->value <= 0){
#ifdef XEMDEBUG
    cprintf("[xem_cond_wait] (%d) sleep %d\n", myproc()->pid, chan);
#endif
    sleept(chan, lock_of(xem));
  }

#ifdef XEMDEBUG
  cprintf("[xem_cond_wait] (%d) end\n", myproc()->pid);
#endif
}

void
xem_cond_signal(xem_t *xem)
{
  void *chan;
#ifdef XEMDEBUG
  cprintf("[xem_cond_signal] (%d) start\n", myproc()->pid);
#endif

  if(xem->front == -1){
#ifdef XEMDEBUG
  cprintf("[xem_cond_signal] (%d) empty (end)\n", myproc()->pid);
#endif
    return;
  }

  // Get value
  chan = (void*)xem->queue[xem->front];

  // Dequeue
  if(xem->front == xem->rear){
    xem->front = -1;
    xem->rear = -1;
  }else{
    xem->front = (xem->front+1) % NPROC;
  }

#ifdef XEMDEBUG
  cprintf("[xem_cond_signal] (%d) wakeup %d\n", myproc()->pid, chan);
#endif
  wakeup(chan);

#ifdef XEMDEBUG
  cprintf("[xem_cond_signal] (%d) end\n", myproc()->pid);
#endif
}

// Semaphore
int
xem_init(xem_t *xem)
{
  xem->value = 1;
  xem->lockidx = -1;
  xem->front = -1;
  xem->rear = -1;

#ifdef XEMDEBUG
  cprintf("[xem_init] (%d) end\n", myproc()->pid);
#endif
  return 0;
}

int
xem_wait(xem_t *xem)
{
  int i;
#ifdef XEMDEBUG
  cprintf("[xem_wait] (%d) acquire try\n", myproc()->pid);
#endif

  acquire(&xemlock.guard);
  if(xem->lockidx == -1){
    // Obtain the available spinlock.
    for(;;){
      for(i=0; i<NPROC; i++){
        if(xemlock.xtable[i] == 0)
          break;
      }

      // Exit loop if it is available.
      if(i != NPROC)
        break;
      else
        sleept((void*)&xemlock.guard, &xemlock.guard);
    }

    // Occupy the empty xem.
    xemlock.xtable[i] = xem;
    xem->lockidx = i;
  }
  xem_acquire(xem);
  release(&xemlock.guard);

#ifdef XEMDEBUG
  cprintf("[xem_wait] (%d) acquire\n", myproc()->pid);
#endif

  // If the queue is full
  if((xem->front == 0 && xem->rear == (NPROC-1))
      || (xem->front == xem->rear+1)){
#ifdef XEMDEBUG
  cprintf("[xem_wait] (%d) queue is full\n", myproc()->pid);
#endif
    xem_release(xem);
    return -1;
  }

  while(xem->value <= 0)
    xem_cond_wait(xem);

  xem->value--;
  xem_release(xem);
#ifdef XEMDEBUG
  cprintf("[xem_wait] (%d) release\n", myproc()->pid);
#endif

  return 0;
}

int
xem_unlock(xem_t *xem)
{
  int i, islast;
#ifdef XEMDEBUG
  cprintf("[xem_unlock] (%d) acquire try\n", myproc()->pid);
#endif
  acquire(&xemlock.guard);
  xem_acquire(xem);
#ifdef XEMDEBUG
  cprintf("[xem_unlock] (%d) acquire\n", myproc()->pid);
#endif

  islast = (xem->front == -1) ? 1 : 0;
  i = xem->lockidx;
  xem->value++;
  xem_cond_signal(xem);

  if(islast == 1){
    // Leave a room for other xem
    xemlock.xtable[xem->lockidx] = 0;
    xem->lockidx = -1;
    wakeup((void*)&xemlock.guard);
  }
  release(&xemlock.ltable[i]);
  release(&xemlock.guard);

#ifdef XEMDEBUG
  cprintf("[xem_unlock] (%d) release\n", myproc()->pid);
#endif

  return 0;
}

// Readers-writer Lock
int
rwlock_init(rwlock_t *rwlock)
{
  return 0;
}

int
rwlock_acquire_readlock(rwlock_t *rwlock)
{
  return 0;
}

int
rwlock_acquire_writelock(rwlock_t *rwlock)
{
  return 0;
}

int
rwlock_release_readlock(rwlock_t *rwlock)
{
  return 0;
}

int
rwlock_release_writelock(rwlock_t *rwlock)
{
  return 0;
}

// System Call Wrappers
int
sys_xem_init(void)
{
  xem_t *xem;

  if(argptr(0, (void*)&xem, sizeof(*xem)) < 0)
    return -1;

  return xem_init(xem);
}

int
sys_xem_wait(void)
{
  xem_t *xem;

  if(argptr(0, (void*)&xem, sizeof(*xem)) < 0)
    return -1;

  return xem_wait(xem);
}

int
sys_xem_unlock(void)
{
  xem_t *xem;

  if(argptr(0, (void*)&xem, sizeof(*xem)) < 0)
    return -1;

  return xem_unlock(xem);
}

int
sys_rwlock_init(void)
{
  rwlock_t *rwlock;

  if(argptr(0, (void*)&rwlock, sizeof(*rwlock)) < 0)
    return -1;

  return rwlock_init(rwlock);
}

int
sys_rwlock_acquire_readlock(void)
{
  rwlock_t *rwlock;

  if(argptr(0, (void*)&rwlock, sizeof(*rwlock)) < 0)
    return -1;

  return rwlock_acquire_readlock(rwlock);
}

int
sys_rwlock_acquire_writelock(void)
{
  rwlock_t *rwlock;

  if(argptr(0, (void*)&rwlock, sizeof(*rwlock)) < 0)
    return -1;

  return rwlock_acquire_writelock(rwlock);
}

int
sys_rwlock_release_readlock(void)
{
  rwlock_t *rwlock;

  if(argptr(0, (void*)&rwlock, sizeof(*rwlock)) < 0)
    return -1;

  return rwlock_release_readlock(rwlock);
}

int
sys_rwlock_release_writelock(void)
{
  rwlock_t *rwlock;

  if(argptr(0, (void*)&rwlock, sizeof(*rwlock)) < 0)
    return -1;

  return rwlock_release_writelock(rwlock);
}
