#include "vm/page.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include <stdio.h>

extern struct semaphpore paging_lock;

static unsigned spte_hash (const struct hash_elem *, void *);
static bool spte_cmp_upage (const struct hash_elem *,
                            const struct hash_elem *, void *);
static void spt_clear_func (struct hash_elem *, void *);


/* Initialize the supplemental page table. Called by load() */
struct hash *
suppl_page_table_create (void)
{
  struct hash *spt = malloc (sizeof (struct hash));
  if (spt != NULL)
    hash_init (spt, spte_hash, spte_cmp_upage, NULL);

  return spt;
}

void
suppl_page_table_del (struct hash *spt)
{
  sema_down (&paging_lock);
  hash_destroy (spt, spt_clear_func);
  free (spt);
  sema_up (&paging_lock);
}

/* Create and initialize new supplemental page table entry. */
struct spte *
spte_create (void *upage, void *kpage)
{
  struct spte *p = malloc (sizeof (struct spte));
  p->kpage = kpage;
  p->upage = upage;
  p->writable = true;
  p->mapped = false;
  p->file = NULL;

  return p;
}

/* Insert spte to the supplemental page table */
bool
suppl_page_table_insert (struct hash *spt, struct spte *p)
{
  sema_down (&paging_lock);
  bool success = (hash_insert (spt, &p->elem) == NULL);
  sema_up (&paging_lock);

  return success;
}

/* Find spte from address. If not exist, return NULL */
struct spte *
suppl_page_table_find (struct hash *spt, void *upage)
{
  struct spte p;
  p.upage = upage;

  sema_down (&paging_lock);
  struct hash_elem *e = hash_find (spt, &p.elem);
  sema_up (&paging_lock);

  if (e != NULL)
    return hash_entry (e, struct spte, elem);
  return NULL;
}



/* ===== Helpers ===== */

/* Hash function */
static unsigned
spte_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct spte *p = hash_entry (e, struct spte, elem);
  return hash_bytes (&p->upage, sizeof p->upage);
}

/* Compare fte's addresses */
static bool
spte_cmp_upage (const struct hash_elem *a_,
                const struct hash_elem *b_, void *aux UNUSED)
{
  const struct spte *a = hash_entry (a_, struct spte, elem);
  const struct spte *b = hash_entry (b_, struct spte, elem);

  return a->upage < b->upage;
}

static void
spt_clear_func (struct hash_elem *e, void *aux UNUSED)
{
  struct spte *p = hash_entry (e, struct spte, elem);

  switch (p->stat)
    {
    case PG_ON_MEMORY:
    {
      /* Deallocate frame table entry */
      struct fte *f = frame_table_find (p->kpage);
      if (f != NULL)
        {
          ASSERT (f->process == thread_current ());
          frame_free (f->kpage);
        }
      break;
    }
    case PG_EVICTED:
    {
      if (p->file == NULL)
        /* Empty page slot in the swap disk */
        swap_table_set_available (p->pg_idx, true);
      break;
    }
    default:
      break;
    }

  free (p);
}

void
print_spte (struct spte *p)
{
  printf ("spte with kpage %p, upage %p, status %s, wrtable %s, mapped %s, file %p\n",
          p->kpage, p->upage,
          p->stat == PG_ON_MEMORY? "PG_ON_MEMORY" : "PG_EVICTED",
          p->writable? "true" : "false",
          p->mapped? "true" : "false",
          p->file);
}
