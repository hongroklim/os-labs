#include "types.h"
#include "defs.h"

int
getlev(void)
{
  return 0;
}

int
set_cpu_share(int share)
{
  return 0;
}

// wrapper for yield() defined in proc.c
int
sys_yield(void)
{
  return yield();
}

// wrapper for getlev()
int
sys_getlev(void)
{
  return getlev();
}

// wrapper for set_cpu_share()
int
sys_set_cpu_share(int share)
{
  return set_cpu_share(share);
}
