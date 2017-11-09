#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>

enum pg_status
  {
    PG_ON_MEMORY,
    PG_EVICTED,
    PG_UNLOADED,
  };

/* Supplemental page table entry */
struct spte
  {
    void *kpage;            /* Address to kernel page */
    void *upage;            /* Address to user page */

    enum pg_status stat;    /* Status of current page */
    size_t pg_idx;          /* Page index in swap disk which page is evicted to */

    struct hash_elem elem;
  };


struct hash *suppl_page_table_create (void);
void suppl_page_table_del (struct hash *spt);
struct spte *spte_create (void *, void *);

bool suppl_page_table_insert (struct hash *, struct spte *);
struct spte *suppl_page_table_find (struct hash *, void *);
void suppl_page_table_del_page (struct hash *, struct spte *);

void suppl_page_table_set_page_status (struct hash *, void *, enum pg_status);

void suppl_page_table_print (struct hash *);

#endif
