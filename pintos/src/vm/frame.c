#include "vm/frame.h"
#include <stdio.h>

static struct hash frame_table;

static unsigned fte_hash (const struct hash_elem *, void *);
static bool fte_cmp_paddr (const struct hash_elem *,
                           const struct hash_elem *, void *);


/* Initialize the frame table. Called by init.c */
void
frame_table_init (void)
{
  hash_init (&frame_table, fte_hash, fte_cmp_paddr, NULL);
}

/* Insert fte to the frame table */
void
frame_table_insert (struct fte *f)
{
  bool is_already_in = hash_insert (&frame_table, &f->elem);

  if (is_already_in)
    printf("already exist!\n");
}

/* Find fte from address */
struct fte *
frame_table_find (void *paddr)
{
  struct fte f;
  f.paddr = paddr;

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
frame_alloc (enum palloc_flags flags)
{
  void *kpage;
  while ((kpage = palloc_get_page (flags)) == NULL)
    {
      // TODO : eviction
    }

  struct fte *fte_new = malloc (sizeof (struct fte));
  fte_new->paddr = kpage;
  fte_new->proccess = thread_current ();

  hash_insert (&frame_table, &fte_new->elem);

  return kpage;
}

/* Free KPAGE */
void
frame_free (void *kpage)
{
  struct fte *f = frame_table_find (kpage);
  if (f != NULL)
    {
      palloc_free_page (kpage);
      frame_table_del (f);
      free (f);
    }
}

/* Hash function */
static unsigned
fte_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct fte *f = hash_entry (e, struct fte, elem);
  return hash_bytes (&f->paddr, sizeof(f->paddr));
}

/* Compare fte's addresses */
static bool
fte_cmp_paddr (const struct hash_elem *a_,
               const struct hash_elem *b_, void *aux UNUSED)
{
  const struct fte *a = hash_entry (a_, struct fte, elem);
  const struct fte *b = hash_entry (b_, struct fte, elem);

  return a->paddr < b->paddr;
}
