#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"

thread_safe_guard*
thread_safe_guard_init(int fd)
{
  return 0;
}

int
thread_safe_pread(thread_safe_guard* file_guard, void* addr, int n, int off)
{
  return pread(1, addr, n, off);
}

int
thread_safe_pwrite(thread_safe_guard* file_guard, void* addr, int n, int off)
{
  return pwrite(1, addr, n, off);
}

void
thread_safe_guard_destroy(thread_safe_guard* file_guard)
{
}
