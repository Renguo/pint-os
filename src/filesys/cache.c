#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/cache.h"
#include "devices/timer.h"

/* Implements a cache cache.  caches are read in from the file system and 
   cached for subsequent use.  Dirty caches are written back to the file 
   system periodically by a background thread or when they are evicted to make
   space for a new cache.  Asynchronous read ahead is supported. */

/* cache flags. */
/* If set, the cache is currently in use by a process. */
#define BUF_IN_USE             0x01
/* If set, the cache contents do not match the disk contents. */
#define BUF_DIRTY              0x02
/* Marks a cache as meta data (inode) as opposed to just plain data.
   When searching for a cache to use, data caches are chosen over 
   meta data caches.  Inodes should remain in the cache
   for as long as possible for performance reasons. */
#define BUF_META               0x08

#define CACHE_SIZE             64
/* How often the background write back thread wakes up to write dirty
   caches to disk. */
#define WRITE_BACK_INTERVAL_MS 100

/* Read ahead sectors placed on the read ahead queue. */
struct read_ahead_sector
{
  /* The disk sector to read. */
  block_sector_t sector;
  bool is_meta;
};

static struct cache caches[CACHE_SIZE];
static struct lock cache_lock;
static struct list cache;
/* When signaled indicates that a cache is available. */
static struct condition cache_available;
/* Read ahead. */
static struct lock read_ahead_lock;
/* Read ahead queue. */
static struct read_ahead_sector read_ahead_sectors[CACHE_SIZE];
static int sectors_head;
static int sectors_tail;
static size_t sectors_size;
/* When signaled indicates a cache is available for reading ahead. */
static struct condition read_ahead_available;
/* Stops the read ahead thread. */
static bool stop_read_ahead;
/* Used to wait for the read ahead thread to exit. */
static struct semaphore read_ahead_done;

static int cache_accesses;
static int cache_hits;

static struct cache *get_cache_to_acquire (block_sector_t sector);
static struct cache *get_cache_to_write_back (void);
static void load_cache (block_sector_t sector, bool is_meta,
                         struct cache *cache);
static void flush_all (void);
static void read_ahead (void *aux UNUSED);
static void write_back (void *aux UNUSED);

void
caches_init (void)
{
  int i;

  list_init (&cache);
  lock_init (&cache_lock);
  cond_init (&cache_available);
  lock_init (&read_ahead_lock);
  cond_init (&read_ahead_available);
  sema_init (&read_ahead_done, 0);
  stop_read_ahead = false;
  for (i = 0; i < CACHE_SIZE; i++)
    {
      caches[i].sector = UINT_MAX;
      caches[i].evicting_sector = UINT_MAX;
      cond_init (&caches[i].available);
      cond_init (&caches[i].evicted);
      list_push_back (&cache, &caches[i].elem);
    }
  thread_create ("read_ahead", PRI_DEFAULT, read_ahead, NULL);
  thread_create ("write_back", PRI_DEFAULT, write_back, NULL);
}

void
caches_done (void)
{
  lock_acquire (&cache_lock);
  stop_read_ahead = true;
  cond_signal (&read_ahead_available, &cache_lock);
  lock_release (&cache_lock);
  sema_down (&read_ahead_done);
  flush_all ();
  printf ("cache accesses: %d, hits: %d\n", cache_accesses,
          cache_hits);
}

/* Acquires a cache for reading and writing.  If the cache is already
   cached, it's returned without any I/O.  If not, the contents of the 
   cache are read in before returning.  If the cache is currently in 
   use or being evicted, waits until it's available.  The returned cache 
   is locked so no other process can acquire it until cache_release is 
   called.  Acquired caches should be released as quickly as possible. */
struct cache *
cache_acquire (block_sector_t sector, bool is_meta)
{
  struct cache *cache;
  bool acquire = false;

  lock_acquire (&cache_lock);
  cache_accesses++;

  while (!acquire)
    {
      cache = get_cache_to_acquire (sector);
      if (cache == NULL)
        cond_wait (&cache_available, &cache_lock);
      else if (sector == cache->sector)
        {
          cache_hits++;
          cache->waiting++;
          while (cache->flags & BUF_IN_USE)
            cond_wait (&cache->available, &cache_lock);
          cache->waiting--;
          cache->flags |= BUF_IN_USE;
          lock_release (&cache_lock);
          acquire = true;
        }
      else if (sector == cache->evicting_sector)
        {
          ASSERT (cache->flags & BUF_IN_USE);
          
          while (sector == cache->evicting_sector)
            cond_wait (&cache->evicted, &cache_lock);
        }
      else
        {
          load_cache (sector, is_meta, cache);
          acquire = true;
        }
    }
  return cache;
}

/* Releases a cache and marks it as dirty if it's been written to.  The
 * cache is available for reuse if there are no processes waiting on it. */
void
cache_release (struct cache *cache, bool dirty)
{
  ASSERT (cache->flags & BUF_IN_USE);
  
  lock_acquire (&cache_lock);
  cache->flags &= ~BUF_IN_USE;
  if (dirty)
    cache->flags |= BUF_DIRTY;
  if (cache->waiting > 0)
      cond_signal (&cache->available, &cache_lock);
  else
    {
      list_remove (&cache->elem);
      list_push_back (&cache, &cache->elem);
      cond_signal (&cache_available, &cache_lock);      
    }
  lock_release (&cache_lock);
}

/* Adds a sector to the read ahead queue.  If the queue is full, does
   nothing. */
void
cache_read_ahead (block_sector_t sector, bool is_meta)
{
  struct read_ahead_sector ra_sector;
  
  lock_acquire (&read_ahead_lock);
  if (sectors_size < CACHE_SIZE)
    {
      ra_sector.sector = sector;
      ra_sector.is_meta = is_meta;
      read_ahead_sectors[sectors_head++ % CACHE_SIZE] = ra_sector;
      sectors_size++;
      cond_signal (&read_ahead_available, &read_ahead_lock);
    }
  lock_release (&read_ahead_lock);
}

/* Loads a sector into a cache.  If a sector is already present it's 
   evicted. */
static void
load_cache (block_sector_t sector, bool is_meta, struct cache *cache)
{
  ASSERT (!(cache->flags & BUF_IN_USE));
  
  cache->flags |= BUF_IN_USE;
  if (is_meta)
    cache->flags |= BUF_META;
  else
    cache->flags &= ~BUF_META;
  if (cache->flags & BUF_DIRTY)
    {
      cache->evicting_sector = cache->sector;
      cache->sector = sector;
      lock_release (&cache_lock);
      block_write (fs_device, cache->evicting_sector, cache->data);
      lock_acquire (&cache_lock);
      cache->evicting_sector = UINT_MAX;
      cache->flags &= ~BUF_DIRTY;
      cond_signal (&cache->evicted, &cache_lock);
    }
  else
    cache->sector = sector;
  lock_release (&cache_lock);
  ASSERT (cache->evicting_sector == UINT_MAX);
  block_read (fs_device, cache->sector, cache->data);
}

/* Writes dirty caches until no more are available. Depending on the number
   of dirty caches, this call can take some time to finish.  For that reason
   it should be done on a background thread or called when shutting down. */
static
void flush_all (void)
{
  struct cache *cache;

  lock_acquire (&cache_lock);
  cache = get_cache_to_write_back ();
  while (cache != NULL)
    {
      cache->waiting++;
      while (cache->flags & BUF_IN_USE)
        cond_wait (&cache->available, &cache_lock);
      cache->waiting--;
      cache->flags |= BUF_IN_USE;
      lock_release (&cache_lock);
      block_write (fs_device, cache->sector, cache->data);
      cache->flags &= ~BUF_DIRTY;
      cache_release (cache, false);
      lock_acquire (&cache_lock);
      cache = get_cache_to_write_back ();
    }
  lock_release (&cache_lock);
}

/* Reads in read ahead buffes until the read ahead queue is empty or it's 
   told to stop. */
static void
read_ahead (void *aux UNUSED)
{
  struct read_ahead_sector ra_sector;
  struct cache *cache;
  
  while (true)
    {
      lock_acquire (&read_ahead_lock);
      while (sectors_size == 0 && !stop_read_ahead)
        cond_wait (&read_ahead_available, &read_ahead_lock);      
      if (stop_read_ahead)
        break;
      ra_sector = read_ahead_sectors[sectors_tail++ % CACHE_SIZE];
      sectors_size--;
      lock_release (&read_ahead_lock);
      lock_acquire (&cache_lock);
      cache = get_cache_to_acquire (ra_sector.sector);
      if (cache != NULL && ra_sector.sector != cache->sector
          && ra_sector.sector != cache->evicting_sector)
        {
          load_cache (ra_sector.sector, ra_sector.is_meta, cache);
          cache_release (cache, false);
        }
      else
        lock_release (&cache_lock);
    }
    sema_up (&read_ahead_done);
    thread_exit ();
}

static void
write_back (void *aux UNUSED)
{
  while (true)
    {
      flush_all ();
      timer_msleep (WRITE_BACK_INTERVAL_MS);
    }
}

/* Looks for a cache in the cache.  If a cache is already in the cache returns
   it immediately.  If not, the least recently used unused cache is returned 
   with data caches taking precedence over meta data caches.  If no suitable 
   cache can be found, returns NULL. */
static struct cache *
get_cache_to_acquire (block_sector_t sector)
{
  struct cache *cache;
  struct cache *meta_cache = NULL;
  struct cache *data_cache = NULL;
  struct list_elem *e;

  for (e = list_begin (&cache); e != list_end (&cache); e = list_next (e))
    {
      cache = list_entry (e, struct cache, elem);
      if (sector == cache->sector || sector == cache->evicting_sector)
        return cache;
      else if (cache->waiting == 0 && !(cache->flags & BUF_IN_USE))
        {
          if (cache->flags & BUF_META)
            {
              if (meta_cache == NULL)
                meta_cache = cache;
            }
          else if (data_cache == NULL)
            data_cache = cache;
        }
    }
  return data_cache != NULL ? data_cache : meta_cache;
}

/* Looks for a dirty cache in the cache.  If no suitable cache can be found
   returns NULL. */
static struct cache *
get_cache_to_write_back (void)
{
  struct cache *cache;
  struct list_elem *e;

  for (e = list_begin (&cache); e != list_end (&cache); e = list_next (e))
    {
      cache = list_entry (e, struct cache, elem);
      if ((cache->flags & BUF_DIRTY) && cache->evicting_sector == UINT_MAX)
        return cache;
    }
  return NULL;
}
