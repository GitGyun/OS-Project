#include "vm/swap.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "devices/disk.h"
#include <hash.h>
#include <bitmap.h>
#include <stdio.h>

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

	swap_table = bitmap_create (disk_size (swap_disk) * DISK_SECTOR_SIZE / PGSIZE);
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
  pgl_acquire ();

  /* Allocate new swap slot. */
  size_t pg_idx = alloc_pg_idx ();
	if (pg_idx == BITMAP_ERROR)
    PANIC ("Swap disk capacity insufficient");

  /* Find spte from upage of victim */
	struct spte *p = suppl_page_table_find (victim->process->suppl_page_table, victim->upage);
	p->stat = PG_EVICTED;
	p->pg_idx = pg_idx;

	/* Swap out victim from memory to the swap disk. */
	size_t sec_no = pg_idx * (PGSIZE / DISK_SECTOR_SIZE);
	int i;
	for (i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++)
    disk_write (swap_disk, sec_no + i, (uint8_t *)victim->kpage + DISK_SECTOR_SIZE*i);

  /* Mark that pg_idx is occupied (not available) in the swap disk. */
	swap_table_set_available (pg_idx, false);

	/* Discard victim's kpage from the frame table */
	//frame_free (victim->kpage);
  
  /* Discard upage - kpage mapping from pagedir */
  pagedir_clear_page (victim->process->pagedir, victim->upage);

  /* Free KPAGE */
  palloc_free_page (victim->kpage);

  pgl_release ();
}

void
swap_in (struct spte *p)
{
  pgl_acquire ();

  /* Allocate new frame to load evicted page from the swap disk to
     memory. */
  void *kpage = frame_alloc (p->upage, PAL_USER, true);

  /* Swap in the page from the swap disk to memory */
  size_t sec_no = p->pg_idx * (PGSIZE / DISK_SECTOR_SIZE);
  int i;
  for (i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++)
    disk_read (swap_disk, sec_no + i, (uint8_t *)kpage + DISK_SECTOR_SIZE*i);

  swap_table_set_available (p->pg_idx, true);
  p->stat = PG_ON_MEMORY;

  pgl_release ();
}



/* ===== Helpers ===== */

/* Allocate number of disk sector which is empty */
static size_t
alloc_pg_idx (void)
{
  return bitmap_scan (swap_table, 0, 1, true);
}
