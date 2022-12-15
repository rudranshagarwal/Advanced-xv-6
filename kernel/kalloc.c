// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

uint64 x = PGROUNDUP(PHYSTOP)>>12;

struct {
  struct spinlock lock;
  int count[PGROUNDUP(PHYSTOP)>>12];
} pagereference;

void init_pagereference(){
  int i = 0;
  initlock(&pagereference.lock, "pagereference");

  acquire(&pagereference.lock);

  while(i < x)
  {
   pagereference.count[i]=0;
    i++;
  }
  release(&pagereference.lock);

}


void pagereference_decrease(void*pa){
  int count_var = (uint64)pa>>12;

  acquire(&pagereference.lock);

  int total_count = pagereference.count[count_var];

  if(total_count <0){
    panic("increase page reference");
  }
  pagereference.count[count_var]--;
  release(&pagereference.lock);
}


void pagereference_increase(void*pa){
  int count_var = (uint64)pa>>12;

  acquire(&pagereference.lock);

  int total_count = pagereference.count[count_var];

  if(total_count <0){
    panic("increase page reference");
  }
  pagereference.count[count_var]++;
  release(&pagereference.lock);
}

int get_pagereference(void*pa)
{
  acquire(&pagereference.lock);

  uint64 count_var=(uint64)pa>>12;
  int rvalue = pagereference.count[count_var];

  if(rvalue<0){
    panic("get page reference");
  }
  release(&pagereference.lock);

  return rvalue;
}


int page_fault_handler(void*va,pagetable_t pagetable){
 
  pte_t *pte;
  uint64 pa;
  uint flags;
  struct proc* p = myproc();
  int var1=PGROUNDDOWN(p->trapframe->sp)-PGSIZE;

  if((uint64)va>=MAXVA || ((uint64)va>=var1 && (uint64)va<=(var1+PGSIZE)))
    return -2;

  va = (void*)PGROUNDDOWN((uint64)va);
  pte = walk(pagetable,(uint64)va,0);

  if(!pte)
    return -1;

  pa = PTE2PA(*pte);

  if(!pa)
    return -1;

  flags = PTE_FLAGS(*pte);
  if(flags&PTE_COW)
  {
    char*mem;
    
    flags = (flags|PTE_W);
    flags  = flags &(~PTE_COW);
    mem = kalloc();

    if(!mem)
      return -1;

    memmove(mem,(void*)pa,PGSIZE);
    *pte = PA2PTE(mem);
    *pte=(*pte) | flags;
    
    kfree((void*)pa);
    return 0;
  }

  return 0;
}


void
kinit()
{
  initlock(&kmem.lock, "kmem");
  init_pagereference();

  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {    pagereference_increase(p);

    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{

  struct run *r;

  int count_var = (uint64)pa>>12;


  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&pagereference.lock);

   int total_count = pagereference.count[count_var];
  if(total_count <=0){
    panic("decrease page reference");
  }

  pagereference.count[count_var]--;

  total_count--;
  if(total_count>0){
    release(&pagereference.lock);
    return;
  }
  release(&pagereference.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
  {
    memset((char*)r, 5, PGSIZE);
    pagereference_increase((void*)r);

  } // fill with junk
  return (void*)r;
}
