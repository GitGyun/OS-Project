#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"
#include <hash.h>

/* Frame table entry */
struct fte
  {
    uint8_t *paddr;
    struct thread *proccess;

    struct hash_elem elem;
  };


void frame_table_init (void);
void frame_table_insert (struct fte *f);
struct fte *frame_table_find (const void *paddr);

#endif
