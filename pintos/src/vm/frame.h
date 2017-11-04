#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"

/* Frame table entry */
struct fte
  {
    void *kpage;                /* Address to kernel page */
    void *upage;                /* Address to user page */
    struct thread *process;     /* Owner of this frame */

    bool accessed;              /* Accessed bit */
    bool dirty;                 /* Dirty bit */

    struct hash_elem elem;
  };


void frame_table_init (void);
bool frame_table_insert (struct fte *);
struct fte *frame_table_find (void *);
void frame_table_del (struct fte *);

void *frame_alloc (void *, enum palloc_flags, bool);
void frame_free (void *kpage);

#endif
