# Light-weight Process

> Designs of Light-weight Process

## Process and Thread

> **Notes.** The common knowledges about process, thread and POSIX thread
> mainly referred from [OSTWP](https://pages.cs.wisc.edu/~remzi/OSTEP/) and
> [Open Group Publications](https://pubs.opengroup.org/onlinepubs/9699919799.2018edition/)

### Differences

Both process and thread allow muptiple programs to run at the same time.  They
have their own private state such as PC (Program Counter) and some registers.
Unlike a process, threads can share the address space with their siblings. That
is, within a single virtual memory, including codes and heaps, can be accessed
by multiple threads.

However, each thread has its own stack segment. So the threads have their own
local variables, parameters, return values and others. Finally, while running
independent works, it is easy for threads to share data with others, which is
not for multi-processes.

### Context Switching

Context switching of process changes CPU states and memories entirely. Since
the states are stored in PCB, a running process saves the current states then
another states of ready process are restored before it runs. Switching virtual
memory also should be performed. CR3 register that holds the value for the
process' page table is changed with the value of soon-to-running process.

For a thread, the works of context switching are reduced comapred to the
processs. Changing the CPU states is also done due to the independent runs.
However, if there is a context switching between thread siblings directly,
the pointer of the address space can remain the same. This is because they
share the same address space (except stack segment).

## POSIX thread

### pthread_create

```c
int
pthread_create(pthread_t      *thread,
         const pthread_attr_t *attr,
               void           *(*start_routine)(void*),
               void           *arg);
```

It creates a new thread. If it successes, it will return zero. `thread`
parameter gets an unique ID returned from the function call. `attr` contains
the attributes of the thread. If it passed with `NULL`, it will be configured
in default. `start_routine` indicates a function pointer that the thread
starts to run. `arg` has the parameter of the `start_routine` for the
execution.

### pthread_join

```c
int
pthread_join(pthread_t thread,
             void      **value_ptr);
```

It waits for the completion of the thread. `thread` has the value generated
from `pthread_create` and indicates which thread to wait. `value_ptr` contains
the pointer for the return value. If the return value is not used, it is safe
to pass `NULL` on the parameter.

### pthread_exit

```c
void
pthread_exit(void *value_ptr);
```

It terminates a single thread and makes the `value_ptr` available on the
return value.

## Basic LWP Operations

In my opinion, the overall designs of milestone2 can be adopted from `fork`,
`exit` and `wait` functions which have already existed in xv6. The keypoint
will be the recognition about *'Which operations in LWP should be
preserved or modified from ones in process'*.

### Create

From the basis of `fork()`, `thread_create` will be changed to support the creation
of a LWP. Instead of `copyuvm()`, the virtual memory will allocate a new stack
segment. Its parent will be the same with others parent in the LWP group. Its
instruction pointer will be set to the `start_routine` and the parameters also
will be passed on the stack.

### Exit

`thread_exit` will be based on `exit()`. It stores the `value_ptr` in the
`proc` and marks itself as `ZOMBIE`.

### Join

`thread_join` will mainly follow the `wait()`. It finds a corresponding LWP
and waits until the LWP's state becomes `ZOMBIE`. It takes `value_ptr` from
the `proc`, reset the `proc` and return.

## Integration

In my opinion, the objective of milestone3 is to make operating systems
treat a process and a LWP adequately. In order to accomplish this,
it is important to determine which operations related to LWP should be done
in perspective of a single LWP or a LWP group.

### With Other System Calls

When a system call is revoked, it should determine whether current `proc` is a
process or a LWP. In addiiton, it also takes care of other LWPs during the
operations.  In `exit()`, `exec()` and `kill()`, clean-up will be performed
for all LWPs in the same group in force. A pipe will be synchronously shared
among the same LWP group in `pipe()` operations.  `sleep()` performs for each
LWP separately and terminates if other LWP terminates.

### Scheduling

To consider LWPs scheduling together with others in the same group, one of
them will be treated as a representative. For all LWPs, time quantum,
priority, time allotment, ticket, pass and others related to scheduling will
be accounted only in a representative. The scheduler will also exclude
non-representative LWPs while choosing a `proc` to run. If a representative
LWP is selected by a scheduler, it internally select a LWP to run following
the RR manner.
