#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>

/* Supplemental page table entry */
struct spte
  {
    void *upage;        /* Address to user page */

    struct hash_elem elem;
  };


struct hash *suppl_page_table_create (void);
struct spte *spte_create (void *);

bool suppl_page_table_insert (struct hash *, struct spte *);
struct spte *suppl_page_table_find (struct hash *, void *);
void suppl_page_table_del (struct hash *, struct spte *);

#endif
