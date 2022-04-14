#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"

#define Q0TICKS 1
#define Q1TICKS 2
#define Q2TICKS 4

#define Q1ALTMT 5
#define Q2ALTMT 10

#define BOOST_PERIOD 100

struct {
  struct proc *queue[3];  // Priority Queues
  struct proc *lastproc;  // Last executed process
  int lastpid;            // Last executed process ID
} mlfq;

void
qpop(struct proc *p)
{
  if(p->qnext != 0){
    p->qnext->qprev = p->qprev;
  }
  if(p->qprev != 0){
    p->qprev->qnext = 0;
  }
  if(p == mlfq.queue[p->qlev]){
    mlfq.queue[p->qlev] = 0;
  }
}

int
qmove(struct proc *p, int level)
{
  if(p->qlev < 0)
    return -1;
  
  struct proc *ptr = mlfq.queue[level];
  if(ptr == 0){
    mlfq.queue[level] = p;
  }else{
    for(;ptr->qnext != 0;){
      ptr = ptr->qnext;
    }
    ptr->qnext = p;
  }

  p->qprev = ptr;
  p->qnext = 0;
  p->qelpsd = 0;

  return 0;
}

int
schpush(struct proc *p)
{
  p->qlev = 0;
  p->cshr = 0;

  cprintf("schpush %p\n", p);
  return qmove(p, 0);
}

int
schpop(struct proc *p)
{
  if(p->qlev < 0)
    return -1;

  qpop(p);

  cprintf("schpop %p\n", p);
  return 0;
}

struct proc*
nextmlfq(void)
{
  //cprintf("nextmlfq\n");
  int prevlev = -1; // If non-negative, revious level of executed.
  if(mlfq.lastproc != 0 && mlfq.lastproc->pid == mlfq.lastpid)
    prevlev = mlfq.lastproc->qlev;

  struct proc *p = 0;
  int lev;

  for(lev=0; lev<3; lev++){
    if(mlfq.queue[lev] == 0)
      continue;
    
    p = mlfq.queue[lev];
    for(;p != 0 && p->state != RUNNABLE;)
      p = p->qnext;

    if(p == 0)
      continue;

    //cprintf("level %d %p %p\n", lev, p, p->qnext);

    // When the process is found at the same level.
    if(lev == prevlev && p->pid == mlfq.lastpid){
      // Search for the next runnable process.
      for(;p != 0 && p->state != RUNNABLE;)
        p = p->qnext;

      // If not found, run the previous one.
      if(p == 0)
        p = mlfq.lastproc;
    }

    break;
  }

  if(p == 0)
    return 0;

  mlfq.lastproc = p;
  mlfq.lastpid = p->pid;

  return p;
}

struct proc*
nextproc(void)
{
  return nextmlfq();
}

//ptable lock is required.
void
lockedboost(int mlfqticks)
{
  if(mlfqticks % BOOST_PERIOD != 0)
    return;
}

int
timeqt(struct proc *p)
{
  switch(p->qlev){
  case 0:
    return Q0TICKS;
  case 1:
    return Q1TICKS;
  case 2:
    return Q2TICKS;
  default:
    return -1;
  }
}

int
set_cpu_share(int share)
{
  return 0;
}

// wrapper for set_cpu_share()
int
sys_set_cpu_share(int share)
{
  return set_cpu_share(share);
}
