#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"

thread_safe_guard*
thread_safe_guard_init(int fd)
{
  thread_safe_guard *file_guard;

  // Allocate memory
  file_guard = malloc(sizeof(*file_guard));
  memset(file_guard, 0, sizeof(*file_guard));
  
  // Initialize
  file_guard->fd = fd;
  rwlock_init(&file_guard->rwlock);

  return (thread_safe_guard*)file_guard;
}

int
thread_safe_pread(thread_safe_guard* file_guard, void* addr, int n, int off)
{
  int ret;
  
  rwlock_acquire_readlock(&file_guard->rwlock);
  ret = pread(file_guard->fd, addr, n, off);
  rwlock_release_readlock(&file_guard->rwlock);

  return ret;
}

int
thread_safe_pwrite(thread_safe_guard* file_guard, void* addr, int n, int off)
{
  int ret;
  
  rwlock_acquire_writelock(&file_guard->rwlock);
  ret = pwrite(file_guard->fd, addr, n, off);
  rwlock_release_writelock(&file_guard->rwlock);

  return ret;
}

void
thread_safe_guard_destroy(thread_safe_guard* file_guard)
{
  free((void*)file_guard);
}
