#include "contiki.h" /* Main include file for OS-specific modules. */
#include <stdio.h> /* For printf. */
#include "dev/etc/rgb-led/rgb-led.h"

PROCESS(test_led, "Test led");
AUTOSTART_PROCESSES(&test_led);

PROCESS_THREAD(test_led, ev, data)
{
  PROCESS_BEGIN();

  printf("Setting led to magenta\n");
  rgb_led_set(RGB_LED_MAGENTA);

  PROCESS_END();
}