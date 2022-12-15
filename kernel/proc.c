#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#ifdef MLFQ
struct proc *queue_top(struct Queue *q)
{
  if (q->head == q->tail)
    return 0;
  else
  return q->procs[q->head];
}

void queue_push(struct Queue *q, struct proc *element)
{
  if (q->size == NPROC)
    panic("Proccess limit exceeded");

  q->procs[q->tail] = element;
  q->tail++;
  if (q->tail == NPROC)
    q->tail = 0;
  q->size++;
}

void queue_pop(struct Queue *q)
{
  if (q->size == 0)
    panic("Empty queue");
  q->head++;
  if (q->head == NPROC)
    q->head = 0;
  q->size--;
}

void queue_remove(struct Queue *q, int pid)
{
  for (int curr = q->head; curr != q->tail; curr = (curr + 1) % (NPROC + 1))
  {
    if (q->procs[curr]->pid == pid)
    {
      struct proc *temp = q->procs[curr];
      q->procs[curr] = q->procs[(curr + 1) % (NPROC + 1)];
      q->procs[(curr + 1) % (NPROC + 1)] = temp;
    }
  }

  q->tail--;
  q->size--;
  if (q->tail < 0)
    q->tail = NPROC;
}
#endif

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

#ifdef MLFQ
  struct Queue mlfq[NUMQ];
#endif

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
  #ifdef MLFQ
    for(int i = 0 ; i < NUMQ ; i++)
    {
      mlfq[i].size  = 0;
      mlfq[i].head = 0;
      mlfq[i].tail  = 0;
    }
  #endif
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  p->is_on = 0;
  p->curr_ticks = 0;
  p->alarm_ticks = 0;
  p->handler = 0;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  if ((p->alarm_trapfr_cpy= (struct trapframe*)kalloc())==0)
  {
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  p->ctime = ticks;
  p->stime = 0;
  p->rtime = 0;
  p->exit_time=0;
#ifdef PBS
  p->nschds = 0;
  p->statprior = 60;
#endif
#ifdef LBS
  p->ntickets = 1;
#endif
#ifdef MLFQ
  p->qnum = 0;
  p->qctime = ticks;
  for(int i = 0 ; i < NUMQ ; i++)
  {
    p->inqtime[i] = 0;
  }
  p->inq = 0;
  p->runtime = 0;
#endif
  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;

  if (p->alarm_trapfr_cpy)
  kfree((void*)p->alarm_trapfr_cpy);

  p->alarm_trapfr_cpy=0;
  
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->rtime = 0;
  p->stime = 0;
#ifdef PBS
  p->nschds =0;
  p->statprior = 60;
#endif
#ifdef MLFQ
  p->qnum = 0;
  p->qctime = ticks;
  for(int i = 0 ; i < NUMQ ; i++)
  {
    p->inqtime[i] = 0;
  }
  p->inq = 0;
  p->runtime = 0;
#endif
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
#ifdef LBS
  p->ntickets = 1;
  initproc->ntickets = 1;
#endif
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

#ifdef LBS
  np->ntickets = p->ntickets;
#endif
  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);
    np->tracemask = p->tracemask;
  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  p->exit_time=ticks;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process  .
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.

void preemptandaging()
{
  #ifdef MLFQ
  struct proc *p = 0;
        for(p = proc ; p < &proc[NPROC]; p++)
      { 
        acquire(&p->lock);
        if(p->state == RUNNABLE && ticks - p->qctime >= AGING)
        {
          p->qctime = ticks;
        if (p->inq)
        {
          queue_remove(&mlfq[p->qnum], p->pid);
          p->inq = 0;
        }
        if (p->qnum)
          p->qnum--;
        }
        release(&p->lock);
      }

      for (p = proc; p < &proc[NPROC]; p++)
      {
        acquire(&p->lock);
        if (p->state == RUNNABLE && p->inq == 0)
        {
          queue_push(&mlfq[p->qnum], p);
          p->inq = 1;
        }
        release(&p->lock);
      }
  #endif
}
void
scheduler(void)
{
  struct proc *p = 0;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
  #ifndef MLFQ
    #ifdef FCFS
     struct proc * firstcome = 0;
    #endif

    #ifdef PBS
      struct proc * lowprior = 0;
    #endif

    #ifdef LBS
      struct proc * chosen = 0;
      int totaltickets = 0;
      unsigned long random;
      do_rand(&random);
      int currtickets = 0;
      struct proc * p2 = 0;
      for(p2 = proc ; p2 < &proc[NPROC]; p2++)
      {
        acquire(&p2->lock);
        if(p2->state == RUNNABLE)
          totaltickets += p2->ntickets;
        release(&p2->lock);
      }
      random = random % totaltickets + 1;
    #endif

    for(p = proc; p < &proc[NPROC]; p++) {

      acquire(&p->lock);
      if(p->state == RUNNABLE) {

        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        #ifdef RR

          p->state = RUNNING;
          c->proc = p;
          swtch(&c->context, &p->context);
          c->proc = 0;

        #endif
          
        #ifdef FCFS
          if(firstcome == 0)
          {
            firstcome = p;
            continue;
          }
          else if(p->ctime < firstcome->ctime)
          {
            release(&firstcome->lock);
            firstcome = p;
            continue;
          }
        #endif

        #ifdef PBS
          if(lowprior == 0)
          {
            lowprior = p;
            continue;
          }
          else
          {
            int cp = priority(p);
            int lp = priority(lowprior);
            if(cp < lp)
            {
              release(&lowprior->lock);
              lowprior = p;
              continue;
            }
            else if(cp == lp)
            {
              if(p->nschds < lowprior->nschds)
              {
                release(&lowprior->lock);
                lowprior = p;
                continue;
              }
              else if(p->nschds == lowprior->nschds)
              {
                if(p->ctime < lowprior->ctime)
                {
                  release(&lowprior->lock);
                  lowprior = p;
                  continue;
                }
              }
            }
          }
        #endif

        #ifdef LBS
          
          currtickets += p->ntickets;
          if(currtickets >= random)
          {
            chosen =p;
            break;
          }
        #endif
        // Process is done running for now.
        // It should have changed its p->state before coming back.
      }
      release(&p->lock);
    }
    #ifdef FCFS
      if(firstcome != 0)
      {
        firstcome->state = RUNNING;
        c->proc = firstcome;
        swtch(&c->context, &firstcome->context);
        c->proc = 0;
        release(&firstcome->lock);
      }
    #endif 
    #ifdef PBS
      if(lowprior !=0)
      {
        lowprior->nschds++;
        lowprior->rtime = 0;
        lowprior->stime =0;
        lowprior->state = RUNNING;
        c->proc = lowprior;
        swtch(&c->context, &lowprior->context);
        c->proc = 0;
        release(&lowprior->lock);
      }
    #endif
    #ifdef LBS
      if(chosen !=0)
      {
        chosen->state = RUNNING;
        c->proc = chosen;
        swtch(&c->context, &chosen->context);
        c->proc = 0;
        release(&chosen->lock);
      }
    #endif
  #endif
    #ifdef MLFQ

      struct proc* lowprior = 0;
      for(p = proc ; p < &proc[NPROC]; p++)
      { 
        if(p->state == RUNNABLE)
        {
          acquire(&p->lock);
          if(p->state == RUNNABLE && ticks - p->qctime >= AGING)
          {
            p->qctime = ticks;
          if (p->inq)
          {
            queue_remove(&mlfq[p->qnum], p->pid);
            p->inq = 0;
          }
          if (p->qnum)
            p->qnum--;
          }
          release(&p->lock);
        }
      }
      // for(p = proc ; p < &proc[NPROC]; p++)
      // {
      //   if(p->pid >= 9 && p->pid <=13)
      //   printf("%d,%d,%d\n", p->pid, p->qnum , ticks);
      // }
      for (p = proc; p < &proc[NPROC]; p++)
      {
        acquire(&p->lock);
        if (p->state == RUNNABLE && p->inq == 0)
        {
          queue_push(&mlfq[p->qnum], p);
          p->inq = 1;
        }
        release(&p->lock);
      }

      for (int i = 0; i < NUMQ; i++)
      {

        while (mlfq[i].size > 0)
        {
          p = queue_top(&mlfq[i]);
          acquire(&p->lock);
          queue_remove(&mlfq[i], p->pid);
          p->inq = 0;
          if (p->state == RUNNABLE)
          {

            p->qctime = ticks;
            lowprior = p;
            break;
          }
          release(&p->lock);
        }
        if (lowprior)
          break;
      }
      if (lowprior)
      {
        lowprior->state = RUNNING;
        c->proc = lowprior;
        lowprior->runtime = 0;

        swtch(&c->context, &lowprior->context);
        c->proc = 0;

        lowprior->qctime = ticks;
        release(&lowprior->lock);
      }
    #endif
  }
}


// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s %d %d", p->pid, state, p->name, p->rtime, p->exit_time);
    #ifdef LBS
      printf("%d %s %s %d %d", p->pid, state, p->name, p->rtime, p->ntickets);

    #endif
    #ifdef MLFQ
      printf("%d %s %s %d %d %d %d %d %d", p->pid, state, p->name, p->rtime, p->runtime, 1 << p->qnum, p->qnum, ticks - p->qctime, p->qnum);

    #endif
    #ifdef PBS
            printf("%d %s %s %d %d", p->pid, state, p->name, p->rtime, p->statprior);

    #endif
    printf("\n");
  }
}

int
waitx(uint64 addr, uint* wtime, uint* rtime)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          *rtime = np->rtime;
          *wtime = np->exit_time - np->ctime - np->rtime;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

void
update_time()
{
  struct proc *p;
//   int flag = 0;
//   for(p = proc ; p < &proc[NPROC]; p++)
//   {
//       if(p->pid>=4 && p->pid <=8)
//       {
// flag  =1;
//         printf("%d %d %d ", p->pid,p->qnum, ticks - 23);
//       }
//   }
//   if(flag)
//   printf("\n");

  for(p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if(p->state == RUNNING)
    {
      p->rtime++;
    }
    if(p->state == SLEEPING)
    {
      p->stime++;
    }
    release(&p->lock);
#ifdef MLFQ 
  if(p->state == RUNNING)
  {
    p->inqtime[p->qnum]++;
    p->runtime++;

  }
#endif
  }
}

#ifdef PBS
int max(int a, int b)
{
  if(a>b)return a;
  else return b;
}

int min(int a , int b)
{
  if(a < b ) return a;
  else 
    return b;
}


int priority(struct proc * p)
{
  int nice = 5;
  if((p->rtime + p->stime) !=0)
  {
    nice = (p->stime * 10)/(p->rtime + p->stime);
  }
  int dp ;
  dp = max(0, min(p->statprior - nice + 5, 100));
  return dp;
}

#endif

int setpriority(int priority, int pid)
{
  #ifdef PBS
    struct proc *p;
    int old = -1, found = 0, flag = 1;

    for(p = proc ; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if(p->pid == pid)
      {
        old = p->statprior;
        p->statprior = priority;
        found = 1;
        if(old < priority)
        {
            p->rtime = 0;
            p->stime = 0;
            flag = 0;
            release(&p->lock);
            yield();
        }
        if(flag)  
          release(&p->lock);
        break;
      }
      release(&p->lock);
    }
    if(!found)
      printf("Wrong pid\n");

    return old;
  #endif
  return -1;
}

int settickets(int numbertickets)
{
  #ifdef LBS
    struct proc * p = myproc();
    if(p != 0)
    {
      p->ntickets += numbertickets;
      return numbertickets;
    }
    return -1;
  #endif
  return -1;
}

int
do_rand(unsigned long *ctx)
{
/*
 * Compute x = (7^5 * x) mod (2^31 - 1)
 * without overflowing 31 bits:
 *      (2^31 - 1) = 127773 * (7^5) + 2836
 * From "Random number generators: good ones are hard to find",
 * Park and Miller, Communications of the ACM, vol. 31, no. 10,
 * October 1988, p. 1195.
 */
    long hi, lo, x;

    /* Transform to [1, 0x7ffffffe] range. */
    x = (*ctx % 0x7ffffffe) + 1;
    hi = x / 127773;
    lo = x % 127773;
    x = 16807 * lo - 2836 * hi;
    if (x < 0)
        x += 0x7fffffff;
    /* Transform to [0, 0x7ffffffd] range. */
    x--;
    *ctx = x;
    return (x);
}