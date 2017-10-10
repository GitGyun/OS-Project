#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "lib/user/syscall.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "lib/kernel/console.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <string.h>

static void syscall_handler (struct intr_frame *);
struct lock filesys_lock;
struct lock fd_lock;
static char * scalls[13] = {"halt", "exit", "exec", "wait", "create", 
	"remove", "open", "filesize", "read", "write", "seek", "tell", "close"};

static void ptr_check (void *);
static uint32_t esp_pop (uint32_t **);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
  lock_init (&fd_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uint32_t *sp = f->esp;
  uint32_t **esp = &sp;
  //hex_dump ((uintptr_t) *esp, *esp, 64, true);
  int sys_num = (int) esp_pop (esp);
	
	//printf("%s-%d called %s\n", thread_name (), thread_current ()->tid, scalls[sys_num]);

  switch (sys_num)
		{
		case SYS_HALT: // sys# 0.
		{
			power_off ();
			break;
		}
		
		case SYS_EXIT: // sys# 1.
		{
			int status = (int) esp_pop (esp);
			
			printf("%s: exit(%d)\n", thread_name (), status);
			thread_current ()->exit_status = status;
			thread_exit ();
		}
		
		case SYS_EXEC: // sys# 2.
		{
			const char *cmd_line = (const char *) esp_pop (esp);
			
			ptr_check ((void *) cmd_line);
			
			lock_acquire (&filesys_lock);
			tid_t tid = process_execute (cmd_line);
			f->eax = tid;
			lock_release (&filesys_lock);
			break;
		}
		
		case SYS_WAIT: // sys# 3.
		{
			pid_t pid = (pid_t) esp_pop (esp);
			
			int status = process_wait (pid);
			f->eax = status;
			break;
		}
		
		case SYS_CREATE: // sys# 4.
		{
			esp_pop (esp);
			esp_pop (esp);
			esp_pop (esp);
			const char *file = (const char *) esp_pop (esp);
			unsigned size = (unsigned) esp_pop (esp);
			
			ptr_check ((void *) file);
			
			lock_acquire (&filesys_lock);
			bool success = filesys_create (file, size);
			f->eax = success;
			lock_release (&filesys_lock);
			break;
		}
		
		case SYS_REMOVE: // sys# 5.
		{
			const char *file = (const char *) esp_pop (esp);
			
			ptr_check ((void *) file);
			lock_acquire (&filesys_lock);
			bool success = filesys_remove (file);
			f->eax = success;
			lock_release (&filesys_lock);
			break;
		}
		
		case SYS_OPEN: // sys# 6.
		{
			const char *name = (const char *) esp_pop (esp);
			ptr_check ((void *) name);
			
			lock_acquire (&filesys_lock);
			struct file *file = filesys_open (name);
			
			if (file == NULL)
				{
					f->eax = -1;
					lock_release (&filesys_lock);
					break;
				}
			
			if (strcmp (thread_name (), name) == 0)
					file_deny_write (file);
			
			int fd = 2;
			if (!list_empty (&thread_current ()->open_files))
				fd = list_entry (list_back (&thread_current ()->open_files), struct file_elem, elem)->fd + 1;
			struct file_elem *file_elem = (struct file_elem *) malloc (sizeof (struct file_elem));
			file_elem->file = file;
			file_elem->fd = fd;
			list_push_back (&thread_current ()->open_files, &file_elem->elem);
			f->eax = fd;
			lock_release (&filesys_lock);
			break;
		}
		
		case SYS_FILESIZE: // sys#. 7
		{
			int fd = (int) esp_pop (esp);
			
			lock_acquire (&filesys_lock);
			struct file_elem *file_elem = thread_get_file_elem (fd);
			
			if (file_elem == NULL)
				exit_abnormal ();
			
			struct file *file = file_elem->file;
			int size = -1;
			if (file != NULL)
				size = file_length (file);
			f->eax = size;
			lock_release (&filesys_lock);
			break;
		}
		
		case SYS_READ: // sys# 8.
		{
			esp_pop (esp);
			esp_pop (esp);
			esp_pop (esp);
			esp_pop (esp);
			
			int fd = (int) esp_pop (esp);
			char *buffer = (char *) esp_pop (esp);
			unsigned size = (unsigned) esp_pop (esp);
			unsigned bytes;
			
			ptr_check ((void *) buffer);
			
			lock_acquire (&filesys_lock);
			
			/* Standard input. */
			if (fd == 0)
				{
					for (bytes = 0; bytes < size; bytes++);
						buffer[bytes] = input_getc ();
					f->eax = size;
					lock_release (&filesys_lock);
					break;
				}
			
			/* Read files. */
			struct file_elem *file_elem = thread_get_file_elem (fd);
			if (file_elem == NULL)
				exit_abnormal ();
			
			struct file *file = file_elem->file;
			bytes = file_read (file, (void *) buffer, size);
			f->eax = bytes;
			lock_release (&filesys_lock);
			break;
		}
		
		case SYS_WRITE: // sys# 9.
		{
			esp_pop (esp);
			esp_pop (esp);
			esp_pop (esp);
			esp_pop (esp);
			
			int fd = (int) esp_pop (esp);
			const char *buffer = (const char *) esp_pop (esp);
			unsigned size = (unsigned) esp_pop (esp);
			unsigned bytes;
			
			ptr_check ((void *) buffer);
			
			lock_acquire (&filesys_lock);
			/* Standard output. */
			if (fd == 1)
				{
					putbuf (buffer, size);
					f->eax = size;
					lock_release (&filesys_lock);
					break;
				}
			
			struct file_elem *file_elem = thread_get_file_elem (fd);
			if (file_elem == NULL)
				exit_abnormal ();
			
			struct file *file = file_elem->file;
			bytes = file_write (file, (void *) buffer, size);
			f->eax = bytes;
			lock_release (&filesys_lock);
			break;
		}
		
		case SYS_SEEK: // sys# 10.
		{
			esp_pop (esp);
			esp_pop (esp);
			esp_pop (esp);
			
			int fd = (int) esp_pop (esp);
			unsigned position = (unsigned) esp_pop (esp);
			
			lock_acquire (&filesys_lock);
			struct file_elem *file_elem = thread_get_file_elem (fd);
			if (file_elem == NULL)
				exit_abnormal ();
			
			struct file *file = file_elem->file;
			file_seek (file, position);
			lock_release (&filesys_lock);
			break;
		}
		
		case SYS_TELL: // sys# 11.
		{
			int fd = (int) esp_pop (esp);
			int position;
			
			lock_acquire (&filesys_lock);
			struct file_elem *file_elem = thread_get_file_elem (fd);
			if (file_elem == NULL)
				exit_abnormal ();
			
			struct file *file = file_elem->file;
			position = file_tell (file);
			f->eax = position;
			lock_release (&filesys_lock);
			break;
		}
		
		case SYS_CLOSE: // sys# 12.
		{
			int fd = (int) esp_pop (esp);
			
			lock_acquire (&filesys_lock);
			struct file_elem *file_elem = thread_get_file_elem (fd);
			if (file_elem == NULL)
				exit_abnormal ();
			
			file_close (file_elem->file);
			list_remove (&file_elem->elem);
			free (file_elem);
			lock_release (&filesys_lock);
			break;
		}
		}
}

/* Exit abnormally with termination message. */
void
exit_abnormal (void)
{
	if (filesys_lock.holder == thread_current ())
		lock_release (&filesys_lock);
	printf("%s: exit(-1)\n", thread_name ());
	thread_current ()->exit_status = -1;
	thread_exit ();
}

/* Checks if the pointer is valid in user space. */
static void
ptr_check (void *ptr)
{
	if ((ptr != NULL) && is_user_vaddr (ptr) && (pagedir_get_page (thread_current ()->pagedir, ptr) != NULL))
		return;
	
	exit_abnormal ();
}

/* Pops an argument from the stack. */
static uint32_t
esp_pop (uint32_t **esp)
{
	ptr_check (*esp);
	uint32_t arg = **esp;
	(*esp)++;
	return arg;
}
