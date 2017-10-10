#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

#define MAX_ARGS 32																							/* Maximum number of arguments. */
#define PTR_DEC(ptr, bytes) ptr = (((uint8_t *) ptr) - bytes)		/* Decrement the pointer by bytes. */

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
