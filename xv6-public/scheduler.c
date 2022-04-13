#include "types.h"
#include "defs.h"

struct mlfq {
  struct proc *q0;
  struct proc *q1;
  struct proc *q2;
};

int
schpush(struct proc *p)
{
  return 0;
}

int
schpop(struct proc *p)
{
  return 0;
}

struct proc*
nextproc(struct proc *p)
{
  return p;
}

void
schboost(int ticks)
{
  return;
}

int
timeqt(struct proc *p)
{
  return 0;
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
