#include "vm/swap.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "devices/disk.h"
#include <string.h>
#include <hash.h>
#include <bitmap.h>
#include <stdio.h>

/* The number of sector required for one page */
#define SEC_PER_PG (PGSIZE / DISK_SECTOR_SIZE)

/* Swap disk. It can btained by using disk_get (1, 1). */
static struct disk *swap_disk;

/* Swap table. It manages which sectors are allocated to evicted
   pages. For example, if index 2 of swap table is true then some
   page can be swapped to sector 16 - 23 of the swap disk. (when
   page size is 8 times sector size) */
static struct bitmap *swap_table;

static size_t alloc_pg_idx (void);

/* Create and initialize the swap table. */
void
swap_table_init (void)
{
  swap_disk = disk_get (1, 1);
	if (swap_disk == NULL)
    PANIC ("Can't load swap disk");

	swap_table = bitmap_create (disk_size (swap_disk) / SEC_PER_PG);
	bitmap_set_all (swap_table, true);
}

/* Set IDX of the swap table to AVAILABLE, */
void
swap_table_set_available (size_t idx, bool available)
{
  bitmap_set (swap_table, idx, available);
}

void
swap_out (struct fte *victim)
{
  ASSERT (victim != NULL);

  pgl_acquire ();

  /* Allocate new swap slot. */
  size_t pg_idx = alloc_pg_idx ();
	if (pg_idx == BITMAP_ERROR)
    PANIC ("Swap disk capacity insufficient");

  /* Find spte from upage of victim */
	struct spte *p
      = suppl_page_table_find (victim->process->suppl_page_table,
                               victim->upage);
	if (p != NULL)
    {
      p->stat = PG_EVICTED;
      p->src = PG_SWAP;
      p->pg_idx = pg_idx;
    }
  else
    PANIC ("Cannot find the victim from the supplemental page table");

	/* Swap out victim from memory to the swap disk. */
	size_t sec_no = pg_idx * SEC_PER_PG;
	int i;
	for (i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++)
    disk_write (swap_disk, sec_no + i,
                (uint8_t *)victim->kpage + DISK_SECTOR_SIZE*i);

  /* Mark that pg_idx is occupied (not available) in the swap disk. */
	swap_table_set_available (pg_idx, false);

	/* Discard victim's kpage from the frame table */
	frame_free (victim->kpage);

  pgl_release ();
}

void
swap_in (struct spte *p)
{
  ASSERT (p != NULL);

  pgl_acquire ();

  /* Allocate new frame to load evicted page from the swap disk to
     memory. */
  void *kpage = frame_alloc (p->upage, PAL_USER, true);

  /* Swap in the page from the swap disk to memory */
  size_t sec_no = p->pg_idx * SEC_PER_PG;
  int i;
  for (i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++)
    disk_read (swap_disk, sec_no + i,
               (uint8_t *)kpage + DISK_SECTOR_SIZE*i);

  swap_table_set_available (p->pg_idx, true);
  p->stat = PG_ON_MEMORY;

  pgl_release ();
}

void
evict_file (struct fte *victim)
{
  ASSERT (victim != NULL);

  pgl_acquire ();

  struct spte *p
      = suppl_page_table_find (victim->process->suppl_page_table,
                               victim->upage);
	if (p != NULL)
    p->stat = PG_EVICTED;
  else
    PANIC ("Cannot find the victim from the supplemental page table");

  frame_free (victim->kpage);

  pgl_release ();
}

void
lazy_load (struct spte *p)
{
  ASSERT (p != NULL);

  pgl_acquire ();

  void *kpage = frame_alloc (p->upage, PAL_USER | PAL_ZERO, p->writable);
  if (kpage == NULL)
    goto load_error;

  ASSERT (p->page_read_bytes + p->page_zero_bytes == PGSIZE);

  file_seek (p->file, p->ofs);
  if (file_read (p->file, kpage, p->page_read_bytes)
      != (int) p->page_read_bytes)
    {
      frame_free (kpage);
      goto load_error;
    }
  memset (kpage + p->page_read_bytes, 0, p->page_zero_bytes);

  p->stat = PG_ON_MEMORY;

  pgl_release ();

  return;

 load_error:
  PANIC ("Cannot load the lazily loaded page");
}



/* ===== Helpers ===== */

/* Allocate number of disk sector which is empty */
static size_t
alloc_pg_idx (void)
{
  return bitmap_scan (swap_table, 0, 1, true);
}
