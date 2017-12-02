#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <string.h>

#include <stdio.h>

/* Buffer cache. */
struct bce buffer_cache[BUFFER_CACHE_SIZE];

/* Lock for buffer cache. */
struct lock bc_lock;

static int alloc_cache_idx (void);
static int buffer_cache_find (disk_sector_t);
static int select_victim (void);
static void buffer_cache_evict (int);

void
buffer_cache_init (void)
{
  lock_init (&bc_lock);

  int i;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
    buffer_cache[i].empty = true;
}

void
buffer_cache_done (void)
{
  lock_acquire (&bc_lock);

  /* Writes the data in buffer cache to the disk. I.e., evict all. */
  int i;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
    if (buffer_cache[i].empty == false)
      buffer_cache_evict (i);

  lock_release (&bc_lock);
}

/* Reads sector SEC_NO into BUFFER. If cache hit, reads the buffer
   cache instead of the disk. If missed, cache SEC_NO. */
/* disk --> memory */
void
buffer_cache_disk_read (disk_sector_t sec_no, void *buffer)
{
  lock_acquire (&bc_lock);

  int idx = buffer_cache_find (sec_no);
  if (idx == -1)
    {
      /* Cache miss. Allocate a slot and cache current sector. */
      idx = alloc_cache_idx ();
//      ASSERT (buffer_cache[idx].empty == true);

      buffer_cache[idx].empty = false;
      buffer_cache[idx].sec_no = sec_no;
      buffer_cache[idx].dirty = false;

      /* Reads sector SEC_NO into the buffer cache. */
      /* disk --> buffer cache */
      disk_read (filesys_disk, sec_no, buffer_cache[idx].buffer);
    }

  /* Reads the buffer cache into the BUFFER. */
  /* buffer cache --> memory */
  memcpy (buffer, buffer_cache[idx].buffer, DISK_SECTOR_SIZE);

  buffer_cache[idx].accessed = true;

  lock_release (&bc_lock);
}

/* Writes sector SEC_NO from BUFFER and cache it. However, it does
   not perform disk_write() here actually. Instead cached sector
   will be written into the disk when it is evicted. */
/* memory --> buffer cache */
void
buffer_cache_disk_write (disk_sector_t sec_no, const void *buffer)
{
  lock_acquire (&bc_lock);

  int idx = buffer_cache_find (sec_no);
  if (idx == -1)
    {
      /* Cache miss. Allocate a slot and cache current sector. */
      idx = alloc_cache_idx ();
//      ASSERT (buffer_cache[idx].empty == true);

      buffer_cache[idx].empty = false;
      buffer_cache[idx].sec_no = sec_no;
    }

  /* Writes the buffer cache from BUFFER. */
  /* memory --> buffer cache */
  memcpy (buffer_cache[idx].buffer, buffer, DISK_SECTOR_SIZE);

  buffer_cache[idx].accessed = true;
  buffer_cache[idx].dirty = true;

  lock_release (&bc_lock);
}

/* ===== Helpers ===== */

/* Returns the index of empty buffer cache slot. */
static int
alloc_cache_idx (void)
{
  ASSERT (lock_held_by_current_thread (&bc_lock));

  int i;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
    if (buffer_cache[i].empty == true)
      return i;

  /* No empty slot; evict some. */
  int victim = select_victim ();
  buffer_cache_evict (victim);
//  ASSERT (buffer_cache[victim].empty == true);

  return victim;
}

/* Returns the index of buffer cache slot whose sec_no is SEC_NO. */
static int
buffer_cache_find (disk_sector_t sec_no)
{
  ASSERT (lock_held_by_current_thread (&bc_lock));

  int i;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
    if (buffer_cache[i].empty == false
        && buffer_cache[i].sec_no == sec_no)
      return i;

  /* Slot not found; return -1. */
  return -1;
}

static int
select_victim (void)
{
  ASSERT (lock_held_by_current_thread (&bc_lock));

  /* Use second chance algorithm. */
  int i = 0;
  while (true)
    {
      if (buffer_cache[i].accessed == false)
        return i;
      else
        buffer_cache[i].accessed = false;

      i++;
      i %= BUFFER_CACHE_SIZE;
    }
}

static void
buffer_cache_evict (int idx)
{
  ASSERT (lock_held_by_current_thread (&bc_lock));

  if (buffer_cache[idx].dirty == true)
    {
      disk_write (filesys_disk, buffer_cache[idx].sec_no,
                  buffer_cache[idx].buffer);
      buffer_cache[idx].dirty = false;
    }

  buffer_cache[idx].empty = true;
}
