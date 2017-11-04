#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/disk.h"
#include "vm/frame.h"
#include <hash.h>

/* Swap table entry */
struct ste
  {
    void *upage;							/* Virtual address for the user page. */
    disk_sector_t sec_no;			/* Disk sector number for the swap partition. */

    bool writable;

    struct hash_elem elem;		/* Hash element. */
  };

void swap_init (void);
void swap_insert (struct ste *);
struct ste *swap_find (void *);
void swap_del (struct ste *);

void swap_out (struct fte *);
void swap_in (struct ste *);

#endif
