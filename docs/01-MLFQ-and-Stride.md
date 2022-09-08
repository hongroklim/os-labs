# MLFQ and Stride Scheduling

> Designs of a new scheduler with MLFQ and Stride

## MLFQ

Multilevel Feedback Queue (MLFQ) consists of multiple queues which have
different priorities for each. The structure of `mlfq` is the following.

```c
// scheduler.c
#define Q0TICKS 1         // Ticks of queue 0
#define Q1TICKS 2         // Ticks of queue 1
#define Q2TICKS 4         // Ticks of queue 2

#define Q0ALTMT 5         // Time allotment of queue 0
#define Q1ALTMT 10        // Time allotment of queue 1

#define BSTPRD 100        // Boost period ticks

struct {
  struct proc *queue[3];  // Priority Queues
  struct proc *lastproc;  // Last executed process
  int lastpid;            // Last executed process ID
} mlfq;
```

In order to implement MLFQ, some attributes are added in `struct proc`.

```c
// proc.h
struct proc {
  // ...
  int qlev;                    // If non-negative, level of MLFQ
  int qelpsd;                  // If non-negative, elapsed ticks in the same queue
  struct proc *qnext;          // If non-zero, next process in the process list
}
```

### Queues

In this scheduler, there are 3 queues distinguished by its priority (high,
middle and low). `mlfq.queue[0]` corresponds to high queue and so on.  In the
same level queue, processes are arranged through single linkedlist manner and
`proc.qnext` pointer refers to the next one.

New process from `userinit()` and `fork()` enters MLFQ via `qpush()` defined
in `scheduler.c`. The process will be assigned into `mlfq.queue[0]` at the
first time and appended the last chain of the queue.

When a process ends, `qpop()` will be called from `wait()` in `proc.c`. The
function pops up the process in MLFQ queue.

### Accounting CPU Time

While a process in MLFQ is running, its `proc.qelpsd` increases as much as
`ticks` of `trap.c` does. The property is used to determine whether its
priority goes down or not, comparing the queue level's time allotment, in
`qdown()` in `proc.c`.  If the elapsed time exceeds `Q0ALTMT` (or `Q1ALTMT`),
the level of the process will be changed.

If a process mannually calls `yield()` before a tick increases, its executed
ticks will be increased in `sys_yield()` in order to distinguish `yield()`
calls.

### Scheduling Principles

In the `scheduler()` of `proc.c`, the next process to run is determined
through `nextproc()` of `scheduler.c`. The function internally calls
`nextmlfq()` and it checks whether `proc.qelpsd` of valid `mlfq.lastproc`
exceeds its time quantum or not. The validity of `mlfq.lastproc` is confirmed
by comparing `mlfq.lastproc->pid` and `mlfq.lastpid` to prevent the case of
replacement of the pointer. If there is remaining time quantum,
`mlfq.lastproc` will be returned.

Otherwise, the function tries to find runnable process from the
`mlfq.queue[0]`.  If a proper candidate is found, it is compared to
`mlfq.lastproc` in order to avoid continuous selection of the same process (or
apply round robbin manner). After that, the selected process is recorded in
`mlfq.lastproc` and `mlfq.lastpid` then returned. If there is no runnable
process, zero will be returned.

#### Priority Boost

When the scheduler accounts CPU time, `qboost()` in `scheduler.c` is called.
Its parameter is `mlfqticks` managed by `trap.c` and is used to compare
whether it is time to boost or not with comparing `BSTPRD`. If it is, all
processes in `mlfq.queue[1]` and `mlfq.queue[2]` will be moved into
`mlfq.queue[0]` sequentially.

## Stride Scheduling

Stride scheduling allows processes to run while guaranteeing that they can
obtain the certain propotions of the resources and also they fairly run. The
required structure to manage this scheduling is the following.

```c
// scheduler.c
#define SHAREMAX 80     // Maximum of CPU share of SS
#define GTICKETS 10000  // Global tickets of SS

struct {
  struct proc *queue;     // Process queue
  int shares;             // Total shares
  int mlfqpass;           // Passes of MLFQ
} stride;

// proc.h
struct proc {
  // ...
  int sshr;                    // If positive, shared amount of CPU
  int spass;                   // If non-negative, total passes in ss
}
```

Like MLFQ, the processes assigned CPU share are combined in single linkedlist
format.

### Combining MLFQ and Stride

All processes are allocated into MLFQ except ones which call
`set_share_cpu()`.  The main scheduler is stride and MLFQ takes part in the
stride by obtaining the remaining tickets.

MLFQ's tickets is calculated through `stride.shares` and its's passes is
recorded in `stride.mlfqpass` and it has same function with `proc.spass`. In
`nextproc()`, if the MLFQ is chosen, it calls `nextmlfq()`.  Otherwise, the
one of processes registered in `stride.queue` will be determined.

### Set Share of CPU

When `set_cpu_share()` is called, it locks the `ptable` and calls `setsshr()`
in `scheduler.c`. The first thing is asserting the share is positive and the
overall CPU share doesn't exceed 80. If the current process is located in
MLFQ, `qpop()` move it out of MLFQ and reset `proc.qlev` and `proc.qlepsd`
that are used for MLFQ. Then the process is pushed on the head of the queue.

### Scheduling Principles

The scheduler maintains all shares and passes of the processes through
`proc.sshr` and `proc.spass`. When `nextproc()` is called, it traverses
`stride.queue` and find the minimum of `proc.spass`. Then it compares the
value with `stride.mlfqpass` and choose the smaller one. After the next
process determined, the corresponding pass value will be increased.

After finding the candidate process in stride, the MLFQ can be chosen.
However, `nextmlfq()` might return zero, which means that
there is no runnable process in MLFQ. In such a case, the pre-determined
process from stride is decided to run next. If there is no runnable process in
stride too, `nextproc()` eventually returns zero.

## Scheduling Scenario

### Analysis Approach

Testing scnarios should check all specifications of MLFQ and Stride
scheduling. Their observable attributes are the followings.

* MLFQ: Execution count of Level 2 is twice than Level 1.
* Stride scheduling: The process execution count proportionally increases as
    much as its CPU share.

### Metrics

#### Scenario 1. MLFQ

Execute a single process which runs long. Its priority level will be move down
and increase by boosting. The count ratio is same as time allotment's one.

| Level | Trial 1 | Trial 2 | Trial 3 | Trial 4 |
|:-----:| -------:| -------:| -------:| -------:|
| 0     | 1509    | 1099    | 1101    | 1222    |
| 1     | 2831    | 2236    | 2076    | 2173    |
| 2     | 15701   | 13014   | 10712   | 10641   |
| total | 20041   | 16349   | 13889   | 14036   |

#### Scenario 2. Stride Scheduling

In simultaneously, 4 processes which have different CPU shares run and their
execution counts will be recorded.

| Share | Trial 1 | Trial 2 | Trial 3 | Trial 4 |
|:-----:| -------:| -------:| -------:| -------:|
| 5     | 9441    | 11318   | 11978   | 12300   |
| 10    | 17298   | 23088   | 23179   | 23296   |
| 15    | 29070   | 32883   | 34380   | 35678   |
| 20    | 40119   | 47347   | 47331   | 45160   |

## Limitations

Even if this scheduling exactly follows the specifications,
there are a few things that can be improved.

### ptable.lock Manages The Overall

For all functions, including MLFQ and stride scheduling, modifying `proc`,
acquiring `ptable.lock` is done. Although such a sole locking mechanism
guarantees the correctness, it can be a bottleneck of the concurrent
performance.

### Less Detailed Account CPU Time

While checking time allotment in MLFQ, `proc.qelpsd` is used and it increases
as much as ticks does. It is also increased when `sys_yield()` is called, which causes excessive time estimation if the process yields within a tick.

### Accumulative Passes

`proc.spass` counts the passes of the process in the stride scheduling and it
increases accumulatively. That is, there is no decrement while running. If
there is a long-run process which exceeds the maximum of integer range, the
scheduling won't work correctly.

## References

* [Stride Scheduling Paper](https://rcs.uwaterloo.ca/papers/stride.pdf)
