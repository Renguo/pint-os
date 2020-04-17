+-------------------------+
|      CS 5600           |
| PROJECT 4: FILE SYSTEMS |
|     DESIGN DOCUMENT     |
+-------------------------+

---- GROUP ----

>> Fill in the names and email addresses of your group members.

Shufan Xing <xing.sh@husky.neu.edu>
Xiaoyu Zhang <zhang.xiaoyu1@husky.neu.edu>
Zhifan Hui <hui.z@husky.neu.edu>

---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.
>Pinto Guides:
  https://www.cs.usfca.edu/~benson/cs326/pintos/doc/pintos.pdf
  https://static1.squarespace.com/static/5b18aa0955b02c1de94e4412/t/5b85fad2f950b7b16b7a2ed6/1535507195196/Pintos+Guide
>GitHub
>Lecture Notes:
  https://oslab.kaist.ac.kr/wp-content/uploads/esos_files/courseware/undergraduate/OSTEP/40.File_system_Implementation.pdf?ckattempt=2
  http://bits.usc.edu/cs350/assignments/project4.pdf

INDEXED AND EXTENSIBLE FILES
============================

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.
>
>struct inode_disk
{
  block_sector_t start;               /* First sector */
  off_t length;                       /* File size in bytes. */
  unsigned short magic;               /* Magic number. */
  unsigned short is_dir;              /* Is file or directory. */
  block_sector_t sectors[NDIRECT_SECTORS + 1] /* Sectors. */;
};

>reimplemented on-disk inode struct
>
>struct inode 
{
  struct list_elem elem;              /* Element in inode list. */
  block_sector_t sector;              /* Sector number of disk location. */
  int open_cnt;                       /* Number of openers. */
  bool removed;                       /* True if deleted, false otherwise. */
  int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
  struct lock lock;                   /* Protect directory inodes. */
  struct inode_disk *data;            /* Inode data. */
};
> reimplemented In-momory inode

>> A2: What is the maximum size of a file supported by your inode
>> structure?  Show your work.
>
>8 MB
>we have 124 direct address, 1 indirect addredd and 1 double indirect address, so the maximum size should be
>124 * 512 + 1 * 128 * 512 + 1 * 128 * 128 * 512 = 8MB

/* Number of sector indices stored directly in the inode. */
#define NDIRECT_SECTORS   124
/* Number of indirect or doubly indirect indicies stored in a sector. */
#define NINDIRECT_SECTORS 128
#define NDIRECT_BYTES     (NDIRECT_SECTORS * BLOCK_SECTOR_SIZE)
/* The total number of bytes that can be stored in a single doubly indirect
   sector. */
#define NDINDIRECT_BYTES  (NINDIRECT_SECTORS * BLOCK_SECTOR_SIZE);

---- SYNCHRONIZATION ----

>> A3: Explain how your code avoids a race if two processes attempt to
>> extend a file at the same time.
>
> Directories are already locked. Files will be locked to prevent races when two processes attempted to extend a file at the same time. inode will use the `lock` in structure `inode`, which is used to pretect inodes and and cache the sector, the sector will be unique in cache when we extend the file. The lock only permit one process to access as the following codes in inode.c:
  if (!is_dir)
    lock_acquire (&inode->lock);

>> A4: Suppose processes A and B both have file F open, both
>> positioned at end-of-file.  If A reads and B writes F at the same
>> time, A may read all, part, or none of what B writes.  However, A
>> may not read data other than what B writes, e.g. if B writes
>> nonzero data, A is not allowed to see all zeros.  Explain how your
>> code avoids this race.
>
> inode_length returns the inode' data length. The inode_read_at() reads data, it checked the inode_length and is only allowed to read data within the length. The inode_length is updated in update_length() which is called in inode_write_at(). So process A updates the inode_length and process B reads data which won't access the data beyond the length. 

>> A5: Explain how your synchronization design provides "fairness".
>> File access is "fair" if readers cannot indefinitely block writers
>> or vice versa.  That is, many processes reading from a file cannot
>> prevent forever another process from writing the file, and many
>> processes writing to a file cannot prevent another process forever
>> from reading the file.
>
>Writers require the `lock` in `inode` to extend the file. But readers don't need any lock to read though they can only read the data within `inode_length`. So they won't block each other though one extension writer blocks another one.

---- RATIONALE ----

>> A6: Is your inode structure a multilevel index?  If so, why did you
>> choose this particular combination of direct, indirect, and doubly
>> indirect blocks?  If not, why did you choose an alternative inode
>> structure, and what advantages and disadvantages does your
>> structure have, compared to a multilevel index?
>
> Yes. We have a multilevel index with 124 direct, 1 indirect and 1 doubly indirect blocks. This method is dynamically fit for small and big files. It's also efficient to access data by use at most 3 index to find the block.

SUBDIRECTORIES
==============

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.
>
> we add
> struct thread {
    ...
#ifdef FILESYS
    struct inode *cwd; // current working directory
#endif
    ...
}

---- ALGORITHMS ----

>> B2: Describe your code for traversing a user-specified path.  How
>> do traversals of absolute and relative paths differ?
>
> The lookup() in path.c is used to parse the user-specified path and return the related inode. The basis process of traversing the path are:
1. Find the first character in path. If it's '/', get the inode of the root directory; otherwise, get the inode of current working directory in which the process is working on.
2. Use an iterative method to parse the following part of path and update the inode. Starting from current inode, use `dir_look()` to retrieve the inode of the next level of directory. Update the inode until the final segment separated by '/'.
3. Return the inode of the final segment in the path.

---- SYNCHRONIZATION ----

>> B4: How do you prevent races on directory entries?  For example,
>> only one of two simultaneous attempts to remove a single file
>> should succeed, as should only one of two simultaneous attempts to
>> create a file with the same name, and so on.
>
>We use a lock of the inode to prevent the races on directory entries. The `dir_add()`/`dir_remove()` call `inode_write_at()` to change directory which use the `lock` of `inode` to pretect inodes and and cache the sector.

>> B5: Does your implementation allow a directory to be removed if it
>> is open by a process or if it is in use as a process's current
>> working directory?  If so, what happens to that process's future
>> file system operations?  If not, how do you prevent it?
>
> No, it's not allowed. We use `inode->open_cnt` to check if it's opened by a process, and the number of it is how many times the inode is opened. The directory is removeable only when the number is equal to 0. Each process has its current working directory `struct inode cwd` in structure `thread`, and increase open_cnt when the thread is created and decrease it when it's released, so that the number won't be 0 when the process exists, and prevent removing current working directory of a thread.

---- RATIONALE ----

>> B6: Explain why you chose to represent the current directory of a
>> process the way you did.
>
> We store the current working directory `cwd` in `thread`, so that it's easy to know the directory used by the process. It also prevent the current working directory from being closed.

BUFFER CACHE
============

---- DATA STRUCTURES ----

>> C1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.
>
> struct buffer
{
  uint8_t flags;   // Status flags
  block_sector_t sector;  // Disk sector the buffer
  block_sector_t evicting_sector;  // The sector being evicted
  unsigned waiting;  // The number of processes waiting to be available
  struct condition available;  // Wait on available tf the cache is in use
  struct condition evicted;  //  Wait on evicting if the cache is to be evicted
  uint8_t data[BLOCK_SECTOR_SIZE];
  struct list_elem elem;
};
> It represents a cached block on disk to keep the data and status of cache block.

>#define CACHE_SIZE 64
struct cache caches[CACHE_SIZE];
> The cache array represents at most 64 sector can be cached.

---- ALGORITHMS ----

>> C2: Describe how your cache replacement algorithm chooses a cache
>> block to evict.
>
> All buffers are maintaied in a list of buffer `cache`. When a buffer is required in `buffer_acquire()`, it traverses the buffer list in `get_buffer_to_acquire()` which returns a buffer which already used for the sector or another buffer which is the least recently used(LRU) buffer. If the chosen cache block was not used for sector, it's then evicted.

>> C3: Describe your implementation of write-behind.
>
> When the buffer needs to be write to sector, it won't be writen immediately. Instead, it's marked as dirty. for every 100 ms, a backup tread is waked up to call `flush_all()` to flush dirty buffers back to disk.

>> C4: Describe your implementation of read-ahead.
>
> A read ahead queue `read_ahead_sectors` is maintained. When a file is read and requires read ahead, it calls `buffer_read_ahead()` to add the sector to the queue. A back thread calls `read_ahead()` to pop from the queue and get acquired buffer for the sector.

---- SYNCHRONIZATION ----

>> C5: When one process is actively reading or writing data in a
>> buffer cache block, how are other processes prevented from evicting
>> that block?
>
> When the buffer is actively reading or writing by `load_buffer()` or `flush_all()`, the process waits on a `cache_lock`, increase a counter `waiting` and sets the buffer `flags` to `BUF_IN_USE`. The flag can only be removed when the buffer is released by `buffer_release`. A process get a buffer in `load_buffer()` from from `get_buffer_to_acquire()` which returns a buffer without `BUF_IN_USE` and `waiting` is 0, and only return evicting buffer when the sector of evicting buffer is the same as the acquired sector. If the `load_buffer()` gets the buffer in eviction, it waits on the `cache_lock` and completes the block write and eviction; other wise, `flush_all()` complets all other eviction.

>> C6: During the eviction of a block from the cache, how are other
>> processes prevented from attempting to access the block?
>
> When a process attempts to access a block by `buffer_acquire()`, it gets a buffer from `get_buffer_to_write_back()` which only returns an evicting buffer when the evicting sector of the buffer is the same as the acquired sector by the process; otherwise, it returns a buffer which is not in use and no other process is waiting on it, which means .

---- RATIONALE ----

>> C7: Describe a file workload likely to benefit from buffer caching,
>> and workloads likely to benefit from read-ahead and write-behind.
>
> buffer caching which loads file from memory is much faster than from the disk.
> read-ahead has a better performance while reading large files which use continous sectors.
> write-behind reduces I/O frequency and has better performance while access same file frequently.
