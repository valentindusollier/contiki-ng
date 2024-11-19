#include "contiki.h" /* Main include file for OS-specific modules. */
#include <stdio.h> /* For printf. */
#include "dev/etc/rgb-led/rgb-led.h"
#include "sys/etimer.h"
#include "dev/button-hal.h"

PROCESS(christmas_tree, "Ultimate Christmas tree");
AUTOSTART_PROCESSES(&christmas_tree);

PROCESS_THREAD(christmas_tree, ev, data)
{
  static struct etimer timer;
  static uint8_t color_counter;
  static uint8_t speed_counter = 0;
  static int is_led_on = 1;
  button_hal_button_t *btn;

  PROCESS_BEGIN();

  etimer_set(&timer, CLOCK_SECOND / (speed_counter % 10 + 1));

  while(1) {
    PROCESS_YIELD();

    if (ev == button_hal_press_event) {
      speed_counter += 2;
      printf("Setting speed to %u time(s) by second\n", speed_counter % 10 + 1);
    } else if (ev == button_hal_periodic_event) {
      btn = (button_hal_button_t *)data;
      if (btn->press_duration_seconds >= 2) {
        is_led_on = !is_led_on;
        if (is_led_on) {
          printf("Turning on LED\n");
          etimer_set(&timer, CLOCK_SECOND / (speed_counter % 10 + 1));
        } else {
          printf("Turning off LED\n");
          rgb_led_off();
        }
      }
    } else if (ev == PROCESS_EVENT_TIMER && data == &timer && is_led_on) {
      if (color_counter % 7 == 0) rgb_led_set(RGB_LED_RED);
      else if (color_counter % 7 == 1) rgb_led_set(RGB_LED_GREEN);
      else if (color_counter % 7 == 2) rgb_led_set(RGB_LED_BLUE);
      else if (color_counter % 7 == 3) rgb_led_set(RGB_LED_CYAN);
      else if (color_counter % 7 == 4) rgb_led_set(RGB_LED_MAGENTA);
      else if (color_counter % 7 == 5) rgb_led_set(RGB_LED_YELLOW);
      else if (color_counter % 7 == 6) rgb_led_set(RGB_LED_WHITE);

      etimer_set(&timer, CLOCK_SECOND / (speed_counter % 10 + 1));
      color_counter++;
    }
  }

  PROCESS_END();
}