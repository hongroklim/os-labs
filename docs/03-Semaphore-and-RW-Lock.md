# Semaphore and Readers-Writer Lock

Designs of Semaphore and Readers-writer Lock

> **Notes.** The common knowledges about semaphore and readers-writer lock
> mainly referred from [OSTWP](https://pages.cs.wisc.edu/~remzi/OSTEP/) and
> [Open Group Publications](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/semaphore.h.html)

## Background

### Semaphore

Semaphore is used to protect critical sections and consists with an integer
value. The way to utilize semaphore is the following. Before entering the
critical section, decrease the value of semaphore. If it is greater than or
equal to zero, then it can immediately move in the section. However, the value
is negative, it should wait. After passing the critical section, the value of
semaphore is increased and wake up the waiting processes or threads. Then one
of waiting jobs can go to the critical section.

### Readers-writer Lock

Readers-writer lock is slightly different from the mutual exclusion. It allows
multiple readers if there is no writer. That is, multiple read locks can be
acquired. For a writer, the lock becomes mutual exclusive, which means it
blocks all types of lock acquisition.

## POSIX API

### sem

* `sem_t`
  * Basic structure of semaphore defined in `semaphore.h`. It contains an
  integer value, mutex and condition variable.
* `int sem_init(sem_t *sem, int pshared, unsigned int value)`
  * Initialize an unnamed semaphore. `sem` is the target structure to
  initialize. If `pshared` is non-zero, any process can access the semaphore.
  `value` refers the maximum number of threads that can acquire the semaphore.
  If there is an error during the initialization, the function will return -1.
* `int sem_wait(sem_t *sem)`
  * Lock a semaphore referenced by `sem`. If the current value of `sem` is
  zero, it will wait until the value increases by `#sem_post`. The function
  returns zero if it was successful.
* `int sem_post(sem_t *sem)`
  * Unlock a semaphore. If the value of `sem` is not positive, it means that
  there exists blocked thread. Then the function wakes up the thread and
  returns.

### rwlock

* `pthread_rwlock_t`
  * Basic structure of read-write lock defined in `pthread.h`.
* `int pthread_rwlock_init(pthread_rwlock_t* lock, const pthread_rwlockattr_t attr)`
  * Initialize a read-write lock object. if `attr` is NULL, the default values
  are used. If sucessful, the function returns zero.
* `int pthread_rwlock_rdlock(pthread_rwlock_t* lock)`
  * Lock a read-write lock object for reading. It acquires the lock if there
  is not any writer who has already locked or is blocked. If sucessful, it
  returns zero.
* `int pthread_rwlock_wrlock(pthread_rwlock_t* lock)`
  * Lock a read-write lock object for writing. It acquires the lock if no
  other thread holds the lock, including read and write lock. Otherwise, it
  will be blocked until the lock is released. If sucessful, it returns zero.
* `int pthread_rwlock_unlock(pthread_rwlock_t* lock)`
  * Unlock a read-write lock object. If the function releases the last read
  lock, it puts the `lock` in unlocked state. Also if it was write lock, it
  will be released.

## Implementations

Both custom API are implemented in `semaphore.c`. Before looking at the
methods, related structures are the followings.

### Structures
```c
// types.h
// ...

#define XEMQSIZE       (64*2) // size of semaphore's queue capacity

typedef struct {
  int value;
  int lockidx;
  int queue[XEMQSIZE];
  int front;
  int rear;
} xem_t;

typedef struct {
  xem_t lock;
  xem_t writelock;
  int queue[XEMQSIZE];
  int readers;
  int wlowner;
} rwlock_t;
```

Because it is difficult to dynamically allocate memories in kernel, `xem_t`
was designed with the fixed size (e.g. `queue[XEMQSIZE]`). The lock object is
not embeded in `xem_t` in order to hide the `spinlock` structure to the user
level. Instead, an array of locks is constructed in kernel and each `xem_t`
can refer its lock through `lockidx`. `queue[]` is a FIFO queue to managing
the wait/signal processes or threads. To come up with the fixed size of queue,
it works like *circular queue* with `front` and `rear` variables. Therefore,
its capacity of waiting jobs is greater than the size of queue.

The next structure is `rwlock_t`. It internally utilize `xem_t` mentioned
before. `readers` tracks the number of demanding read locks. In addition, in
order to prevent redundancy of read lock, `queue[]` records the exact process
ID of read locks' owners. `wlowner`, Write-Lock Owner, keep the pid of write
lock's owner.

```c
// semaphore.c
// ...
struct {
  struct spinlock guard;
  xem_t *xtable[NPROC];
  struct spinlock ltable[NPROC];
} xemlock;
```

In `xemlock` structure, the real lock objects are stored in `ltable[]`.
`guard` is used to control the concurrency of the arrays. Each lock is mapped
via `xtable[]` which contains the `xem_t` addresses. By following the
corresponding the index, `xem_t` can utilize its own lock. As it is
implemented with fixed size array (`NPROC`), the occupations of `xemlock` are
regularly managed. (Detailed explanation will be described in *Semaphore*)

### Semaphore

Like `mutex_cond` methods in POSIX, `xem_cond_wait` and `xem_cond_signal` have
the similar roles. In `xem_cond_wait`, it checks the circular queue through
`queue[]`, `front` and `rear`. If the job has to wait, enque then sleep. The
sleeping process is marked with `chan` that can be used to wake it up later.

`xem_cond_signal` first looks up the waiting queue. If any exists, dequeue the
foremost one and wakeup with `chan` (it is same as `cond` of `xem_cond_wait`).

`xem_wait` first tries to acquire `guard` to access `xemlock.xtable[]`. Then
it checks its `lockidx` that contains the information about its own lock. If
it doesn't exist, find an available lock among the `ltable` by checking
whether `xtable[i]` is empty or not. If it fails to find an unused lock, it
goes sleep. (waiting the wake-up from the other's to-be-released lock)
Otherwise, mark its address in `xtable[]` and acquire its own lock. The next
step is check whether the queue of `xem_t` is full or not. If there is no
unused cell, the function returns non-zero. However, it is less likely to fail
because the size of queue is larger than the total number of processes (+
threads). After that, it calls `xem_cond_wait` then decrease `value` and
release its lock.

`xem_unlock` is quite simple. It less cares about other jobs and just calls
`xem_con_signal`. Its additional task is to check whether `xem_t`'s lock can
be released or not due to the fixed number of locks. If it can, clean up the
corresponding index of `xemlock`'s `xtable` and `ltable`. Then it wakes up
some processes who might be sleeping in `xem_wait` because of the lack of
empty lock.

### Readers-witer Lock

When acquiring any rwlock, `rwlock_acquirable` is used to check the current
status. There are tree failure cases when trying to acquiring an read/write
lock.

1. The maximum number of read locks has been exceeded.
  * By comparing `readers` to `XEMQSIZE`
1. The current thread already owns write lock.
  * By checking `wlowner` 
1. The current thread already owns read lock.
  * By checking `queue[]`

Those errors are following the description of POSIX. If it fails, the function
will return non-zero value. After checking the availability,
`rwlock_acquire_readlock` records the current process(thread) ID in `queue`
and increase `readers`. If it is the first reader, it holds `writelock`.

For `rwlock_acquire_writelock`, it waits `writelock`. If there is no reading
or other writing job, it will return immediately. Then `wlonwer` marks the
current process (thread).

Releasing is opposite to acquire. Change `queue`, `readers` and `wlowner` if
necessary.

## Solved/Unsolved Problems

### Fixed Size of Arrays

> Adopting circular queue

(In KOR) 프로세스나 쓰레드와는 달리 Semaphore는 사용자가 일반 변수처럼
무수히 많이 생성해서 사용할 수 있으므로 어떻게 하면 운영체제가 최대한 많은
Semaphore를 관리할 수 있을 지 고민해보았다. 가장 먼저 Linked List로 수요에
따라 동적으로 관리를 하려 했으나 Kernel 영역에서는 Virtual Memory 다루기가
까다로워서 (솔직히 말하면 내 LWP 코드에 자신이 없어서) 포기했다. 이에 대한
대안으로 생각해 낸 것이 Circular Queue이다. 충분한 크기의 배열을 먼저
생성하고, 해당 배열의 크기를 넘어서는 경우가 발생하더라도 감당할 수 있을 것
같았다. 한꺼번에 무수한 할당요청이 들어오는 상황에서도 무작정 실패값을
반환하는 것이 아니라 `sleep` / `wakeup`을 적절히 활용하여 Circular Queue에
공간이 남는대로 순차적으로 처리를 하게 될 수 있다는 장점이 있다.

### Producer-Consumer Problem

> Double-Checking logics

(In KOR) 앞서 말했듯 Queue의 크기에 따라 동시에 운영할 수 있는 Lock의
개수가 한정되었다. 그래서 초기화 단계부터 고정되도록 할당하는 것이 아닌
`xem_wait`과 `xem_unlock`를 할 때마다 현재 상태에서 Lock을 갖고 있을 필요가
없는 Semaphore들을 잘 선별해 내 Lock을 회수하는 전략을 세웠다. 이에 동시성
문제를 해결하기 위해 `guard`를 세웠지만 `xem_wait`과 `xem_unlock` 사이에
자꾸만 불일치가 생겼다. 이번 과제의 대부분의 시간을 이 이슈를 해결하는데
사용해야 했다. 결국에 찾아낸 것은 Critical Section을 `guard`로 잘 막았더라도
중간에 `sleep`과 `wakeup`하는 부분을 놓친 것이었다. 기억을 되살려보니 수업
시간에 교수님께서 *Producer Consumer Problem*에 대해 설명해 주시면서 sleep과
wakeup 사이에 mutex가 보장하지 못 하는 틈이 생긴다는 것을 상기할 수 있었다.
이를 바탕으로 찾아낸 해결방법은 간단하게도 double-check를 하면 되는
것이었다.

## [Unsolved] Bottleneck of `xemlock.guard`

> all `xem_wait` and `xem_unlock` should pass a single lock.

(In KOR) 이 문제 또한 Scalability에 신경을 쓰다보니 발생하였다. 각각의
`xem_t`가 혼자서 lock을 들고 있으면 참 좋으련만 어쩔 수 없이
`xemlock.ltable[]` 이라는 한 바구니에 함께 모아서 나눠쓰는 상황이다. 이런
상황에서 이 `ltable`을 관리하기 위해 `guard`를 세웠는데 자연스럽게 모든
기능들이 이 Lock을 통과해서 한 줄로 움직이고 있었다. `guard`를 과감하게
무시하고 필요할 때만 부르자니 Race Condition과 Deadlock이 자꾸만 신경이
쓰였다. Concurrency와 Performance를 종합적으로 고려할 수 있는 안목을
길러야겠다는 필요성을 느꼈다.
