#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"

#define Q0TICKS 5         // Ticks of queue 0
#define Q1TICKS 10        // Ticks of queue 1
#define Q2TICKS 20        // Ticks of queue 2
#define SSTICKS 5         // Ticks of stride

#define Q0ALTMT 20        // Time allotment of queue 0
#define Q1ALTMT 40        // Time allotment of queue 1

#define BSTPRD 200        // Boost period

#define SHAREMAX 80       // Maximum of CPU share of ss
#define GTICKETS 10000    // Global tickets of ss

struct {
  struct proc *queue[3];  // Priority Queues
  struct proc *lastproc;  // Last executed process
  int lastpid;            // Last executed process ID
} mlfq;

struct {
  struct proc *queue;     // Process queue
  int shares;             // Total shares
  int mlfqpass;           // Passes of MLFQ
  struct proc *lastproc;  // Last executed process
  int lastpid;            // Last executed process ID
} stride;

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

  p->qlev = level;
  p->qnext = 0;
  p->qelpsd = 0;

#ifdef SCHDEBUG
    cprintf("qmove %p %d\n", p, level);
#endif

  return 0;
}

int
qpush(struct proc *p)
{
  p->qlev = 0;
  p->sshr = 0;

#ifdef SCHDEBUG
    cprintf("qpush %p\n", p);
#endif

  return qmove(p, 0);
}

// Both MLFQ and Stride are accepted.
int
qpop(struct proc *p)
{
  struct proc **qhead = (p->qlev >= 0) ?
    &mlfq.queue[p->qlev] : &stride.queue;

  struct proc *ptr = *qhead; 
  if(p == *qhead){
    *qhead = p->qnext;
  }else{
    for(;p != ptr->qnext;)
      ptr = ptr->qnext;
    
    ptr->qnext = p->qnext;
  }

  if(p->qlev < 0){
    stride.shares -= p->sshr;
    p->sshr = 0;
  }

#ifdef SCHDEBUG
    cprintf("qpop %p\n", p);
#endif
  return 0;
}

int
altmt(struct proc *p)
{
  switch(p->qlev){
  case 0:
    return Q0ALTMT;
  case 1:
    return Q1ALTMT;
  default:
    return -1;
  }
}

int
qdown(struct proc *p)
{
  if(p->qlev == 2 || p->qelpsd <= altmt(p))
    return 1;

  qpop(p);
  qmove(p, p->qlev+1);

#ifdef SCHDEBUG
    cprintf("qdown %p %d\n", p, p->qlev);
#endif

  return 0;
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

struct proc*
nextmlfq(void)
{
  int prevlev = -1;
  if(mlfq.lastproc != 0 && mlfq.lastproc->pid == mlfq.lastpid){
    // Return the last process which hasn't ended up
    if((mlfq.lastproc->qelpsd % timeqt(mlfq.lastproc)) > 0 &&
        (mlfq.lastproc->state == RUNNABLE || mlfq.lastproc->state == TSLEEPING)){
      return mlfq.lastproc;
    }

    prevlev = mlfq.lastproc->qlev;
  }

  struct proc *p = 0;
  int lev;

  for(lev=0; lev<3; lev++){
    if(mlfq.queue[lev] == 0)
      continue;
    
    p = mlfq.queue[lev];
    for(;p != 0 && (p->state != RUNNABLE && p->state != TSLEEPING);)
      p = p->qnext;

    if(p == 0)
      continue;

    // When the process is found at the same level.
    if(lev == prevlev && p->pid == mlfq.lastpid){
      // Search for the next runnable process.
      for(;p != 0 && (p->state != RUNNABLE && p->state != TSLEEPING);)
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

#ifdef SCHDEBUG
    cprintf("nextmlfq %d %d %d\n", p->pid, p->qlev, p->qelpsd);
#endif

  return p;
}

struct proc*
nextproc(void)
{
  struct proc *p = 0, *ptr;

  if(stride.lastproc != 0 && stride.lastproc->pid == stride.lastpid &&
      (stride.lastproc->qelpsd % SSTICKS) > 0 &&
      (stride.lastproc->state == RUNNABLE || stride.lastproc->state == TSLEEPING)){
    // Return the last process which hasn't ended up
    p = stride.lastproc;

  }else{
    // Traverse stride queue and find the minimum passes.
    ptr = stride.queue;
    for(;ptr != 0;){
      if((ptr->state == RUNNABLE || ptr->state == TSLEEPING)
           && (p == 0 || ptr->spass < p->spass))
        p = ptr;

      ptr = ptr->qnext;
    }

    if(p != 0 && stride.mlfqpass > p->spass){
      // Select from stride
      p->spass += (GTICKETS / p->sshr);

      stride.lastproc = p;
      stride.lastpid = p->pid;
    }else{
      // Find from MLFQ
      ptr = nextmlfq();
      if(ptr != 0){
        p = ptr;
        stride.mlfqpass += (GTICKETS / (100-stride.shares));
      }
    }
  }

  return p;
}

//ptable lock is required.
void
qboost(int mlfqticks)
{
  if(mlfqticks % BSTPRD != 0)
    return;
  
#ifdef SCHDEBUG
    cprintf("boost start\n");
#endif

  struct proc *ptr;
  int cnt;
  int lev;
  for(lev=1; lev<=2; lev++){
    cnt = 0;
    for(ptr=mlfq.queue[lev]; ptr!=0;){
#ifdef SCHDEBUG
        cprintf("boost[%d] %p %d\n", lev, ptr, ptr->qlev);
#endif
      qpop(ptr);
      qmove(ptr, 0);
      ptr=ptr->qnext;
      cnt++;
    }
#ifdef SCHDEBUG
      cprintf("boost[%d] (%d)\n", lev, cnt);
#endif
  }
}

int
setsshr(struct proc *p, int share)
{
  if(share <= 0)
    return -1;

  // Check the maximum of total shares.
  if((stride.shares - p->sshr + share) > SHAREMAX)
    return -2;

  int minpass = stride.mlfqpass;
  struct proc *ptr = stride.queue;
  for(;ptr != 0;){
    if(ptr->spass < minpass)
      minpass = ptr->spass;
    ptr = ptr->qnext;
  }

  if(p->qlev >= 0){
    qpop(p);
    p->qlev = -1;
    p->qelpsd = 0;
    p->qnext = stride.queue;
    stride.queue = p;
    stride.shares += share;
  }else{
    stride.shares += (share - p->sshr);
  }

  p->sshr = share;
  p->spass = minpass;

  return 0;
}
