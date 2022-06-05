#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

struct {
  struct spinlock lock;
} lwpgroup;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  p->hpsz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");
  p->schidx = 0;
  p->lwpidx = 0;

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  qpush(p);
  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);

  if(curproc->oproc != 0)
    sz = (curproc->oproc->hpsz > curproc->oproc->sz) ? curproc->oproc->hpsz : curproc->oproc->sz;
  else
    sz = (curproc->hpsz > curproc->sz) ? curproc->hpsz : curproc->sz;

  if(curproc->lwpidx > 0)
    sz = curproc->oproc->hpsz;

#ifdef LWPFKDEBUG
  cprintf("[growproc] (%d) start %d\n", curproc->pid, curproc->sz);
#endif

  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  }

  if(curproc->oproc != 0){
    curproc->oproc->hpsz = sz;
    if(curproc->oproc->sz < sz)
      curproc->oproc->sz = sz;
  }

  curproc->hpsz = sz;
  if(curproc->sz < sz)
    curproc->sz = sz;

  release(&ptable.lock);

  switchuvm(curproc);

#ifdef LWPFKDEBUG
  cprintf("[growproc] (%d) end %d\n", curproc->pid, curproc->sz);
#endif
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  uint sz;
  struct proc *np;
  struct proc *curproc = myproc();

#ifdef LWPFKDEBUG
  cprintf("[fork] (%d) start\n", curproc->pid);
#endif

  // Allocate process.
  if((np = allocproc()) == 0){
#ifdef LWPFKDEBUG
    cprintf("[fork] (%d) allocproc fail\n", curproc->pid);
#endif
    return -1;
  }

  // Copy process state from proc.
  sz = (curproc->hpsz > curproc->sz) ? curproc->hpsz : curproc->sz;
#ifdef LWPFKDEBUG
    cprintf("[fork] (%d) %d sz %d\n", curproc->pid, np->pid, sz);
#endif
  if((np->pgdir = copyuvm(curproc->pgdir, sz, curproc->sksz)) == 0){
#ifdef LWPFKDEBUG
    cprintf("[fork] (%d) %d copyuvm fail\n", curproc->pid, np->pid);
#endif
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->sksz = curproc->sksz;
  np->hpsz = curproc->hpsz;
  np->parent = curproc;
  np->oproc = curproc->oproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  np->schidx = -1;
  np->lwpidx = 0;

  pid = np->pid;

  acquire(&ptable.lock);

  if(np->oproc == 0){
    qpush(np);
  }else{
    np->schproc = curproc->schproc;
  }

  np->state = RUNNABLE;

  release(&ptable.lock);

#ifdef LWPFKDEBUG
  cprintf("[fork] (%d) %d end\n", curproc->pid, np->pid);
#endif

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        if(p->oproc == 0)
          qpop(p);
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
#ifdef LWPFKDEBUG
    cprintf("[wait] (%d) sleep\n", curproc->pid);
#endif
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

// Find the topmost process
struct proc*
schproc(struct proc *curproc)
{
  if(curproc->parent == initproc || curproc->oproc == 0)
    return curproc;
  else
    return schproc(curproc->oproc);
}
  
// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
thread_create(thread_t *thread, void* (*start_routine)(void *), void *arg)
{
  // Based on fork()
  int i, sidx[NPROC] = {0};
  uint sksz, sp, ustack[2];
  struct proc *np, *p;
  struct proc *curproc = myproc();

#ifdef LWPDEBUG
  cprintf("[t_create] (%d) start\n", curproc->pid);
#endif

  // Avoid double-mark in stack index
  acquire(&lwpgroup.lock);

  // Mark used stack index
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->oproc == curproc)
      sidx[p->lwpidx] = 1;
  }
  release(&ptable.lock);

  // Get available stack index
  for(i = 1; i < NPROC; i++){
    if(sidx[i] == 0)
      break;
  }

  // No available stack index
  if(i == NPROC){
    release(&lwpgroup.lock);
#ifdef LWPDEBUG
    cprintf("[t_create] (%d) no stack index\n", curproc->pid);
#endif
    return -1;
  }

#ifdef LWPDEBUG
    cprintf("[t_create] (%d) stack index %d selected\n", curproc->pid, i);
#endif

  // Allocate process
  if((np = allocproc()) == 0){
    release(&lwpgroup.lock);
#ifdef LWPDEBUG
    cprintf("[t_create] (%d) allocproc fail\n", curproc->pid);
#endif
    return -1;
  }

  // Update lwp
  np->oproc = curproc;
  np->lwpidx = i;

  // Based on exec()
  // Allocate stack segment
  sksz = PGROUNDUP(KERNBASE - ((np->lwpidx+1) * 2*PGSIZE));

  if((sksz = allocuvm(curproc->pgdir, sksz, sksz + 2*PGSIZE)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    release(&lwpgroup.lock);

#ifdef LWPDEBUG
    cprintf("[t_create] (%d) %d allocuvm fail on %d\n", np->oproc->pid, np->pid, np->lwpidx);
#endif

    return -1;
  }
  clearpteu(curproc->pgdir, (char*)(sksz - 2*PGSIZE));
  sp = sksz;

#ifdef LWPDEBUG
  cprintf("[t_create] (%d) %d allocuvm on %d, %d\n", np->oproc->pid, np->pid, np->lwpidx, sksz);
  cprintf("[t_create] (%d) %d sz: %d, sksz: %d\n", np->oproc->pid, np->pid, np->oproc->sz, sksz);
#endif

  release(&lwpgroup.lock);

  // Set stack segment
  ustack[0] = 0xffffffff;
  ustack[1] = (uint)arg;

  sp -= 2*4;

  if(copyout(curproc->pgdir, sp, ustack, 2*4) < 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    deallocuvm(curproc->pgdir, sksz + 2*PGSIZE, sksz);

#ifdef LWPDEBUG
    cprintf("[t_create] (%d) copyout fail\n", curproc->pid);
#endif
    return -1;
  }

  // Update proc
  np->pgdir = curproc->pgdir;
  np->sz = sksz;
  np->hpsz = curproc->hpsz;
  np->sksz = sksz - 2*PGSIZE;
  np->parent = curproc;
  np->schproc = schproc(curproc);
  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  // Update trap frame
  *np->tf = *curproc->tf;
  np->tf->eip = (uint)start_routine;
  np->tf->esp = sp;

  // Copy file descriptor
  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  *thread = np->pid;

#ifdef LWPDEBUG
  cprintf("[t_create] (%d) end %d\n", curproc->pid, np->pid);
#endif
  return 0;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
thread_exit(void *retval)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc->oproc == 0)
    panic("non-LWP exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->oproc);

  // Pass abandoned children to oproc.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->oproc == curproc){
      p->parent = curproc->oproc;
      if(p->state == ZOMBIE)
        wakeup1(curproc->oproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->retval = retval;
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a LWP to exit and return zero.
// Return -1 if this process has no children.
int
thread_join(thread_t thread, void **retval)
{
  int exist;
  struct proc *p;
  struct proc *curproc = myproc();

#ifdef LWPDEBUG
  cprintf("[t_join  ] (%d) start %d\n", curproc->pid, thread);
#endif
  
  acquire(&ptable.lock);
  for(;;){
    exist = 0;
    // Scan through table looking for exited LWP.
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->pid != (int)thread)
        continue;
      exist = 1;

#ifdef LWPDEBUG
  cprintf("[t_join  ] (%d) exist %d\n", curproc->pid, p->pid);
#endif

      if(p->state == ZOMBIE){
        // Found one.
        *retval = p->retval;

#ifdef LWPDEBUG
        cprintf("[t_join  ] (%d) ZOMBIE %d\n", curproc->pid, thread);
        cprintf("[t_join  ] (%d) deallocuvm on %d, %d\n", curproc->pid, p->lwpidx, p->sksz);
#endif

        p->oproc = 0;
        p->schproc = 0;
        p->schidx = 0;
        p->lwpidx = 0;
        kfree(p->kstack);
        p->kstack = 0;
        deallocuvm(p->pgdir, p->sksz + 2*PGSIZE, p->sksz);
        p->sksz = 0;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);

#ifdef LWPDEBUG
        cprintf("[t_join  ] (%d) retval of %d is %d\n", curproc->pid, thread, (int)*retval);
        cprintf("[t_join  ] (%d) end %d\n", curproc->pid, thread);
#endif

        return 0;
      }else{
        break;
      }
    }

#ifdef LWPDEBUG
    cprintf("[t_join  ] (%d) ZOMBIE not %d\n", curproc->pid, thread);
#endif

    // No point waiting if we don't have a thread.
    if(!exist || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for oproc to join.
    sleept(curproc, &ptable.lock);

#ifdef LWPDEBUG
    cprintf("[t_join  ] sleep after %d %d\n", curproc->pid, thread);
#endif
  }
}

struct proc*
nextlwp(struct proc *schproc)
{
  int i, schidx = -1;
  struct proc *p, *nextproc = 0;

  for(i = schproc->schidx+1; i < NPROC; i++){
    p = &ptable.proc[i];
    if(p->schproc == schproc && p->state == RUNNABLE){
      nextproc = p;
      schidx = i;
      break;
    }
  }

  if(schidx == -1)
    nextproc = schproc;

  schproc->schidx = schidx;

  return nextproc;
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p = 0;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    p = nextproc();
    if(p != 0){
      // Select next LWP if available
      p = nextlwp(p);
#ifdef SCHDEBUG
      //cprintf("[nextlwp] (%d)\n", p->pid);
#endif

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
int
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
  return 0;
}

// wrapper for yield().
int
sys_yield(void)
{
  if(myproc()->qlev >= 0){
    acquire(&ptable.lock);
    myproc()->qelpsd++;
    qdown(myproc());
    release(&ptable.lock);
  }
  return yield();
}

void
mlfqelpsd(int mlfqticks)
{
  acquire(&ptable.lock);
  
  struct proc *schproc = myproc()->schproc;
  if(schproc == 0)
    schproc = myproc();

  schproc->qelpsd++;
  qdown(schproc);
  qboost(mlfqticks);

  release(&ptable.lock);
}

int
set_cpu_share(int share)
{
  acquire(&ptable.lock);
  int res = setsshr(myproc(), share);
  release(&ptable.lock);
  return res;
}

// wrapper for set_cpu_share()
int
sys_set_cpu_share(void)
{
  int share;
  argint(0, &share);
  return set_cpu_share(share);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}


// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep1(void *chan, struct spinlock *lk, int tsleep)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  //if(lk == 0)
  //  panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if(lk != 0)
      release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = (tsleep == 0) ? SLEEPING : TSLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if(lk != 0)
      acquire(lk);
  }
}

void
sleep(void *chan, struct spinlock *lk)
{
  sleep1(chan, lk, 0);
}

void
sleept(void *chan, struct spinlock *lk)
{
  sleep1(chan, lk, 1);
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if((p->state == SLEEPING || p->state == TSLEEPING) && p->chan == chan){
      p->state = RUNNABLE;
#ifdef XEMDEBUG
      if(myproc() != 0)
        cprintf("[wakeup1] (%d) wakeup %d - %d\n", myproc()->pid, chan, p->pid);
#endif
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING || p->state == TSLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [TSLEEPING] "tsleep",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING || p->state == TSLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
