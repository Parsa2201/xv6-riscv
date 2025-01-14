#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
//#include <pthread.h>
uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_child_processes(void)
{
  struct child_processes *cp;
  argaddr(0, (uint64 *) &cp);
  return child_processes(cp);
}

uint64
sys_report_traps(void)
{
  struct report_traps *rt;
  argaddr(0, (uint64 *)&rt);
  return report_traps(rt);
}

uint64
sys_load_traps(void)
{
  return load_traps();
}

uint64
sys_create_thread(void) 
{
  uint *thread_id;
  void *(*function)(void *arg);
  void *arg;
  void *stack;
  uint64 stack_size;
  argaddr(0, (uint64 *)&thread_id);
  argaddr(1, (uint64 *)&function);
  argaddr(2, (uint64 *)&arg);
  argaddr(3, (uint64 *)&stack);
  argaddr(4, (uint64 *)&stack_size);

  return create_thread(thread_id, function, arg, stack, stack_size);
}
uint64
sys_join_thread(void) 
{
  uint thread_id;
  argaddr(0, (uint64 *)&thread_id);
  return join_thread(thread_id);
}

uint64
sys_cpu_usage(void)
{
  return cpu_usage();
}

uint64
sys_top(void)
{
  struct top *tp;
  argaddr(0, (uint64 *)&tp);
  return top(tp);
}

uint64
sys_set_cpu_quota(void)
{
  int pid, quota;
  argint(0, &pid);
  argint(1, &quota);
  return set_cpu_quota(pid, quota);
}

uint64
sys_hotfork(void)
{
  int deadline;
  argint(0, &deadline);
  return hotfork(deadline);
}
