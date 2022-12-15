#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"


void restore(){

  struct proc*p=myproc();
  p->alarm_trapfr_cpy->kernel_trap = p->trapframe->kernel_trap;
  p->alarm_trapfr_cpy->kernel_satp = p->trapframe->kernel_satp;

  p->alarm_trapfr_cpy->kernel_hartid = p->trapframe->kernel_hartid;
  p->alarm_trapfr_cpy->kernel_sp = p->trapframe->kernel_sp;
  
  *(p->trapframe) = *(p->alarm_trapfr_cpy);
}

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

//new

uint64
sys_trace(void)
{
  argint(0, &myproc()->tracemask);

  if (myproc()->tracemask<0)
  return -1;

  return 0;
}

uint64
sys_sigalarm(void){

  uint64 hndl;

  argaddr(1, &hndl);

  if (hndl<0)
  return -1;

  int ti;
  argint(0, &ti);

  if (ti<0)
  return -1;

  myproc()->handler=hndl;
  myproc()->is_on=0;
  myproc()->curr_ticks=0;
  myproc()->alarm_ticks=ti;

  return 0;
}

uint64
sys_sigreturn(void){
  restore();

  myproc()->is_on = 0;

  int r_val=myproc()->trapframe->a0;

  return r_val;
}

uint64
sys_setpriority(void)
{
  #ifdef PBS
    int priority, pid;
    argint(0, &priority);
    argint(1, &pid);
    if(priority < 0 || priority > 100)
      return -1;
    return setpriority(priority, pid);
  #endif

  return -1;
    
}

uint64
sys_settickets(void)
{
  #ifdef LBS
    int numbertickets;
    argint(0, &numbertickets);
    if(numbertickets < 1)
      return -1;
    return settickets(numbertickets);
  #endif
  return -1;
}

uint64
sys_waitx(void)
{
  uint64 a, a1, a2;
  uint wtime, rtime;

  argaddr(0, &a);
  argaddr(1, &a1); // user vm
  argaddr(2, &a2);

  struct proc* p = myproc();
  int ret = waitx(a, &wtime, &rtime);

  if (copyout(p->pagetable, a2,(char*)&rtime, sizeof(int)) < 0)
    return -1;
  if (copyout(p->pagetable, a1,(char*)&wtime, sizeof(int)) < 0)
    return -1;

  return ret;
}