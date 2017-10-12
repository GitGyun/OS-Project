#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>

void syscall_init (void);

struct fd_elem {
  int fd;
  struct file *file;
  struct list_elem elem;
};

/* Lock for file read, write, etc... */
struct lock file_lock;
struct lock load_lock;
struct lock hexdump_lock;

void syscall_exit (int);

#endif /* userprog/syscall.h */
