+-------------------------+
|      CS 5600           |
| PROJECT 4: FILE SYSTEMS |
|     DESIGN DOCUMENT     |
+-------------------------+

---- GROUP ----

>> Fill in the names and email addresses of your group members.

Tianyu Liu <liu.tianyu@husky.neu.edu>
Yang sun <sun.yang@husky.neu.edu>
Zhilong Zheng <zheng.zhil@husky.neu.edu>

---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.
> GitHub

INDEXED AND EXTENSIBLE FILES
============================

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.
>struct inode_disk
 {
   block_sector_t sectors[SECTOR_CNT]; /* Sectors. */
   enum inode_type type;               /* file or dir */
   off_t length;                       /* File size in bytes. */
   unsigned magic;                     /* Magic number. */
 }
>reimplemented the inode struct with sectors
>
>struct inode
 {
   struct list_elem elem;              /* Element in inode list. */
   block_sector_t sector;              /* Sector number of disk location. */
   int open_cnt;                       /* Number of openers. */
   bool removed;                       /* True if deleted, false otherwise. */
   int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
   struct lock lock;                   /* Protects the inode. */
   struct lock deny_write_lock;        /* Protects members below. */
   struct condition no_writers_cond;   /* Signaled when no writers. */
   int writer_cnt;                     /* Number of writers. */
 }

>static struct lock free_map_lock;
>lock for freemap

>> A2: What is the maximum size of a file supported by your inode
>> structure?  Show your work.
>
>8 MB
>we have 123 direct address, 1 indirect addredd and 1 double indirect address, so the maximum size should be
>123 * 512 + 1 * 128 * 512 + 1 * 128 * 128 * 512 = 8MB
>#define INODE_SPAN ((DIRECT_CNT 
                      + PTRS_PER_SECTOR * INDIRECT_CNT                        \
                      + PTRS_PER_SECTOR * PTRS_PER_SECTOR * DOUBLE_INDIRECT_CNT) \
                     * BLOCK_SECTOR_SIZE)

---- SYNCHRONIZATION ----

>> A3: Explain how your code avoids a race if two processes attempt to
>> extend a file at the same time.
>
>If two processes attempt to extend a same file, inode will call cache_lock, which cache the sector from disk,
>the sector shouble be unique in cache and every time we extend the file, the lock type is EXCLUSIVE which
>only permit one lock at a time

>> A4: Suppose processes A and B both have file F open, both
>> positioned at end-of-file.  If A reads and B writes F at the same
>> time, A may read all, part, or none of what B writes.  However, A
>> may not read data other than what B writes, e.g. if B writes
>> nonzero data, A is not allowed to see all zeros.  Explain how your
>> code avoids this race.
>
> Both read and write file are on cache block, so they all need call cache_read(), and this method has a
> data lock which means only one of them can do it at a time. If there is a writter write something to file,
> this cache block would mark as dirty and not up to date, and when reader read, it will check if the data
> is up to date, if not, it will read from disk again.

>> A5: Explain how your synchronization design provides "fairness".
>> File access is "fair" if readers cannot indefinitely block writers
>> or vice versa.  That is, many processes reading from a file cannot
>> prevent forever another process from writing the file, and many
>> processes writing to a file cannot prevent another process forever
>> from reading the file.
>
> We allowed multiple read and only one write. Lock happen when called cache_lock(), and after we cache
> the sector from disk, we may call cache_unlock() and it will broadcast other thread who waiting for 
> the lock.

---- RATIONALE ----

>> A6: Is your inode structure a multilevel index?  If so, why did you
>> choose this particular combination of direct, indirect, and doubly
>> indirect blocks?  If not, why did you choose an alternative inode
>> structure, and what advantages and disadvantages does your
>> structure have, compared to a multilevel index?
>
> Yes. We pick 123 direct, 1 indirect and 1 doubly indirect blocks and there are 2 reason:
> 1. 123 direct, 1 indirect and 1 doubly indirect blocks in total have 8MB size blocks
> 2. In this case, we total have 125 address as 500 bytes, add on inode_type, length and magic number, total
> is 512 bytes as the size of a sector.

SUBDIRECTORIES
==============

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.
>
> we add
> struct dir *wd;
> in Thread struct

---- ALGORITHMS ----

>> B2: Describe your code for traversing a user-specified path.  How
>> do traversals of absolute and relative paths differ?
>
> The name_to_entry() in filesys.c, we check the each part in the user-specified path, 
> As long as another part follows the current one, traverse down another directory until find the file 
> type in inode
> If path start from '/', it lookup from root directory, else return the working directory of current thread

---- SYNCHRONIZATION ----

>> B4: How do you prevent races on directory entries?  For example,
>> only one of two simultaneous attempts to remove a single file
>> should succeed, as should only one of two simultaneous attempts to
>> create a file with the same name, and so on.
>
> Every time we create or remove file in a directory, we will lock the directory inode. And every time 
> create a file in a directory, we will check if the name is already exist.

>> B5: Does your implementation allow a directory to be removed if it
>> is open by a process or if it is in use as a process's current
>> working directory?  If so, what happens to that process's future
>> file system operations?  If not, how do you prevent it?
> 
> No. We first check the onen_cnt of inode, if it more than 1 means it is in use and the process will 
> release lock and close inode.

---- RATIONALE ----

>> B6: Explain why you chose to represent the current directory of a
>> process the way you did.
> we use a pointer point to current working directory because it belongs to each thread.

BUFFER CACHE
============

---- DATA STRUCTURES ----

>> C1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.
>
> struct cache_block
  {
    struct lock block_lock;
    int readers;       
    int read_waiters;        
    int writers;
    int write_waiters;
    struct condition no_readers_or_writers;
    struct condition no_writers; 
    block_sector_t sector;
    uint8_t data[BLOCK_SECTOR_SIZE];  
    bool up_to_date;
    bool dirty;
    struct lock data_lock;            
  };
> cache_block represents a cached block from disk, store the data and status of sector.
>
> struct cache_block cache[CACHE_CNT];
> a cache_block array which represent we can at most have 64 sector cached.

---- ALGORITHMS ----

>> C2: Describe how your cache replacement algorithm chooses a cache
>> block to evict.
>
> When there need a new cache_block for a sector, it will go over all cache array.
> If there is a cache_block that the readers num, the writers num, the read_waiters num and the 
> write_waiters num are all 0, that means this is a free cache which we can evict. 

>> C3: Describe your implementation of write-behind.
>
> Every time we write to cache, we mark it as dirty and we open a thread behind which flush dirty cache 
> back to disk every 30 seconds.

>> C4: Describe your implementation of read-ahead.
> We open a thread behind which cache the sector in the readahead list from disk. 
> Every time a reader read a file and call the readahead_submit, it will add a sector into the list, then
> the thread will cache it.

---- SYNCHRONIZATION ----

>> C5: When one process is actively reading or writing data in a
>> buffer cache block, how are other processes prevented from evicting
>> that block?
>
> We have counters in cache struct, as reader count, writer count, reader waiter count and writer
> waiters count, only when those counts are 0 the process can evict cache.

>> C6: During the eviction of a block from the cache, how are other
>> processes prevented from attempting to access the block?
>
> When do the cache_free, it required to have the lock of the cache_block and this lock prevent other
> processes to access the block

---- RATIONALE ----

>> C7: Describe a file workload likely to benefit from buffer caching,
>> and workloads likely to benefit from read-ahead and write-behind.
> read-ahead perform better when read a large file.
> write-behind perform better when we need to access a same file frequently.
> buffer caching make access to file faster than from the disk.
