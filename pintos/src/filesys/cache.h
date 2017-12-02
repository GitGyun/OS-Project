#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/disk.h"
#include <stdbool.h>

/* Size of the buffer cache: 64 sectors. */
#define BUFFER_CACHE_SIZE 64

/* Buffer cache entry. */
struct bce
  {
    bool empty;
    disk_sector_t sec_no;

    uint8_t buffer[DISK_SECTOR_SIZE];

    bool accessed;
    bool dirty;
  };

void buffer_cache_init (void);
void buffer_cache_done (void);

void buffer_cache_disk_read (disk_sector_t, void *);
void buffer_cache_disk_write (disk_sector_t, const void *);


#endif
