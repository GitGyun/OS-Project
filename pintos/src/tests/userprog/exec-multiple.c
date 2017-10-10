/* Executes and waits for multiple child processes. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include <stdio.h>

void
test_main (void) 
{
  wait (exec ("child-simple"));
  wait (exec ("child-simple"));
  wait (exec ("child-simple"));
  wait (exec ("child-simple"));
}
