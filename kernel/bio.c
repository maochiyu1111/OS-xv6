// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define NBUCK 13
#define BUCK_HASH(blockno) (blockno % NBUCK)

struct bcacheHashBucket{
  struct spinlock lock;  
  struct buf head; 
};

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct bcacheHashBucket Buckets[NBUCK];
} bcache;

void
binit(void)
{
  struct buf *b;

  //初始化全局大锁
  initlock(&bcache.lock, "bcache");

  for (int i = 0; i < NBUCK; i++) {
    //初始化桶的锁
    initlock(&bcache.Buckets[i].lock, "bcache_buckets");
    //初始化各个桶的链表
    bcache.Buckets[i].head.prev = &bcache.Buckets[i].head;
    bcache.Buckets[i].head.next = &bcache.Buckets[i].head;
  }
  
  //初始化buffer，开始散列
  // Create linked list of buffers
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    int index = BUCK_HASH(b->blockno);
    b->next = bcache.Buckets[index].head.next;
    b->prev = &bcache.Buckets[index].head;
    initsleeplock(&b->lock, "buffer");
    bcache.Buckets[index].head.next->prev = b;
    bcache.Buckets[index].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int index = BUCK_HASH(blockno);
  acquire(&bcache.Buckets[index].lock);

  // Is the block already cached?
  for(b = bcache.Buckets[index].head.next; b != &bcache.Buckets[index].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.Buckets[index].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.Buckets[index].head.prev; b != &bcache.Buckets[index].head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.Buckets[index].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 还没找到，从别的桶里面去找
  // 先把原来的锁释放
  release(&bcache.Buckets[index].lock);

  //然后进入替换/分配流程
  acquire(&bcache.lock);
  acquire(&bcache.Buckets[index].lock);
  for(int i = 0; i<NBUCK; i++) {
    if (i == index)
      continue;
    //找第i个桶的
    acquire(&bcache.Buckets[i].lock);
    for(b = bcache.Buckets[i].head.prev; b != &bcache.Buckets[i].head; b = b->prev){
      //在第i个桶中找到
      if(b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        //善后
        b->next->prev = b->prev;
        b->prev->next = b->next;

        //头插法插到index的桶里面
        b->next = bcache.Buckets[index].head.next;
        b->prev = &bcache.Buckets[index].head;
        bcache.Buckets[index].head.next->prev = b;
        bcache.Buckets[index].head.next = b;

        release(&bcache.Buckets[i].lock);
        release(&bcache.Buckets[index].lock);
        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    //在第i个桶中没找到，释放i的锁
    release(&bcache.Buckets[i].lock);
  }
  //在所有的桶中都没找到，panic
  release(&bcache.Buckets[index].lock);
  release(&bcache.lock);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    //将磁盘块加载到block cache
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int index = BUCK_HASH(b->blockno);
  acquire(&bcache.Buckets[index].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.Buckets[index].head.next;
    b->prev = &bcache.Buckets[index].head;
    bcache.Buckets[index].head.next->prev = b;
    bcache.Buckets[index].head.next = b;
  }
  release(&bcache.Buckets[index].lock);
}

void
bpin(struct buf *b) {
  int index = BUCK_HASH(b->blockno);
  acquire(&bcache.Buckets[index].lock);
  b->refcnt++;
  release(&bcache.Buckets[index].lock);
}

void
bunpin(struct buf *b) {
  int index = BUCK_HASH(b->blockno);
  acquire(&bcache.Buckets[index].lock);
  b->refcnt--;
  release(&bcache.Buckets[index].lock);
}


