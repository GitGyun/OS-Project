#include "vm/page.h"
#include "threads/malloc.h"
#include <stdio.h>

static unsigned spte_hash (const struct hash_elem *, void *);
static bool spte_cmp_upage (const struct hash_elem *,
                            const struct hash_elem *, void *);



/* Initialize the supplemental page table. Called by load() */
struct hash *
suppl_page_table_create (void)
{
  struct hash *spt = malloc (sizeof (struct hash));
  if (spt != NULL)
    hash_init (spt, spte_hash, spte_cmp_upage, NULL);

  return spt;
}

/* Create and initialize new supplemental page table entry. */
struct spte *
spte_create (void *upage)
{
  struct spte *p = malloc(sizeof (struct spte));
  p->upage = upage;

  return p;
}

/* Insert spte to the supplemental page table */
void
suppl_page_table_insert (struct hash *spt, struct spte *p)
{
  bool is_already_in = hash_insert (spt, &p->elem);

  if (is_already_in)
    printf("already exist!\n");
}

/* Find spte from address */
struct spte *
suppl_page_table_find (struct hash *spt, void *upage)
{
  struct spte p;
  p.upage = upage;

  struct hash_elem *e = hash_find (spt, &p.elem);

  if (e != NULL)
    return hash_entry (e, struct spte, elem);
  return NULL;
}

/* Delete spte from the supplemental page table */
void
suppl_page_table_del (struct hash *spt, struct spte *p)
{
  hash_delete (spt, &p->elem);
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
