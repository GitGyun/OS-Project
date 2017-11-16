#include "vm/swap.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "devices/disk.h"
#include "filesys/file.h"
#include <hash.h>
#include <bitmap.h>
#include <stdio.h>
#include <string.h>

/* The number of sector in one page */
#define SEC_PG (PGSIZE / DISK_SECTOR_SIZE)

/* Swap disk. It can btained by using disk_get (1, 1). */
static struct disk *swap_disk;

/* Swap table. It manages which sectors are allocated to evicted
   pages. For example, if index 2 of swap table is true then some
   page can be swapped to sector 16 - 23 of the swap disk. (when
   page size is 8 times sector size) */
static struct bitmap *swap_table;

static size_t alloc_pg_idx (void);
static void to_swap_disk (struct spte *);
static void to_file (struct spte *p);
static void from_swap_disk (struct spte *);
static void from_file (struct spte *);

/* Create and initialize the swap table. */
void
swap_table_init (void)
{
  swap_disk = disk_get (1, 1);
	if (swap_disk == NULL)
    PANIC ("Can't load swap disk");

	swap_table = bitmap_create (disk_size (swap_disk) / SEC_PG);
	//printf ("swap_table size %d\n", disk_size (swap_disk) / SEC_PG);
	bitmap_set_all (swap_table, true);
}

/* Set IDX of the swap table to AVAILABLE, */
void
swap_table_set_available (size_t idx, bool available)
{
  //printf ("set available :: idx %zu\n", idx);
  bitmap_set (swap_table, idx, available);
}

/* Evict the frame VICTIM as proper file.
     FILE is NULL             : to swap disk
     read-only and not mapped : to file
     mapped                   : to memory-mapped file */
void
swap_out (struct fte *victim)
{
  pgl_acquire ();

  /* Find spte from upage of victim */
  struct hash *spt = victim->process->suppl_page_table;
	struct spte *p = suppl_page_table_find (spt, victim->upage);
	ASSERT (p != NULL);

	if (p->file == NULL)
    to_swap_disk (p);
  else if (p->writable == false && p->mapped == false)
    to_file (p);

  pgl_release ();
}

/* Reload the evicted frame to memory.
     FILE is NULL     : from swap disk
     FILE is not NULL : from file */
void
swap_in (struct spte *p)
{
  pgl_acquire ();

  //printf ("(%d) swap in!\n", thread_tid ());

  if (p->file == NULL)
    from_swap_disk (p);
  else
    from_file (p);

  pgl_release ();
}



/* ===== Helpers ===== */

/* Allocate number of disk sector which is empty */
static size_t
alloc_pg_idx (void)
{
  return bitmap_scan (swap_table, 0, 1, true);
}

static void
to_swap_disk (struct spte *p)
{
  ASSERT (p != NULL)

  /* Allocate new swap slot. */
  size_t pg_idx = alloc_pg_idx ();
	if (pg_idx == BITMAP_ERROR)
    PANIC ("Swap disk capacity insufficient");

	p->stat = PG_EVICTED;
	p->pg_idx = pg_idx;

	/* Swap out victim from memory to the swap disk. */
	size_t sec_no = pg_idx * SEC_PG;
	int i;
	for (i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++)
    disk_write (swap_disk, sec_no + i, (uint8_t *)p->kpage + DISK_SECTOR_SIZE*i);

  /* Mark that pg_idx is occupied (not available) in the swap disk. */
	swap_table_set_available (pg_idx, false);

	/* Discard kpage from the frame table */
	frame_free (p->kpage);
}

static void
to_file (struct spte *p)
{
  ASSERT (p != NULL);

  p->stat = PG_EVICTED;
  frame_free (p->kpage);
}

static void
from_swap_disk (struct spte *p)
{
  ASSERT (p != NULL);

  /* Allocate new frame to load evicted page from the swap disk to
     memory. */
  void *kpage = frame_alloc (p->upage, PAL_USER, true);

  /* Swap in the page from the swap disk to memory */
  size_t sec_no = p->pg_idx * SEC_PG;
  int i;
  for (i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++)
    disk_read (swap_disk, sec_no + i, (uint8_t *)kpage + DISK_SECTOR_SIZE*i);

  swap_table_set_available (p->pg_idx, true);
  p->stat = PG_ON_MEMORY;
}

static void
from_file (struct spte *p)
{
  void *kpage = frame_alloc (p->upage, PAL_USER, p->writable);

  file_seek (p->file, p->ofs);
  if (file_read (p->file, kpage, p->page_read_bytes)
      != (int)p->page_read_bytes)
    {
      frame_free (kpage);
      PANIC ("Cannot load page from file");
    }
  memset (kpage + p->page_read_bytes, 0, p->page_zero_bytes);

  /* If page is writable and not mapped, page must be evicted to
     and reloaded from swap disk from now on. */
  if (p->writable == true && p->mapped == false)
    p->file = NULL;

  p->stat = PG_ON_MEMORY;
}
