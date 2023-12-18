// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// 引用计数数组
#define REF_NUM(pa) (((uint64)pa-KERNBASE)/PGSIZE)
#define REF_MAX REF_NUM(PHYSTOP)

// 每个物理页的引用计数数组
int refcount[REF_MAX];
// 引用计数数组的锁
struct spinlock reflock;

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

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&reflock, "reflock");
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

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&reflock);
  if(--refcount[REF_NUM(pa)] <= 0) {
    //释放页面
    // Fill with junk to catch dangling refs.
    // printf("count：%d", refcount[REF_NUM(pa)]);
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&reflock);
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

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk

    // acquire(&reflock);
    refcount[REF_NUM(r)] = 1;
    // release(&reflock);
  }
    
  return (void*)r;
}

// fork时引用计数++
void
forkrefcount(void *pa)
{
  acquire(&reflock);
  refcount[REF_NUM(pa)]++;
  release(&reflock);  
}

// cow时，原页面-1，新页面置1
void*
cowrefcount(void *pa)
{
  acquire(&reflock);
  // 当子进程已经cow后，父进程并不需要再操作一次，应仍保持之前的引用
  // 若有多次引用，则当前涉及到的进程全部创建新页，消除cow，其余不受影响
  if(refcount[REF_NUM(pa)] <= 1) {
    release(&reflock);
    return pa;
  }

  // 分配新的内存页，并复制旧页中的数据到新页
  uint64 newpa = (uint64)kalloc();
  // out of memory
  if(newpa == 0) {
    release(&reflock);
    return 0; 
  }
  memmove((void*)newpa, (void*)pa, PGSIZE);

  refcount[REF_NUM(pa)]--;
  release(&reflock);  
  return (void*)newpa;
}