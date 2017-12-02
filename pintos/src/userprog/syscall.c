#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "threads/init.h"
#include "userprog/process.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "threads/vaddr.h"
#include "userprog/exception.h"
#ifdef VM
#include "vm/swap.h"
#endif

#define INC_PTR(ptr, bytes) ptr = (((uint8_t *)(ptr)) + (bytes))

static void syscall_handler (struct intr_frame *);
static bool get_args (void *, int);
static struct file *fd_to_file (int);
#ifdef VM
static struct mmap_elem *mapid_to_me (int);
#endif

static tid_t syscall_exec (const char *);
static int syscall_wait (tid_t);
static bool syscall_create (char *, unsigned);
static bool syscall_remove (char *);
static int syscall_open (const char *);
static int syscall_filesize (int);
static int syscall_read (int, void *, unsigned);
static int syscall_write (int, void *, unsigned);
static void syscall_seek (int, unsigned);
static unsigned syscall_tell (int);
static void syscall_close (int);
#ifdef VM
static int syscall_mmap (int, void *);
#endif

/* Lock for file read, write, etc... */
struct lock file_lock;

uint32_t args[7];
const int arg_nums[] = {0, 1, 1, 1, 5, 1, 1, 1, 7, 7,
                        5, 1, 1, 5, 1, 1, 1, 2, 1, 1};

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&file_lock);
}

/* Number of arguments:

     exit:     1      seek:    5
     exec:     1      tell:    1
     wait:     1      close:   1
     create:   5      mmap:    5
     remove:   1      munmap:  1
     open:     1      chdir:   1
     filesize: 1      mkdir:   1
     read:     7      readdir: 2
     write:    7      isdir:   1
                      inumber: 1
*/

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  if (is_kernel_vaddr (f->esp))
    syscall_exit (-1);

  int syscall_num = *(int *)f->esp;
  if (syscall_num < SYS_HALT || syscall_num > SYS_INUMBER)
    syscall_exit (-1);

  bool success = get_args (f->esp, syscall_num);

  if (!success)
    syscall_exit (-1);

#ifdef VM
  /* Copy esp for page fault handling */
  thread_current ()->user_esp = f->esp;
#endif

  switch (syscall_num)
    {
    case SYS_HALT:      /* void -> void */
      power_off ();
      break;
    case SYS_EXIT:      /* int -> void */
    {
      syscall_exit ((int)args[0]);
      break;
    }
    case SYS_EXEC:      /* char * -> pid_t */
    {
      tid_t tid = syscall_exec ((char *)args[0]);

      f->eax = (uint32_t)tid;
      break;
    }
    case SYS_WAIT:      /* pid_t -> int */
    {
      int child_status = syscall_wait ((tid_t)args[0]);

      f->eax = (uint32_t)child_status;
      break;
    }
    case SYS_CREATE:    /* char *, unsigned -> bool */
    {
      bool success = syscall_create ((char *)args[3], (off_t)args[4]);

      f->eax = (uint32_t)success;
      break;
    }
    case SYS_REMOVE:    /* char * -> bool */
    {
      bool success = syscall_remove ((char *)args[0]);

      f->eax = (uint32_t)success;
      break;
    }
    case SYS_OPEN:      /* char * -> int */
    {
      int fd = syscall_open ((char *)args[0]);

      f->eax = (uint32_t)fd;
      break;
    }
    case SYS_FILESIZE:  /* int -> int */
    {
      int len = syscall_filesize ((int)args[0]);

      f->eax = (uint32_t)len;
      break;
    }
    case SYS_READ:      /* int, void *, unsigned -> int */
    {
      int size = syscall_read ((int)args[4], (void *)args[5],
                               (unsigned)args[6]);

      f->eax = (uint32_t)size;
      break;
    }
    case SYS_WRITE:     /* int, void *, unsigned -> int */
    {
      int size = syscall_write ((int)args[4], (void *)args[5],
                                (unsigned)args[6]);

      f->eax = (uint32_t)size;
      break;
    }
    case SYS_SEEK:      /* int, unsigned -> void */
    {
      syscall_seek ((int)args[3], (unsigned)args[4]);
      break;
    }
    case SYS_TELL:      /* int -> unsigned */
    {
      unsigned pos = syscall_tell ((int)args[0]);

      f->eax = pos;
      break;
    }
    case SYS_CLOSE:     /* int -> void */
    {
      syscall_close ((int)args[0]);
      break;
    }
#ifdef VM
    case SYS_MMAP:
    {
      int mapid = syscall_mmap ((int)args[3], (void *)args[4]);

      f->eax = mapid;
      break;
    }
    case SYS_MUNMAP:
    {
      syscall_munmap ((int)args[0]);
      break;
    }
#endif
    }
}

/* Return true if succeeded to get arguments */
static bool
get_args (void *esp, int num)
{
  int argc = arg_nums[num];
  if (argc == 0)
    return true;

  if (is_kernel_vaddr (esp))
    return false;

  INC_PTR (esp, 4);

  int i;
  for (i = 0; i < argc; i++)
    {
      if (is_kernel_vaddr (esp))
        return false;
      args[i] = *(uint32_t *)esp;
      INC_PTR (esp, 4);
    }

  return true;
}

static struct file *
fd_to_file (int fd)
{
  if (fd == 0 || fd == 1)
    return NULL;

  struct thread *curr = thread_current ();
  struct list_elem *e;
  struct fd_elem *fe;

  for (e = list_begin (&curr->fd_list);
       e != list_end (&curr->fd_list); e = list_next (e))
    {
      fe = list_entry (e, struct fd_elem, elem);
      if (fe->fd == fd)
        return fe->file;
    }
  return NULL;
}

#ifdef VM
static struct mmap_elem *
mapid_to_me (int mapid)
{
  struct thread *curr = thread_current ();
  struct list_elem *e;
  struct mmap_elem *me;

  for (e = list_begin (&curr->mmap_list);
       e != list_end (&curr->mmap_list); e = list_next (e))
    {
      me = list_entry (e, struct mmap_elem, elem);
      if (me->mapid == mapid)
        return me;
    }
  return NULL;
}
#endif

/* ================================= */
/* Syscall handlers for each syscall */
/* ================================= */

void
syscall_exit (int status)
{
  thread_current ()->exit_status = status;

  //printf ("(%d) ", thread_tid ());

  printf ("%s: exit(%d)\n", thread_name (), status);
  thread_exit ();
}

static tid_t
syscall_exec (const char *file)
{
  lock_acquire (&file_lock);

  tid_t tid = process_execute (file);

  lock_release (&file_lock);
  return tid;
}

static int
syscall_wait (tid_t tid)
{
  /* If thread is already waiting, cannot wait. */
  if (thread_current ()->is_waiting)
    return -1;

  return process_wait (tid);
}

static bool
syscall_create (char *name, unsigned initial_size)
{
  lock_acquire (&file_lock);

  if (name == NULL)
    {
      lock_release (&file_lock);
      syscall_exit (-1);
    }

  bool success = filesys_create (name, initial_size);

  lock_release (&file_lock);
  return success;
}

static bool
syscall_remove (char *name)
{
  lock_acquire (&file_lock);

  bool success = filesys_remove (name);

  lock_release (&file_lock);
  return success;
}

static int
syscall_open (const char *file)
{
  lock_acquire (&file_lock);

  if (file == NULL)
    {
      lock_release (&file_lock);
      syscall_exit (-1);
    }

  if (is_kernel_vaddr (file))
    {
      lock_release (&file_lock);
      syscall_exit (-1);
    }

  struct file *f = filesys_open (file);

  /* File open failed */
  if (f == NULL)
    {
      lock_release (&file_lock);
      return -1;
    }

  /* If this file is executable, deny writing */
  if (thread_same_name (file))
    file_deny_write (f);

  struct thread *curr = thread_current ();
  struct fd_elem *fe = malloc (sizeof (struct fd_elem));

  /* fd_elem allocation failed */
  if (fe == NULL)
    {
      file_close (f);
      lock_release (&file_lock);
      return -1;
    }

  /* Allocate new fd */
  struct list *fl = &curr->fd_list;
  int new_fd = 2;

  if (!list_empty (fl))
    new_fd = list_entry (list_back (fl),
                         struct fd_elem, elem)->fd + 1;

  fe->fd = new_fd;
  fe->file = f;

  list_push_back (&curr->fd_list, &fe->elem);

  lock_release (&file_lock);
  return new_fd;
}

static int
syscall_filesize (int fd)
{
  lock_acquire (&file_lock);

  struct file *file = fd_to_file (fd);

  if (file == NULL)
  {
    lock_release (&file_lock);
    return -1;
  }

  int len = file_length (file);

  lock_release (&file_lock);
  return len;
}

static int
syscall_read (int fd, void *buffer, unsigned size)
{
  lock_acquire (&file_lock);

  if (buffer == NULL)
    {
      lock_release (&file_lock);
      return -1;
    }

  if (is_kernel_vaddr (buffer) ||
      is_kernel_vaddr ((uint8_t *)buffer + size - 1))
    {
      lock_release (&file_lock);
      syscall_exit (-1);
    }

  /* Use input_getc() */
  if (fd == 0)
    {
      unsigned i;
      for (i = 0; i < size; i++)
        /* Read one character and write to buffer */
        ((uint8_t *)buffer)[i] = input_getc ();

      lock_release (&file_lock);
      return (int)size;
    }

  if (fd == 1)
    {
      lock_release (&file_lock);
      return -1;
    }

  struct file *f = fd_to_file (fd);
  if (f == NULL)
    {
      lock_release (&file_lock);
      return -1;
    }

  int size_read = (int)file_read (f, buffer, size);

  lock_release (&file_lock);
  return size_read;
}

static int
syscall_write (int fd, void *buffer, unsigned size)
{
  lock_acquire (&file_lock);

  if (buffer == NULL)
    {
      lock_release (&file_lock);
      return -1;
    }

  if (is_kernel_vaddr (buffer) ||
      is_kernel_vaddr ((uint8_t *) buffer + size - 1))
    {
      lock_release (&file_lock);
      syscall_exit (-1);
    }

  if (fd == 0)
    {
      lock_release (&file_lock);
      return -1;
    }

  if (fd == 1)
    {
      putbuf (buffer, size);
      lock_release (&file_lock);
      return (int) size;
    }

  struct file *f = fd_to_file (fd);
  if (f == NULL)
    {
      lock_release (&file_lock);
      return -1;
    }

  int size_written = (int) file_write (f, buffer, size);

//  printf ("size actually written %d\n", size_written);

  lock_release (&file_lock);
  return size_written;
}

static void
syscall_seek (int fd, unsigned pos)
{
  lock_acquire (&file_lock);

  struct file *file = fd_to_file (fd);
  if (file == NULL)
    {
      lock_release (&file_lock);
      return;
    }

  file_seek (file, pos);

  lock_release (&file_lock);
}

static unsigned
syscall_tell (int fd)
{
  lock_acquire (&file_lock);

  struct file *file = fd_to_file (fd);
  unsigned pos = file_tell (file);

  lock_release (&file_lock);

  return pos;
}

static void
syscall_close (int fd)
{
  lock_acquire (&file_lock);

  struct list_elem *e;

#ifdef VM
  struct list *ml = &thread_current ()->mmap_list;
  struct mmap_elem *me;

  for (e = list_begin (ml); e != list_end (ml); e = list_next (e))
    {
      me = list_entry (e, struct mmap_elem, elem);
      if (me->fd == fd)
        {
          /* If file is mapped, reject closing. */
          lock_release (&file_lock);
          return;
        }
    }
#endif

  struct list *fl = &thread_current ()->fd_list;
  struct fd_elem *fe;

  for (e = list_begin (fl); e != list_end (fl); e = list_next (e))
    {
      fe = list_entry (e, struct fd_elem, elem);
      if (fe->fd == fd)
        {
          list_remove (&fe->elem);
          file_close (fe->file);
          free (fe);

          lock_release (&file_lock);
          return;
        }
    }

  lock_release (&file_lock);
}

#ifdef VM
static int
syscall_mmap (int fd, void *addr)
{
  if (addr == NULL || pg_ofs (addr) != 0)
    return -1;

  struct thread *t = thread_current ();

  struct file *file = fd_to_file (fd);
  if (file == NULL)
    return -1;

  int length = file_length (file);
  if (length == 0)
    return -1;

  int pg_num = (int) pg_round_up ((void *) length) / PGSIZE;
  if (addr + PGSIZE*pg_num >= PHYS_BASE - MAX_STACK_SIZE)
    return -1;

  int i;
  for (i = 0; i < pg_num; i++)
    if (suppl_page_table_find (t->suppl_page_table, addr + PGSIZE*i) != NULL)
      return -1;

  struct mmap_elem *me = malloc (sizeof (struct mmap_elem));
  if (me == NULL)
    return -1;

  for (i = 0; i < pg_num; i++)
    {
      struct spte *p = malloc (sizeof (struct spte));
      ASSERT (p != NULL);

      p->kpage = NULL;
      p->upage = addr + PGSIZE*i;

      p->writable = true;
      p->mapped = true;

      p->file = file;
      p->ofs = PGSIZE*i;
      p->page_read_bytes = (length > PGSIZE) ? PGSIZE : length;
      p->page_zero_bytes = PGSIZE - p->page_read_bytes;

      length -= PGSIZE;

      suppl_page_table_insert (t->suppl_page_table, p);
    }

  int new_mapid = 0;
  if (!list_empty (&t->mmap_list))
    new_mapid = list_entry (list_end (&t->mmap_list), struct mmap_elem, elem)->mapid + 1;

  me->mapid = new_mapid;
  me->fd = fd;
  me->file = file;
  me->pg_num = pg_num;
  me->addr = addr;

  list_push_back (&t->mmap_list, &me->elem);

  return new_mapid;
}

void
syscall_munmap (int mapid)
{
  struct thread *t = thread_current ();
  struct hash *spt = t->suppl_page_table;

  struct mmap_elem *me = mapid_to_me (mapid);
  if (me == NULL)
    return;

  int pg_num = me->pg_num;
  void *addr = me->addr;

  struct spte *p;
  struct fte *f;
  int i;
  for (i = 0; i < pg_num; i++)
    {
      p = suppl_page_table_find (spt, addr + PGSIZE*i);
      if (p->kpage != NULL)
        {
          f = frame_table_find (p->kpage);
          swap_out (f);
        }
      suppl_page_table_del_page (spt, p);
    }

  list_remove (&me->elem);
  free (me);
}
#endif
