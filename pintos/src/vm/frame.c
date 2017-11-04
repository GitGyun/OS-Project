#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <hash.h>
#include <stdio.h>

static struct hash frame_table;

static unsigned fte_hash (const struct hash_elem *, void *);
static bool fte_cmp_kpage (const struct hash_elem *,
                           const struct hash_elem *, void *);
struct fte *frame_select_victim (void);


/* Initialize the frame table. Called by init.c */
void
frame_table_init (void)
{
  hash_init (&frame_table, fte_hash, fte_cmp_kpage, NULL);
}

/* Insert fte to the frame table */
bool
frame_table_insert (struct fte *f)
{
  bool success = (hash_insert (&frame_table, &f->elem) == NULL);

  return success;
}

/* Find fte from address */
struct fte *
frame_table_find (void *kpage)
{
  struct fte f;
  f.kpage = kpage;

  struct hash_elem *e = hash_find (&frame_table, &f.elem);

  if (e != NULL)
    return hash_entry (e, struct fte, elem);
  return NULL;
}

/* Delete fte from the frame table */
void
frame_table_del (struct fte *f)
{
  hash_delete (&frame_table, &f->elem);
}

/* Allocate a new frame */
void *
frame_alloc (void *upage, enum palloc_flags flags, bool writable)
{
  struct thread *t = thread_current ();

  void *kpage;
  if ((kpage = palloc_get_page (flags)) == NULL)
    {
      /* fte to be needed */
      struct fte *victim = frame_select_victim ();

      /* Evict the victim to swap disk */
      swap_out (victim);

      kpage = palloc_get_page (flags);
    }

  struct fte *fte_new = malloc (sizeof (struct fte));
  fte_new->kpage = kpage;
  fte_new->upage = upage;
  fte_new->proccess = thread_current ();
  fte_new->accessed = false;
  fte_new->dirty = false;

  hash_insert (&frame_table, &fte_new->elem);

  /* upage - kpage mapping */
  bool success = pagedir_get_page (t->pagedir, upage) == NULL
                 && pagedir_set_page (t->pagedir, upage, kpage, writable);
  if (!success)
    {
      palloc_free_page (kpage);
      return NULL;
    }

  /* Update suppl. page table */
  struct spte *p = spte_create (upage);
  success = suppl_page_table_insert(t->suppl_page_table, p);
  if (!success)
    return NULL;

  return kpage;
}

/* Free KPAGE */
void
frame_free (void *kpage)
{
  struct thread *t = thread_current ();
  struct fte *f = frame_table_find (kpage);

  if (f != NULL)
    {
      palloc_free_page (kpage);
      frame_table_del (f);
      free (f);

      void *upage = f->upage;
      pagedir_clear_page (t->pagedir, upage);

      struct spte *p = suppl_page_table_find (t->suppl_page_table, upage);
      if (p != NULL)
        suppl_page_table_del(t->suppl_page_table, p);
    }
}

/* Select fte to be evicted */
struct fte *
frame_select_victim (void)
{
  struct hash_iterator i;

  hash_first (&i, &frame_table);
  hash_next (&i);
  struct fte *f = hash_entry (hash_cur (&i), struct fte, elem);

  return f;
}



/* ===== Helpers ===== */

/* Hash function */
static unsigned
fte_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct fte *f = hash_entry (e, struct fte, elem);
  return hash_bytes (&f->kpage, sizeof f->kpage);
}

/* Compare fte's addresses */
static bool
fte_cmp_kpage (const struct hash_elem *a_,
               const struct hash_elem *b_, void *aux UNUSED)
{
  const struct fte *a = hash_entry (a_, struct fte, elem);
  const struct fte *b = hash_entry (b_, struct fte, elem);

  return a->kpage < b->kpage;
}
