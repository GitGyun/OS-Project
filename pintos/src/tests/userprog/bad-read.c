/* This program attempts to read memory at an address that is not mapped.
   This should terminate the process with a -1 exit code. */

#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{
	int *p = NULL;
	//int x = *p;
	*p = 3;
  /*msg ("Congratulations - you have successfully dereferenced NULL: %d", 
        *(int *)NULL);
  fail ("should have exited with -1");*/
}
