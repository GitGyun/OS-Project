#include "vm/swap.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "devices/disk.h"
#include <hash.h>
#include <bitmap.h>
#include <stdio.h>

static struct hash swap_table;
static struct bitmap empty_sector;
static disk_sector_t sec_no_curr;

static unsigned swap_hash (const struct hash_elem *, void *);
static bool ste_cmp_less (const struct hash_elem *,
                          const struct hash_elem *, void *);

/* Create and initialize the swap table. */
void
swap_table_init (void)
{
	hash_init (&swap_table, swap_hash, ste_cmp_less, NULL);
}

/* Insert ste to the swap table */
bool
swap_table_insert (struct ste *s)
{
  bool is_already_in = (hash_insert (&swap_table, &s->elem) == NULL);

  return is_already_in;
}

/* Find ste from address */
struct ste *
swap_table_find (void *upage)
{
  struct ste s;
  s.upage = upage;

  struct hash_elem *e = hash_find (&swap_table, &s.elem);

  if (e != NULL)
    return hash_entry (e, struct ste, elem);

  return NULL;
}

/* Delete ste from the swap table */
void
swap_table_del (struct ste *s)
{
  hash_delete (&swap_table, &s->elem);
}

void
swap_out (struct fte *victim)
{
	struct disk *swap = disk_get (1, 1);
	disk_sector_t sec_no = sec_no_curr;

	/* Make a swap table entry for the swap slot of the victim. */
	struct ste *s = malloc (sizeof (struct ste));
	s->upage = victim->upage;
	s->sec_no = sec_no;
	s->writable = victim->writable;

	/* Swap out the victim page to the swap partition. */
	int i;
	for (i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++)
		{
			if (sec_no >= disk_size (swap))
				PANIC ("No remaining swap slot!");
			
      disk_write (swap, sec_no, (uint8_t *)victim->kpage + DISK_SECTOR_SIZE*i);
			sec_no++;
		}

	sec_no_curr = sec_no;

	swap_table_insert (s);
}

void
swap_in (struct ste *slot)
{
  //printf ("swap in!!!\n");
  void *swap = disk_get (1, 1);
  disk_sector_t sec_no = slot->sec_no;

  void *kpage = frame_alloc (slot->upage, PAL_USER, slot->writable);

  int i;
  for (i = 0; i < PGSIZE / DISK_SECTOR_SIZE; i++)
    {
      disk_read (swap, sec_no, (uint8_t *)kpage + DISK_SECTOR_SIZE*i);
      sec_no++;
    }

  swap_table_del (slot);
  free (slot);
}



/* ===== Helpers ===== */

/* Hash function */
static unsigned
swap_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct ste *s = hash_entry (e, struct ste, elem);
  return hash_bytes (&s->upage, sizeof s->upage);
}

/* Compare ste's addresses */
static bool
ste_cmp_less (const struct hash_elem *a_,
              const struct hash_elem *b_, void *aux UNUSED)
{
  const struct ste *a = hash_entry (a_, struct ste, elem);
  const struct ste *b = hash_entry (b_, struct ste, elem);

  return a->upage < b->upage;
}

