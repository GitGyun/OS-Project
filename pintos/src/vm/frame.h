#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include <hash.h>

/* Frame table entry */
struct fte
  {
    uint8_t *paddr;							/* Physical address for the frame. */
    struct thread *proccess;		/* Process that occupying the frame. */
    bool accessed;							/* Accessed bit. */
    bool dirty;									/* Dirty bit. */

    struct hash_elem elem;
  };


void frame_table_init (void);
void frame_table_insert (struct fte *);
struct fte *frame_table_find (void *);
void frame_table_del (struct fte *);

void *frame_alloc (enum palloc_flags);
void frame_free (void *kpage);

#endif
