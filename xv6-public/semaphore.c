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

void
xem_cond_wait(xem_t *xem)
{
  void *chan;
#ifdef XEMDEBUG
  cprintf("[xem_cond_wait] (%d) start\n", myproc()->pid);
#endif

  // Enqueue
  if(xem->front == -1) xem->front = 0;
  xem->rear = (xem->rear + 1) % XEMQSIZE;
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
    sleept(chan, &xemlock.ltable[xem->lockidx]);
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
  xem->queue[xem->front] = 0;

  // Dequeue
  if(xem->front == xem->rear){
    xem->front = -1;
    xem->rear = -1;
  }else{
    xem->front = (xem->front+1) % XEMQSIZE;
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
        if(xemlock.xtable[i] == 0){
#ifdef XEMDEBUG
          cprintf("[xem_wait] (%d) index %d\n", myproc()->pid, i);
#endif
          break;
        }
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
  }else{
#ifdef XEMDEBUG
    cprintf("[xem_wait] (%d) existing index %d\n", myproc()->pid, xem->lockidx);
#endif
    i = xem->lockidx;
  }
  acquire(&xemlock.ltable[i]);
  release(&xemlock.guard);

#ifdef XEMDEBUG
  cprintf("[xem_wait] (%d) acquire\n", myproc()->pid);
#endif

  // If the queue is full
  if((xem->front == 0 && xem->rear == (XEMQSIZE-1))
      || (xem->front == xem->rear+1)){
    release(&xemlock.ltable[xem->lockidx]);
#ifdef XEMDEBUG
  cprintf("[xem_wait] (%d) queue is full\n", myproc()->pid);
#endif
    return -1;
  }

  if(xem->value <= 0)
    xem_cond_wait(xem);

  xem->value--;
  release(&xemlock.ltable[i]);
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
  if(xem->lockidx != -1)
    acquire(&xemlock.ltable[xem->lockidx]);
#ifdef XEMDEBUG
  cprintf("[xem_unlock] (%d) acquire\n", myproc()->pid);
#endif

  islast = (xem->front == -1) ? 1 : 0;
  i = xem->lockidx;
  xem->value++;
  xem_cond_signal(xem);

  if(islast == 1 && i != -1){
#ifdef XEMDEBUG
    cprintf("[xem_unlock] (%d) index %d\n", myproc()->pid, i);
#endif
    // Leave a room for other xem
    xemlock.xtable[xem->lockidx] = 0;
    xem->lockidx = -1;
    wakeup((void*)&xemlock.guard);
  }
  if(i != -1)
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
  xem_init(&rwlock->lock);
  xem_init(&rwlock->writelock);
  memset(rwlock->queue, 0, XEMQSIZE);
  rwlock->readers = 0;
  rwlock->wlowner = 0;
  return 0;
}

int
rwlock_acquirable(int isread, rwlock_t *rwlock)
{
  int pid, i;
  // The maximum number of read locks has been exceeded.
  if(isread == 1 && rwlock->readers >= XEMQSIZE){
#ifdef RWLDEBUG
    cprintf("[RWL_acq] (%d) error (exceed)\n", myproc()->pid);
#endif
    return 0;
  }

  // The current thread already owns write lock.
  pid = myproc()->pid;
  if(rwlock->wlowner == pid){
#ifdef RWLDEBUG
    cprintf("[RWL_acq] (%d) error (dupl-w)\n", pid);
#endif
    return 0;
  }

  // The current thread already owns read lock.
  for(i = 0; i < XEMQSIZE; i++){
    if(rwlock->queue[i] == pid){
#ifdef RWLDEBUG
      cprintf("[RWL_acq] (%d) error (dupl-r)\n", pid);
#endif
      return 0;
    }
  }
  
  return 1;
}

int
rwlock_acquire_readlock(rwlock_t *rwlock)
{
#ifdef RWLDEBUG
      cprintf("[RWL_acq_r] (%d) start\n", myproc()->pid);
#endif
  int i;
  xem_wait(&rwlock->lock);

  if(rwlock_acquirable(1, rwlock) == 0){
    xem_unlock(&rwlock->lock);
#ifdef RWLDEBUG
      cprintf("[RWL_acq_r] (%d) error\n", myproc()->pid);
#endif
    return -1;
  }

  // Record the current pid 
  for(i = 0; i < XEMQSIZE; i++){
    if(rwlock->queue[i] == 0){
      rwlock->queue[i] = myproc()->pid;
#ifdef RWLDEBUG
      cprintf("[RWL_acq_r] (%d) enque %d\n", myproc()->pid, i);
#endif
      break;
    }
  }

  if(i == XEMQSIZE){
    xem_unlock(&rwlock->lock);
#ifdef RWLDEBUG
    cprintf("[RWL_acq_r] (%d) index not found\n", myproc()->pid);
#endif
    return -1;
  }

  // Increase reader count
  rwlock->readers++;

  if(rwlock->readers == 1)
    xem_wait(&rwlock->writelock);

  xem_unlock(&rwlock->lock);
#ifdef RWLDEBUG
      cprintf("[RWL_acq_r] (%d) end\n", myproc()->pid);
#endif
  return 0;
}

int
rwlock_acquire_writelock(rwlock_t *rwlock)
{
#ifdef RWLDEBUG
      cprintf("[RWL_acq_w] (%d) start\n", myproc()->pid);
#endif
  xem_wait(&rwlock->lock);

  if(rwlock_acquirable(0, rwlock) == 0){
    xem_unlock(&rwlock->lock);
#ifdef RWLDEBUG
      cprintf("[RWL_acq_w] (%d) error\n", myproc()->pid);
#endif
    return -1;
  }
  
  xem_unlock(&rwlock->lock);
  xem_wait(&rwlock->writelock);

  rwlock->wlowner = myproc()->pid;
#ifdef RWLDEBUG
      cprintf("[RWL_acq_w] (%d) end\n", myproc()->pid);
#endif
  return 0;
}

int
rwlock_release_readlock(rwlock_t *rwlock)
{
  int i, pid;
  xem_wait(&rwlock->lock);

  // Erase the current pid 
  pid = myproc()->pid;
  for(i = 0; i < XEMQSIZE; i++){
    if(rwlock->queue[i] == pid){
      rwlock->queue[i] = 0;
#ifdef RWLDEBUG
      cprintf("[RWL_rel_r] (%d) deque %d\n", myproc()->pid, i);
#endif
      break;
    }
  }

  if(i == XEMQSIZE){
    xem_unlock(&rwlock->lock);
#ifdef RWLDEBUG
    cprintf("[RWL_rel_r] (%d) index not found\n", myproc()->pid);
#endif
    return -1;
  }

  rwlock->readers--;

  if(rwlock->readers == 0)
    xem_unlock(&rwlock->writelock);

#ifdef RWLDEBUG
      cprintf("[RWL_rel_r] (%d) readers %d\n", myproc()->pid, rwlock->readers);
#endif

  xem_unlock(&rwlock->lock);
  return 0;
}

int
rwlock_release_writelock(rwlock_t *rwlock)
{
  rwlock->wlowner = 0;

#ifdef RWLDEBUG
      cprintf("[RWL_rel_w] (%d) end\n", myproc()->pid);
#endif

  xem_unlock(&rwlock->writelock);
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
