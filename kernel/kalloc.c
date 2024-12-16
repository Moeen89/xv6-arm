// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
char* kmem_names[] = {"kmem_core0","kmem_core1","kmem_core2","kmem_core3","kmem_core4","kmem_core5","kmem_core6","kmem_core7",};

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem_cpus[NCPU];



void
kinit()
{
  for (int i=0; i<NCPU; i++){
  	initlock(&kmem_cpus[i].lock, kmem_names[i]);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  int cpu_i = cpuid();
  push_off(); 
  acquire(&kmem_cpus[cpu_i].lock);
  r->next = kmem_cpus[cpu_i].freelist;
  kmem_cpus[cpu_i].freelist = r;
  release(&kmem_cpus[cpu_i].lock);
  pop_off(); 
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run* r;
  int cpu_i = cpuid();
  push_off();
  acquire(&kmem_cpus[cpu_i].lock);
  if (!kmem_cpus[cpu_i].freelist){ 
    int steal = 64;
    for (int i=0;i<NCPU;i++){
      if (i!=cpu_i) {
      acquire(&kmem_cpus[i].lock);
      struct run* r2 = kmem_cpus[i].freelist;
      while (steal && r2  ){
	      kmem_cpus[i].freelist = r2->next;
	      r2->next = kmem_cpus[cpu_i].freelist;
	      kmem_cpus[cpu_i].freelist = r2;
	      r2 = kmem_cpus[i].freelist;
	      steal --;
      }
      release(&kmem_cpus[i].lock);
      if (steal==0) break;	
      }
    }
  }

  r = kmem_cpus[cpu_i].freelist;
  if(r)
    kmem_cpus[cpu_i].freelist = r->next;

  release(&kmem_cpus[cpu_i].lock);
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
