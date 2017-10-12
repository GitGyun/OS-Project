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

#define INC_PTR(ptr, bytes) ptr = (((uint8_t *)(ptr)) + (bytes))

#define PR_EXIT 0
#define PR_DEBUG_SYSCALL 0

static void syscall_handler (struct intr_frame *);
static bool get_args (void *, int);
//static int allocate_fd (void);
static struct file *fd_to_file (int);

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

/* Lock used by allocate_fd(). */
//static struct lock fd_lock;

uint32_t args[7];
const int arg_nums[] = {0, 1, 1, 1, 5, 1, 1, 1, 7, 7,
                        5, 1, 1, 2, 1, 1, 1, 2, 1, 1};

#if PR_DEBUG_SYSCALL
const char *syscall_str[] = {"halt",   "exit",    "exec",   "wait",
                             "create", "remove",  "open",   "filesize",
                             "read",   "write",   "seek",   "tell",
                             "close",  "mmap",    "munmap", "chdir",
                             "mkdir",  "readdir", "isdir",  "inumber"};
#endif

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  //lock_init (&fd_lock);
  lock_init (&file_lock);
  lock_init (&load_lock);
  lock_init (&hexdump_lock);
}

/* Number of arguments:

     exit:     1      seek:    5
     exec:     1      tell:    1
     wait:     1      close:   1
     create:   5      mmap:    2
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
    {
      //printf ("\nsystem call number invalid\n");
      syscall_exit (-1);
    }

#if PR_DEBUG_SYSCALL
  printf ("\nsystem call!\n");
  printf ("syscall no.: %s\n", syscall_str[syscall_num]);
#endif

  bool success = get_args (f->esp, syscall_num);

  if (!success)
    syscall_exit (-1);

#if PR_DEBUG_SYSCALL
  if (syscall_num != SYS_WRITE)
    {
      printf("=========\n");
      hex_dump ((uintptr_t)f->esp, f->esp, 62, true);
      printf("=========\n");
    }
#endif

  switch (syscall_num)
    {
    case SYS_HALT:    /* void -> void */
      power_off ();
      break;
    case SYS_EXIT:    /* int -> void */
    {
      syscall_exit ((int)args[0]);
      break;
    }
    case SYS_EXEC:      /* char * -> pid_t */
    {
      get_args (f->esp, 1);

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
    }

  //thread_exit ();
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

/*
static int
allocate_fd (void)
{
  static int next_fd = 2;
  int fd;

  lock_acquire (&fd_lock);
  fd = next_fd++;
  lock_release (&fd_lock);

  printf ("new fd: %d\n", fd);

  return fd;
}
*/

static struct file *
fd_to_file (int fd)
{
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

/* ================================= */
/* Syscall handlers for each syscall */
/* ================================= */

void
syscall_exit (int status)
{
  //printf ("syscall exit:: status %d\n", status);

  thread_current ()->exit_status = status;
  printf ("%s: exit(%d)\n", thread_name (), status);
  thread_exit ();
}

static tid_t
syscall_exec (const char *file)
{
#if PR_DEBUG_SYSCALL
  printf ("syscall exec!\n");
#endif

  lock_acquire (&file_lock);

  tid_t tid = process_execute (file);

#if PR_DEBUG_SYSCALL
  printf ("process execute complete\n");
  printf ("file name: %s\n", file);
  printf ("tid: %d\n", tid);
#endif

  lock_release (&file_lock);
  return tid;
}

static int
syscall_wait (tid_t tid)
{
  //printf ("current thread waiting status: %s\n", thread_current ()->is_waiting?"waiting":"not waiting");
  //printf ("syscall wait!\n");
  //printf ("child tid: %d\n", tid);

  if (thread_current ()->is_waiting)
    /* Thread is already waiting */
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
#if PR_EXIT
      printf ("in syscall create: NULL error\n");
#endif
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
#if PR_EXIT
      printf ("in syscall open: NULL error\n");
#endif
      lock_release (&file_lock);
      syscall_exit (-1);
    }

  if (is_kernel_vaddr (file))
    {
#if PR_EXIT
      printf ("in syscall open: kernel vaddr error\n");
#endif
      lock_release (&file_lock);
      syscall_exit (-1);
    }

  struct file *f = filesys_open (file);

  //printf ("syscall open\n");
  //printf ("file name: %s\n", file);

  /* File open failed */
  if (f == NULL)
    {
      lock_release (&file_lock);
      return -1;
    }

  /* If this file is executable, deny writing */
  //printf ("is there a thread with name: %s?\n", file);
  if (thread_same_name (file))
    file_deny_write (f);

  struct thread *curr = thread_current ();

  //int fd = allocate_fd ();
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

#define PR_READ 0

static int
syscall_read (int fd, void *buffer, unsigned size)
{
  lock_acquire (&file_lock);

  if (buffer == NULL)
    {
#if PR_READ
      printf ("buffer null\n");
#endif

      lock_release (&file_lock);
      return -1;
    }

  if (is_kernel_vaddr (buffer) ||
      is_kernel_vaddr ((uint8_t *)buffer + size - 1))
    {
#if PR_EXIT
      printf ("in syscall read: kernel vaddr error\n");
#endif
      lock_release (&file_lock);
      syscall_exit (-1);
    }

#if PR_READ
  printf("syscall read! ");
  printf("fd: %d ", fd);
  printf("size: %d\n", (int)size);
#endif // PR_READ

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
#if PR_READ
      printf ("fd = 1\n");
#endif

      lock_release (&file_lock);
      return -1;
    }

  struct file *f = fd_to_file (fd);
  if (f == NULL)
    {
#if PR_READ
      printf ("file null\n");
      printf ("fd: %d\n", fd);

      struct thread *curr = thread_current ();
      struct list *fl = &curr->fd_list;
      struct list_elem *e;
      struct fd_elem *fe;

      for (e = list_begin (fl); e != list_end (fl); e = list_next (e))
        {
          fe = list_entry (e, struct fd_elem, elem);
          printf ("fd list: %d\n", fe->fd);
        }
#endif
      lock_release (&file_lock);
      return -1;
    }

  int size_read = (int)file_read (f, buffer, size);
#if PR_READ
  printf("read size: %u\n", size_read);

  /*
  unsigned i;
  printf ("read buffer: ");
  for (i = 0; i < size; i++)
    printf ("%c", ((char *)buffer)[i]);
  printf ("\n");*/
#endif

  lock_release (&file_lock);
  return size_read;
}

#define PR_WRITE 0

static int
syscall_write (int fd, void *buffer, unsigned size)
{
  lock_acquire (&file_lock);

  if (buffer == NULL)
    {
#if PR_EXIT
      printf ("in syscall read: buffer null error\n");
#endif
      lock_release (&file_lock);
      return -1;
    }

  if (is_kernel_vaddr (buffer) ||
      is_kernel_vaddr ((uint8_t *)buffer + size - 1))
    {
#if PR_EXIT
      printf ("in syscall read: kernel vaddr error\n");
#endif
      lock_release (&file_lock);
      syscall_exit (-1);
    }

#if PR_WRITE
  printf("syscall write!\n");
  printf("fd: %d\n", fd);
  printf("size: %d\n", (int)size);
#endif
  if (fd == 0)
    {
      lock_release (&file_lock);
      return -1;
    }

  if (fd == 1)
    {
      /* Read the data from console and write to buffer */
      putbuf (buffer, size);
      lock_release (&file_lock);
      return (int)size;
    }

  struct file *f = fd_to_file (fd);
  if (f == NULL)
    {
      lock_release (&file_lock);
      return -1;
    }

  int size_written = (int)file_write (f, buffer, size);

#if PR_WRITE
  printf("written size: %d\n", size_written);

  unsigned i;
  printf ("written buffer: ");
  for (i = 0; i < size; i++)
    printf ("%c", ((char *)buffer)[i]);
  printf("\n");
#endif

  lock_release (&file_lock);
  return size_written;
}

#define PR_SEEK 0

static void
syscall_seek (int fd, unsigned pos)
{
#if PR_SEEK
  printf ("syscall seek!\n");
  printf ("fd: %d\n", fd);
  printf ("pos: %u\n", pos);
#endif

  lock_acquire (&file_lock);

  struct file *file = fd_to_file (fd);
  if (file == NULL)
    {
#if PR_EXIT
      printf ("in syscall seek: file null error\n");
#endif
      lock_release (&file_lock);
      return;
    }

  //printf ("calling file_seek\n");
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

  struct list *fl = &thread_current ()->fd_list;
  struct list_elem *e;
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
