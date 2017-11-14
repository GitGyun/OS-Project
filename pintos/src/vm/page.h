#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "filesys/file.h"
#include "filesys/off_t.h"

enum pg_status
  {
    PG_ON_MEMORY,       /* Page is currently on memory. */
    PG_EVICTED,         /* Writable and unmapped Page is swapped. */
  };

enum pg_source
  {
    PG_SWAP,            /* From swap disk */
    PG_FILE,            /* From file */
  };

/* Supplemental page table entry */
struct spte
  {
    void *kpage;                    /* Address to kernel page */
    void *upage;                    /* Address to user page */

    enum pg_status stat;            /* Status of page */
    enum pg_source src;             /* Where to search when page is not present */
    bool writable;                  /* Is the page writable or read-only? */
    bool mapped;                    /* Is the page mapped to a file? */

    /* For swapping */
    size_t pg_idx;                  /* Page index in swap disk which page is evicted to */

    /* For lazily loaded page and mapped file */
    struct file *file;
    off_t ofs;
    size_t page_read_bytes;
    size_t page_zero_bytes;

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
