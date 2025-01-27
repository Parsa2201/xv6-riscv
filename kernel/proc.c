#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fcntl.h"
#include "fs.h"
#include "sort.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];
// struct pri_proc lowpri_proc[NPROC];
// struct pri_proc highpri_proc[NPROC];
// struct spinlock dir_lock;

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

// Next thread id available
int nextthrid = 1;
struct spinlock thrid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// stores every user-trap information and its grand-parents
// stores at most 4 grand-parents for every report
struct {
  struct report reports[MAX_REPORT_BUFFER_SIZE];
  int numberOfReports;
  int writeIndex;       // Circle loop
} _internal_report_list = {0};

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
  // struct pri_proc *low;
  // struct pri_proc *high;

  initlock(&pid_lock, "nextpid");
  initlock(&thrid_lock, "nextthrid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
  // for (low = lowpri_proc; low < &lowpri_proc[NPROC]; low++) {
  //   initlock(&low->lock, "low_proc");
  //   low->p = 0;
  //   low->used = 0;
  // }
  // for (high = highpri_proc; high < &highpri_proc[NPROC]; high++) {
  //   initlock(&high->lock, "high_proc");
  //   high->p = 0;
  //   high->used = 0;
  // }
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

struct trapframe*
current_trapframe(struct proc *p)
{
  if (p->current_thread)
    return p->current_thread->trapframe;
  else
    return p->trapframe;
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
  // struct pri_proc *high;

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

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
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

  printf("making proc\n");
  p->proc_thread.id = allocthrid();
  p->proc_thread.join = 0;
  p->proc_thread.state = THREAD_RUNNABLE;
  p->proc_thread.trapframe = p->trapframe;
  p->thread_count = 0;
  p->current_thread = 0;

  for (struct thread *t = p->threads; t <= &p->threads[MAX_THREAD]; t++) {
    t->id = 0;
    t->join = 0;
    t->state = THREAD_FREE;
    t->trapframe = 0;
  }

  // init proc usage
  p->usage.sum = 0;
  p->usage.start = ticks;
  p->usage.last_tick = ticks;
  p->usage.deadline = (uint)-1;

  // set quota to infinity = the proc can live forever
  p->usage.quota = (uint)-1;

  p->priority = HIGH_PRIORITY;

  // for (high = highpri_proc; high < &highpri_proc[NPROC]; high++) {
  //   acquire(&high->lock);
  //   if(high->used == 0) {
  //     high->p = p;
  //     high->used = 1;
  //   }
  //   else
  //   {
  //     release(&high->lock);
  //   }
  // }

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

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

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

  acquire(&p->lock);
  // free all threads of the process
  if (p->thread_count > 0)
    for (struct thread *t = p->threads; t <= &p->threads[MAX_THREAD]; t++) {
      if (t->state != THREAD_FREE) {
        p->current_thread = t;
        thread_exit(0, 0);
      }
    }

  p->proc_thread.id = 0;
  p->proc_thread.join = 0;
  p->proc_thread.state = THREAD_FREE;
  p->proc_thread.trapframe = 0;
  // printf("exiting process1\n");
  release(&p->lock);
  // printf("exiting process2\n");

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

  release(&wait_lock);

  // printf("exiting process3\n");

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

// runs the process and its threads for one cycle
// p->lock should be acquired before calling it
void
run_proc(struct cpu *c, struct proc *p)
{
  if (p->state != RUNNABLE)
    return;
  // Switch to chosen process.  It is the process's job
  // to release its lock and then reacquire it
  // before jumping back to us.
  p->state = RUNNING;
  c->proc = p;
  if (p->proc_thread.state != THREAD_JOINED) {
    p->proc_thread.state = THREAD_RUNNING;
    p->current_thread = &p->proc_thread;

    p->usage.last_tick = ticks;
    swtch(&c->context, &p->context);
    if (p->state == RUNNABLE)
      p->usage.sum += ticks - p->usage.last_tick;
    p->usage.last_tick = ticks;

    if (p->state == ZOMBIE || p->state == UNUSED)
    {
      p->current_thread = 0;
      goto stop;
    }
  }

  struct trapframe proc_trapframe = *(p->trapframe);
  int proc_exited = 0;
  int thread_exited = 0;

  if (p->thread_count > 0) {
    for (struct thread *t = p->threads; t < &p->threads[MAX_THREAD]; t++)
    {
      if (t->state != THREAD_RUNNABLE || t->state == THREAD_JOINED)
        continue;
      if (p->state == ZOMBIE || p->state == UNUSED) {
        proc_exited = 1;
        break;
      }

      p->state = RUNNING;
      t->state = THREAD_RUNNING;
      p->current_thread = t;

      // change the process trapframe with current thread trapframe
      *(p->trapframe) = *(t->trapframe);
      // uint start = ticks;
      swtch(&c->context, &p->context);
      // uint end = ticks;
      // p->usage.sum += end - start;

      // check if the process has exited
      if (p->state == ZOMBIE || p->state == UNUSED) {
        proc_exited = 1;
        break;
      }
      if (t->state == THREAD_FREE) {
        thread_exited = 1;
        break;
      }

      // update the trapframe of the current thread
      *(t->trapframe) = *(p->trapframe);
      // TODO: check for joined thread and process (main thread)
      p->state = RUNNING;
      if (t->state != THREAD_JOINED)
        t->state = THREAD_RUNNABLE;
    }
  }
  p->current_thread = 0;

  // restore the main thread trapframe to the process
  if (proc_exited == 0) {
    *(p->trapframe) = proc_trapframe;
    if (thread_exited) {
      // printf("restoring the process when thread is exited\n");
      // printf("p->epc = %ld\n", p->trapframe->epc);
    }

    p->state = RUNNABLE;
  }

  stop:
  // Process is done running for now.
  // It should have changed its p->state before coming back.
  c->proc = 0;
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler2(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
    intr_on();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        run_proc(c, p);
        found = 1;
      }

      release(&p->lock);
    }
    if(found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      intr_on();
      asm volatile("wfi");
    }
  }
}

// changes the priority of the process using some factors.
// the process lock should be acquired
// for deadlock prevention, 
enum pri
adjpri(struct proc *p)
{
  if (p->state != RUNNABLE)
    return HIGH_PRIORITY;

  uint quota = p->usage.quota;
  uint sum = p->usage.sum;
  enum pri priority = p->priority;
  
  if (quota == -1) {
    // change it's priority
    return HIGH_PRIORITY;
  }
  else if (sum > quota) {
    // change it's priority
    return LOW_PRIORITY;
  }
  return priority;
}

int
proccomp(struct proc *p1, struct proc *p2)
{
  if (p1->usage.sum > p2->usage.sum)
    return 1;
  if (p1->usage.sum < p2->usage.sum)
    return -1;
  
  if (p1->usage.deadline > p2->usage.deadline)
    return 1;
  if (p1->usage.deadline < p2->usage.deadline)
    return -1;
  return 0;
}

// It finds the next process to schedule
// Returns 1 if found, 0 if not.
// If found, it acquires its lock.
int
find_next_sched_proc(struct proc **p_ptr, enum pri queue)
{
  struct proc *p = 0;
  int found = 0;
  // uint sum;

  for (struct proc *pp = proc; pp < &proc[NPROC]; pp++)
  {
    // printf("acquiring pp->lock [high priority]\n");
    acquire(&pp->lock);
    pp->priority = adjpri(pp);

    if (pp->state == RUNNABLE && pp->priority == queue)
    {
      if (found == 0)
      {
        p = pp;
        // sum = pp->usage.sum;
        found = 1;
        continue;
      }
      else if (proccomp(pp, p) == -1) // (pp->usage.sum < sum)
      {
        release(&p->lock);
        p = pp;
        // sum = pp->usage.sum;
        continue;
      }
    }
    release(&pp->lock);
  }

  *p_ptr = p;
  return found;
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
//
// Note: this scheduler runs the processes with
// lowest cpu usage and highest priority
void 
scheduler(void)
{
  struct proc *p = 0;
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;) {
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
    intr_on();

    if (find_next_sched_proc(&p, HIGH_PRIORITY))
      goto found;
    if (find_next_sched_proc(&p, LOW_PRIORITY))
      goto found;

    // nothing to run; stop running on this core until an interrupt.
    intr_on();
    asm volatile("wfi");
    continue;

    found:
    // p->lock is already acquired and
    // should be released after done with it.
    if (p->usage.deadline != (uint)-1 && p->usage.sum  > p->usage.deadline) {
      printf("killing pid=%d with sum=%u and deadline=%u\n", p->pid, p->usage.sum, p->usage.deadline);
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
    } else {
      // printf("pid=%d has sum=%d\n", p->pid, p->usage.sum);
    }
    run_proc(c, p);
    release(&p->lock);
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
  if(mycpu()->noff != 1) {
    printf("noff = %d\n", mycpu()->noff);
    panic("sched locks");
  }
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
  // printf("yielding\n");
  struct proc *p = myproc();
  // printf("t.epc is %ld\n", p->trapframe->epc);
  acquire(&p->lock);
  p->state = RUNNABLE;
  if (p->proc_thread.state == THREAD_RUNNING)
    p->proc_thread.state = THREAD_RUNNABLE;
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
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();
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
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

// Returns processes that are child or grand-child of this process in *cp
// Returns 0 on success, -1 on error.
int
child_processes(struct child_processes *cp_result)
{
  struct proc *parent = myproc();
  struct proc *child;
  struct child_processes cp = {0};

  for (child = proc; child < &proc[NPROC]; child++){
    struct proc *temp_child = child;
    int num_tries = 0;
    while (temp_child->state != UNUSED && temp_child->pid != parent->pid && num_tries < NPROC)
    {
      temp_child = temp_child->parent;
      if (temp_child == 0)
        break;
      if (temp_child->pid == parent->pid){
        // copy content of child to result(cp)
        strncpy(cp.processes[cp.count].name, child->name, 16);
        cp.processes[cp.count].pid = child->pid;
        cp.processes[cp.count].ppid = child->parent->pid;
        cp.processes[cp.count].state = child->state;

        // increase number of children by 1
        cp.count++;

        break;
      }
      num_tries++;
    }
    if (num_tries == NPROC)
      return -1;
  }
  return copyout(parent->pagetable, (uint64)cp_result, (char *)&cp, sizeof(cp));
}

// Check if value is in list or not
// Returns 1 on true, 0 on false.
// For local use of report traps
int is_in(int value, int list[], int len)
{
  for (int i = 0; i < len; i++)
    if (value == list[i])
      return 1;
  return 0;
}

// returns traps that have been made by a process or its grand-children.
// it works only if the child is in depth of 4
// returns at most 10 traps in order of occurrence 
// Returns 0 on success, -1 on error.
int
report_traps(struct report_traps *rt_result)
{
  struct proc *p = myproc();

  #define irl _internal_report_list // for ease of use

  // The index in irl that is oldest among others
  int beg_indx = irl.numberOfReports == MAX_REPORT_BUFFER_SIZE ? irl.writeIndex : 0;

  // choose the reports that are in the children of thils process
  struct report_traps rt = {0};
  for (int i = 0; i < irl.numberOfReports; i++){
    // circle_index mapes i (0 to n) into irl indecies (beg_index to n) U (0 to beg_index) 
    int circ_indx = i + beg_indx;
    if (circ_indx >= MAX_REPORT_BUFFER_SIZE)
      circ_indx -= MAX_REPORT_BUFFER_SIZE;

    if (is_in(p->pid, irl.reports[circ_indx].parents, irl.reports[circ_indx].parents_count)){
      // add the report[circle_index] to the result
      rt.reports[rt.count] = irl.reports[circ_indx];
      rt.count++;
    }
  }

  // copy the result from kernel space to user space
  return copyout(p->pagetable, (uint64)rt_result, (char *)&rt, sizeof(rt));
}

struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

// addes a user trap with the given informations to _internal_report_list
// it also stores it in a file
void
add_trap(int scause, int sepc, int stval)
{
  struct proc *p = myproc();

  // acquire(&p->lock);

  // create a report and save it in internal-report-list
  struct report *rpt = &(_internal_report_list.reports[_internal_report_list.writeIndex]);
  rpt->pid = p->pid;
  strncpy(rpt->pname, p->name, 16);
  rpt->scause = scause;
  rpt->sepc = sepc;
  rpt->stval = stval;

  struct proc *parent = p->parent;
  while (parent != 0 && parent->state != UNUSED && rpt->parents_count < 4){
    // Add the parent to grand parent list
    rpt->parents[rpt->parents_count] = parent->pid;
    rpt->parents_count++;

    // Fild the grand grand parent
    parent = parent->parent;
  }

  // save the report into "reports" file
  int fd;
  if ((fd = sys_open_kernel("reports.bin", O_WRONLY | O_CREATE)) < 0)
    printf("Opening reports.bin failed.\n");
  if (sys_write_kernel(fd, rpt, sizeof(struct report)) < 0)
    printf("Writing to reports.bin failed.");
  if (sys_close_kernel(fd) < 0)
    printf("Closing reports.bin failed.");
  

  // update internal-report-list parameters
  if (_internal_report_list.numberOfReports < MAX_REPORT_BUFFER_SIZE)
    _internal_report_list.numberOfReports++;

  _internal_report_list.writeIndex++;

  if (_internal_report_list.writeIndex >= MAX_REPORT_BUFFER_SIZE)
    _internal_report_list.writeIndex = 0;

  // release(&p->lock);
}

// loads last 10 traps from reports.bin into manim memory
// Returns 0 on success, -1 on error.
int
load_traps()
{
  int fd;
  if ((fd = sys_open_kernel("reports.bin", O_RDONLY)) < 0){
    printf("Opening reports.bin failed.\n");
    return -1;
  }

  // set the offset to the last 10 reports in the file
  int report_count = 0;
  int file_size = 0;
  if(get_file_size(fd, &file_size) < 0){
    printf("Error getting file size!\n");
    return -1;
  }
  if (file_size >= 10 * sizeof(struct report))
  {
    get_offset_before_end(fd, 10 * sizeof(struct report));
    report_count = 10;
  } else {
    move_offset_start(fd);
    report_count = file_size / sizeof(struct report);
  }
  printf("report count = %d\n", report_count);
  printf("file_size = %d\n", file_size);
  int offset;
  get_offset(fd, &offset);
  printf("offset = %d\n", offset);

  // read and load reports
  for (int i = 0; i < report_count; i++)
  {
    if(sys_read_kernel(fd, _internal_report_list.reports + i, sizeof(struct report)) < 0){
      printf("Error reading reports.bin\n");
      _internal_report_list.numberOfReports = i; // save count of currently loaded reports
      return -1;
    }
    printf("%s\n", _internal_report_list.reports[i].pname);
  }
  _internal_report_list.numberOfReports = report_count;
  if(report_count < 10)
    _internal_report_list.writeIndex = report_count;
  
  if(sys_close_kernel(fd) < 0){
    printf("Error closing file");
    return -1;
  }

  return 0;
}

int
allocthrid()
{
  int thrid;
  
  acquire(&thrid_lock);
  thrid = nextthrid;
  nextthrid = nextthrid + 1;
  release(&thrid_lock);

  return thrid;
}

// Creates a new thread for the current process.
// Returns the thread id in thread_id if the thread creation
// is successful. Gets the start function (function) and it's argument (arg)
// as the arguments. It also needs an allocated memory (user space) for the
// thread stack, and also it's size (in bytes).
// # Args
// - `uint *thread_id`: The id of the created thread
// - `void *(*function)(void *)`: The start function to be executed by the thread
// - `void *arg`: The parameter to be given to the start function
// - `void *stack`: A pointer to where the stack of the new thread should be pointing at
// - `uint64 stack_size`: The size of the given stack
// # Return
// Returns 0 on success, -1 on error.
int create_thread(uint *thread_id, void *(*function)(void *arg), void *arg, void *stack, uint64 stack_size) {
  struct thread *t;
  struct proc *p = myproc();

  // in case something went wrong finding the current process;
  if (p == 0)
    panic("didn't find current process");
    
  // Use locks for synchronization to ensure mutual exclusion
  acquire(&p->lock);

  // each process has a maximum limit of creating threads
  if (p->thread_count == MAX_THREAD) {
    release(&p->lock);
    return -1;
  }

  // Search for a free slot for a new thread in the threads array
  for (t = p->threads; t < &p->threads[MAX_THREAD]; t++) {
      if (t->state == THREAD_FREE) {
          goto found;
      }
  }
  
  // Release lock if no free space for a new thread is found
  release(&p->lock);
  return -1; // No space available for a new thread

found:
  // Mark the thread as runnable
  t->state = THREAD_RUNNABLE;
  
  // Assign a unique thread ID
  t->id = allocthrid();

  // Initialize the join variable
  t->join = 0;

  // Allocate a trapframe for the thread
  if ((t->trapframe = (struct trapframe *)kalloc()) == 0) {
      t->state = THREAD_FREE;
      release(&p->lock);
      return -1; // Failed to allocate trapframe
  }

  // Initialize the trapframe with default values
  memset(t->trapframe, 0, sizeof(*t->trapframe));
  
  // Set up the trapframe for the thread to execute the function
  t->trapframe->epc = (uint64)function;                   // Start function
  t->trapframe->sp = (uint64)stack + stack_size;     // Stack Pointer: stack allocated for the thread
  t->trapframe->a0 = (uint64)arg;
  t->trapframe->ra = (uint64)-1;
  // Argument for the function

  // Set the thread's ID to the provided thread_id
  if (copyout(p->pagetable, (uint64)thread_id, (char *)&t->id, sizeof(t->id)) < 0) {
    t->state = THREAD_FREE;
    kfree(t->trapframe);
    release(&p->lock);
    return -1;
  }

  p->thread_count++;

  // Release the lock as the thread creation process is now complete
  release(&p->lock);
  return 0; // Success
}

int join_thread(uint thread_id) {
  // printf("joining thread\n");
  struct proc *p = myproc();
  struct thread *t;
  int found = 0;

  acquire(&p->lock);

  // Finding the thread with the given ID
  for (t = p->threads; t < &p->threads[MAX_THREAD]; t++) {
      if (t->id == thread_id) {
          found = 1;
          break;
      }
  }

  // If the thread is not found, return error
  if (!found) {
      release(&p->lock);
      return -1; // No thread with this ID found
  }

  if (t->state == THREAD_FREE) {
    release(&p->lock);
    return -1;
  }

  p->current_thread->state = THREAD_JOINED;
  // t->join = p->current_thread->id;
  p->current_thread->join = t->id;
  // printf("%d joined on thread id: %d\n", t->join, thread_id);

  release(&p->lock);
  yield();
  usertrapret();
  return 0; // Success
}

void
thread_exit_caller(void)
{
  thread_exit(1, 1);
}

// # Arguments:
// - `should_acquire`: should we acquire the `p->lock` or not
//              1 means we should
//              0 means we shouldn't
// - `should_sched`: should we call `sched()` after exiting the thread or not
//              1 means we should
//              0 means we shouldn't
void thread_exit(int should_acquire, int should_sched)
{
  // printf("exiting thread1\n");
  struct proc *p = myproc();
  struct thread *t = p->current_thread;
  // printf("current thread id from exiting thread: %d\n", p->current_thread->id);

  // Acquire process lock
  if (should_acquire)
    acquire(&p->lock);

  // printf("exiting thread2\n");
  // Clean up resources
  if (t != &p->proc_thread && t->trapframe != 0) {
    kfree(t->trapframe);
    t->trapframe = 0;
  }
  t->state = THREAD_FREE;

  // printf("exiting thread3\n");
  // Decrement thread count
  if (t != &p->proc_thread)
    p->thread_count--;

  // Remove the thread from process
  p->current_thread = 0;

  // printf("exiting thread4\n");

  // printf("t->join = %d\n", t->join);
  // wakeup the joned thread
  for (struct thread *tt = p->threads; tt <= &p->threads[MAX_THREAD]; tt++)
    if (tt->join == t->id && tt->state == THREAD_JOINED) {
      tt->state = THREAD_RUNNABLE;
      tt->join = 0;
      // printf("thread %d is awake\n", t->join);
    }
  if (p->proc_thread.join == t->id) {
    p->proc_thread.state = THREAD_RUNNABLE;
    p->proc_thread.join = 0;
    // printf("thread %d is awake\n", t->join);
  }

  // printf("exiting thread5\n");

  // printf("exiting thread6, should acquire = %d\n", should_acquire);
  if (should_acquire)
    release(&p->lock);

  // Yield CPU
  if (should_sched) {
    yield();
    usertrapret();
  }
}

uint cpu_usage()
{
  struct proc *p = myproc();

  acquire(&p->lock);
  // the total usage of the process is:
  // 1. all the times that sched has run the proc for complete round (usage.sum)
  // 2. the last round of sched that has not been calculated yet (now - last_tick)
  uint sum = p->usage.sum + ticks - p->usage.last_tick;
  release(&p->lock);

  return sum;
}

int
top_cmp(void *v1, void *v2)
{
  struct top_proc_info *inf1 = (struct top_proc_info *)v1;
  struct top_proc_info *inf2 = (struct top_proc_info *)v2;

  if (inf1->usage.sum > inf2->usage.sum)
    return 1;
  if (inf1->usage.sum < inf2->usage.sum)
    return -1;
  return 0;
}

int
top(struct top *tp)
{
  struct top k_tp;
  k_tp.count = 0;

  for (struct proc *p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED) {
      // the first free entry
      struct top_proc_info *inf = &k_tp.procs[k_tp.count];

      // fill in the infos
      strncpy(inf->name, p->name, 16);
      inf->pid = p->pid;

      if (p != initproc) {
      struct proc *pp = p->parent;
      inf->ppid = pp->pid;
      } else {
        inf->ppid = 0;
      }

      inf->state = p->state;
      inf->usage = p->usage;
      // inf->usage.sum += ticks - inf->usage.last_tick;

      k_tp.count++;
    }
    release(&p->lock);
  }

  qsort(k_tp.procs, sizeof(struct top_proc_info), 0, k_tp.count - 1, top_cmp);

  struct proc *p = myproc();

  int result;
  result = copyout(p->pagetable, (uint64)&tp->count, (char *)&k_tp.count, sizeof(k_tp.count));
  if (result == -1)
    return -1;
  return copyout(p->pagetable, (uint64)&tp->procs, (char *)&k_tp.procs, sizeof(struct top_proc_info) * k_tp.count);
}

int
set_cpu_quota(int pid, int quota)
{
  struct proc *this_proc = myproc();
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid)
      goto found;
    release(&p->lock);
  }
  printf("pid %d not found\n", pid);
  release(&p->lock);
  return -1;

found:
  // ensure that the given pid in this process 
  // or a grandchild of this process.
  struct proc *pp = p;
  while (pp != this_proc && pp != 0)
    pp = p->parent;
  if (pp == 0) {
    printf("pid %d not a grandchild of %d\n", pid, this_proc->pid);
    release(&p->lock);
    return -1;
  }

  // change the quota
  p->usage.quota = quota;
  release(&p->lock);
  return 0;
}

// Similar to fork(), but it also sets a deadline for
// the child process. If the child process exceeds its
// time limit, it will automatically be terminated.
int
hotfork(int deadline)
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

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

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
  np->usage.deadline = deadline;
  release(&np->lock);

  return pid;
}
