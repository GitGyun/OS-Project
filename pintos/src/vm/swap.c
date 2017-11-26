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

extern struct lock paging_lock;

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

static bool debug = false;

/* Create and initialize the swap table. */
void
swap_table_init (void)
{
  swap_disk = disk_get (1, 1);
	if (swap_disk == NULL)
    PANIC ("Can't load swap disk");

	swap_table = bitmap_create (disk_size (swap_disk) / SEC_PG);
	bitmap_set_all (swap_table, true);
}

/* Set IDX of the swap table to AVAILABLE, */
void
swap_table_set_available (size_t idx, bool available)
{
  sema_down (&paging_lock);
  bitmap_set (swap_table, idx, available);
  sema_up (&paging_lock);
}

/* Evict the frame VICTIM as proper file.
     FILE is NULL             : to swap disk
     read-only and not mapped : to file
     mapped                   : to memory-mapped file */
void
swap_out (struct fte *victim)
{
  ASSERT (victim != NULL);

  if (debug)
    printf ("(%2d) swap out: upage %p, kpage %p\n",
            thread_tid (), victim->upage, victim->kpage);

  /* Find spte from upage of victim */
  struct hash *spt = victim->process->suppl_page_table;
	struct spte *p = suppl_page_table_find (spt, victim->upage);
	ASSERT (p != NULL);
	//printf ("upage %p, kpage %p\n", p->upage, p->kpage);
	//ASSERT (p->kpage == victim->kpage && p->upage == victim->upage);

	if (p->file == NULL)
    to_swap_disk (p);
  else if (p->writable == false && p->mapped == false)
    to_file (p);
  else
    {
      if (debug)
        print_spte (p);
      PANIC ("Cannot swap out");
    }
}

/* Reload the evicted frame to memory.
     FILE is NULL     : from swap disk
     FILE is not NULL : from file */
void
swap_in (struct spte *p)
{
  ASSERT (p != NULL);

  

  if (debug)
    printf ("(%2d) swap in\n", thread_tid ());

  if (p->file == NULL)
    from_swap_disk (p);
  else
    from_file (p);
}



/* ===== Helpers ===== */

/* Allocate number of disk sector which is empty */
static size_t
alloc_pg_idx (void)
{
  sema_down (&paging_lock);
  size_t idx = bitmap_scan (swap_table, 0, 1, true);
  sema_up (&paging_lock);
  return idx;
}

static void
to_swap_disk (struct spte *p)
{
  ASSERT (p != NULL)

  if (debug)
    printf ("(%2d) swap to swap disk: upage %p, kpage %p\n",
            thread_tid (), p->upage, p->kpage);

  /* Allocate new swap slot. */
  size_t pg_idx = alloc_pg_idx ();
	if (pg_idx == BITMAP_ERROR)
    PANIC ("Swap disk capacity insufficient");

	/* Swap out victim from memory to the swap disk. */
	size_t sec_no = pg_idx * SEC_PG;
	int i;
	for (i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++)
    {
      if (debug)
        printf ("%p\n", (uint8_t *)p->kpage + DISK_SECTOR_SIZE*i);
      disk_write (swap_disk, sec_no + i, (uint8_t *)p->kpage + DISK_SECTOR_SIZE*i);
    }

  /* Mark that pg_idx is occupied (not available) in the swap disk. */
	swap_table_set_available (pg_idx, false);

	/* Discard kpage from the frame table */
	frame_free (p->kpage);

	p->stat = PG_EVICTED;
	p->pg_idx = pg_idx;
}

static void
to_file (struct spte *p)
{
  ASSERT (p != NULL);

  frame_free (p->kpage);
  p->stat = PG_EVICTED;
}

static void
from_swap_disk (struct spte *p)
{
  ASSERT (p != NULL);

  if (debug)
    printf ("(%2d) swap from swap disk\n", thread_tid ());

  /* Allocate new frame to load evicted page from the swap disk to
     memory. */
  void *kpage = frame_alloc (p->upage, PAL_USER, true);
  ASSERT (p->kpage == kpage);

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
  ASSERT (p != NULL);

  

  if (debug)
    printf ("(%2d) swap from file: upage %p, kpage %p, file %p, ofs %d, read bytes %zu, zero bytes %zu\n",
            thread_tid (), p->upage, p->kpage,
            p->file, p->ofs, p->page_read_bytes, p->page_zero_bytes);

  void *kpage = frame_alloc (p->upage, PAL_USER, p->writable);

  if (debug)
    printf ("(%2d) swap from file: frame allocated\n", thread_tid ());

  file_seek (p->file, p->ofs);

//  if (debug)
//    printf ("(%2d) from file: file seek\n", thread_tid ());

  if (file_read (p->file, kpage, p->page_read_bytes)
      != (int)p->page_read_bytes)
    {
      frame_free (kpage);
      PANIC ("Cannot load page from file");
    }
//  if (debug)
//    printf ("(%2d) from file: file read\n", thread_tid ());

  memset (kpage + p->page_read_bytes, 0, p->page_zero_bytes);

//  if (debug)
//    printf ("(%2d) from file: memset\n", thread_tid ());

  /* If page is writable and not mapped, page must be evicted to
     and reloaded from swap disk from now on. */
  if (p->writable == true && p->mapped == false)
    {
      if (debug)
        printf ("(%2d) swap from file: page is writable and not mapped; file be NULL.\n", thread_tid ());
      p->file = NULL;
    }

  p->stat = PG_ON_MEMORY;

  if (debug)
    printf ("(%2d) swap from file end\n", thread_tid ());

  
}
