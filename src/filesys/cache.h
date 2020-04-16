#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <list.h>
#include <stdint.h>
#include "devices/block.h"
#include "threads/synch.h"

struct cache
{
  /* Status flags.  See cache.c. */
  uint8_t flags;
  /* The disk sector for the cache. */
  block_sector_t sector;
  /* The sector that's currently being evicted if this is not UINT_MAX. */
  block_sector_t evicting_sector;
  /* The number of processes waiting on the cache to become available. */
  unsigned waiting;
  /* If the cache is in use, wait on available. */
  struct condition available;
  /* If the cache is being evicted, wait on evicting. */
  struct condition evicted;
  uint8_t data[BLOCK_SECTOR_SIZE];
  struct list_elem elem;
};

void caches_init (void);
void caches_done (void);
struct cache *cache_acquire (block_sector_t sector, bool is_meta);
void cache_release (struct cache *cache, bool dirty);
void cache_read_ahead (block_sector_t sector, bool is_meta);

#endif /* filesys/cache.h */
