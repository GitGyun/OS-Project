#include "vm/frame.h"
#include <stdio.h>

static struct hash frame_table;

static unsigned fte_hash (const struct hash_elem *, void *);


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
frame_table_find (const void *paddr)
{
  struct fte f;
  f.paddr = paddr;

  struct hash_elem *e = hash_find (&frame_table, &f.elem);

  if (e != NULL)
    return hash_entry (e, struct fte, elem);
  return NULL;
}

/* Allocate a new frame */
void *
frame_alloc ()


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
