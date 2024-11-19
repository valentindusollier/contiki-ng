#include "contiki.h" /* Main include file for OS-specific modules. */
#include <stdio.h> /* For printf. */
#include "dev/etc/rgb-led/rgb-led.h"
#include "sys/etimer.h"

PROCESS(christmas_tree, "Christmas tree");
AUTOSTART_PROCESSES(&christmas_tree);

PROCESS_THREAD(christmas_tree, ev, data)
{
  static struct etimer timer;
  static uint8_t counter;

  PROCESS_BEGIN();

  etimer_set(&timer, CLOCK_SECOND / 2);

  while(1) {
    if (counter % 7 == 0) rgb_led_set(RGB_LED_RED);
    else if (counter % 7 == 1) rgb_led_set(RGB_LED_GREEN);
    else if (counter % 7 == 2) rgb_led_set(RGB_LED_BLUE);
    else if (counter % 7 == 3) rgb_led_set(RGB_LED_CYAN);
    else if (counter % 7 == 4) rgb_led_set(RGB_LED_MAGENTA);
    else if (counter % 7 == 5) rgb_led_set(RGB_LED_YELLOW);
    else if (counter % 7 == 6) rgb_led_set(RGB_LED_WHITE);

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
    etimer_reset(&timer);
    counter++;
  }

  PROCESS_END();
}