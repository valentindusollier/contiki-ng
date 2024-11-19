#include "contiki.h" /* Main include file for OS-specific modules. */
#include <stdio.h> /* For printf. */

PROCESS(test_proc, "Test process");
AUTOSTART_PROCESSES(&test_proc);

PROCESS_THREAD(test_proc, ev, data)
{
  PROCESS_BEGIN();

  printf("Hello, world!\n");

  PROCESS_END();
}