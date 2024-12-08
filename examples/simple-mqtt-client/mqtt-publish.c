#include "contiki.h"
#include "net/routing/routing.h"
#include "mqtt.h"
#include "mqtt-prop.h"
#include "net/ipv6/uip.h"
#include "net/ipv6/uip-icmp6.h"
#include "net/ipv6/sicslowpan.h"
#include "sys/etimer.h"
#include "sys/ctimer.h"
#include "lib/sensors.h"
#include "dev/button-hal.h"
#include "dev/leds.h"
#include "os/sys/log.h"

#include <string.h>
#include <strings.h>
#include <stdarg.h>
/*---------------------------------------------------------------------------*/
/* Logging module and level configuration */
#define LOG_MODULE "MQTT"
#ifdef MQTT_CLIENT_CONF_LOG_LEVEL
#define LOG_LEVEL MQTT_CLIENT_CONF_LOG_LEVEL
#else
#define LOG_LEVEL LOG_LEVEL_INFO
#endif
/*---------------------------------------------------------------------------*/
/* Various MQTT client states */
static uint8_t state;
#define STATE_INIT 0
#define STATE_WAITING_CONNECTIVITY 1
#define STATE_WAITING_CONNECTION 2
#define STATE_CONNECTED 3
#define STATE_PUBLISHING 20
#define STATE_DISCONNECTED 4
/*---------------------------------------------------------------------------*/
/* Timing configurations */
#define WAITING_INTERVAL CLOCK_SECOND
#define DEFAULT_PUBLISH_INTERVAL (30 * CLOCK_SECOND)
/*---------------------------------------------------------------------------*/
/* LED configurations for various states */
#define MQTT_WAITING_FOR_NET_LED LEDS_BLUE
#define MQTT_CONNECTING_LED LEDS_BLUE
#define MQTT_PUBLISHING_LED LEDS_GREEN
#define MQTT_ERROR_LED LEDS_RED
/*---------------------------------------------------------------------------*/
PROCESS_NAME(mqtt_client_process);
AUTOSTART_PROCESSES(&mqtt_client_process);
/*---------------------------------------------------------------------------*/
/* Maximum TCP segment size for outgoing segments of our socket */
#define MAX_TCP_SEGMENT_SIZE 32
/*---------------------------------------------------------------------------*/
/* The main MQTT buffers */
#define APP_BUFFER_SIZE 512
static struct mqtt_connection conn;
static char app_buffer[APP_BUFFER_SIZE];
/*---------------------------------------------------------------------------*/
/* MQTT client ID */
#define CLIENT_ID_SIZE 25
char client_id[CLIENT_ID_SIZE];
/*---------------------------------------------------------------------------*/
static struct etimer periodic_timer;
static struct ctimer ct;
/*---------------------------------------------------------------------------*/
PROCESS(mqtt_client_process, "MQTT Client");
/*---------------------------------------------------------------------------*/
/**
 * Turns off all LEDs.
 * This is used as a callback function for the ctimer.
 */
static void status_led_off(void *d) {
  leds_off(LEDS_ALL);
}
/*---------------------------------------------------------------------------*/
/**
 * Constructs a unique MQTT client ID based on the device's MAC address.
 * The client ID is stored in the `client_id` global variable.
 */
static void construct_client_id() {
  uint8_t mac_address[8];
  linkaddr_copy((linkaddr_t *)mac_address, &linkaddr_node_addr);
  snprintf(client_id, CLIENT_ID_SIZE, "contiki-%02x%02x%02x%02x%02x%02x%02x%02x",
           mac_address[0], mac_address[1], mac_address[2], mac_address[3],
           mac_address[4], mac_address[5], mac_address[6], mac_address[7]);
}
/*---------------------------------------------------------------------------*/
/**
 * MQTT Event Handler.
 * Handles various MQTT events like connection, disconnection, and publishing acknowledgments.
 * This function is called as a callback from the MQTT library when an event occurs.
 * 
 * @param m      MQTT connection instance.
 * @param event  The type of MQTT event.
 * @param data   Additional event-specific data.
 */
static void mqtt_event(struct mqtt_connection *m, mqtt_event_t event, void *data) {
  switch (event)
  {
  case MQTT_EVENT_CONNECTED:
  {
    LOG_INFO("Device is connected to broker %s on port %u with client ID %s\n",
            MQTT_BROKER_IP_ADDR, MQTT_BROKER_PORT, client_id);
    state = STATE_CONNECTED;
    break;
  }
  case MQTT_EVENT_DISCONNECTED:
  {
    LOG_ERR("MQTT Disconnect. Reason %u\n", *((mqtt_event_t *)data));
    state = STATE_DISCONNECTED;
    break;
  }
  case MQTT_EVENT_CONNECTION_REFUSED_ERROR:
  {
    LOG_ERR("MQTT Disconnect. Reason %u (CONNECTION_REFUSED)\n", *((mqtt_event_t *)data));
    state = STATE_DISCONNECTED;
    break;
  }
  case MQTT_EVENT_PUBACK:
  {
    LOG_DBG("Publishing complete.\n");
    break;
  }
  default:
    LOG_DBG("Application got a unhandled MQTT event: %i\n", event);
    break;
  }
}
/*---------------------------------------------------------------------------*/
/**
 * Publishes data to a specified MQTT topic.
 * 
 * @param topic The MQTT topic to publish to.
 * @param data  The data to publish.
 */
static void publish(char *topic, char *data) {
  snprintf(app_buffer, APP_BUFFER_SIZE, "%s", data);
  mqtt_publish(&conn, NULL, topic, (uint8_t *)app_buffer,
               strlen(app_buffer), MQTT_QOS_LEVEL_0, MQTT_RETAIN_OFF);
  LOG_INFO("Publish on topic \"%s\"\n", topic);
}
/*---------------------------------------------------------------------------*/
/**
 * Process of the MQTT client.
 * Handles the lifecycle of the MQTT client, including initialization, connection, and publishing.
 * 
 * The usual workflow is:
 *  - STATE_INIT: Initialize the MQTT connection.
 *  - STATE_WAITING_CONNECTIVITY: Wait for a global IPv6 address.
 *  - STATE_WAITING_CONNECTION: Wait for the broker to acknowledge the connection.
 *  - STATE_CONNECTED: Check if the MQTT connection is ready to publish.
 *  - STATE_PUBLISHING: Publish data to the broker. Once this state is attained, it loops in it.
 * 
 *  - STATE_DISCONNECTED: If the MQTT connection is lost, it goes back to STATE_INIT to reconnect.
 * 
 * Once each state is processed, the process yields control back to the system and program the timer to fire
 * in WAITING_INTERVAL seconds or DEFAULT_PUBLISH_INTERVAL seconds based on the state.
 * 
 */
PROCESS_THREAD(mqtt_client_process, ev, data) {

  PROCESS_BEGIN();

  etimer_set(&periodic_timer, 0);

  construct_client_id();
  state = STATE_INIT;

  while (1) {
    PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER && data == &periodic_timer);

    switch (state) {
    case STATE_INIT: /* Initialize the MQTT connection */
      mqtt_register(&conn, &mqtt_client_process, client_id, mqtt_event, MAX_TCP_SEGMENT_SIZE);
      LOG_DBG("Init MQTT version %d\n", MQTT_PROTOCOL_VERSION);
      state = STATE_WAITING_CONNECTIVITY;
      etimer_set(&periodic_timer, WAITING_INTERVAL);
      break;
    case STATE_WAITING_CONNECTIVITY: /* Waiting for a global address */
      if (uip_ds6_get_global(ADDR_PREFERRED) != NULL) {
        LOG_INFO("Device got a IPv6 address ");
        LOG_INFO_6ADDR(&uip_ds6_get_global(ADDR_PREFERRED)->ipaddr);
        LOG_INFO_("\n");
        LOG_DBG("Device is registered, connecting...\n");
        mqtt_connect(&conn, MQTT_BROKER_IP_ADDR, MQTT_BROKER_PORT,
                     (DEFAULT_PUBLISH_INTERVAL * 3) / CLOCK_SECOND, MQTT_CLEAN_SESSION_ON);

        state = STATE_WAITING_CONNECTION;
      } else {
        LOG_DBG("No connectivity yet, waiting...\n");
        leds_on(MQTT_WAITING_FOR_NET_LED);
        ctimer_set(&ct, WAITING_INTERVAL >> 1, status_led_off, NULL);
      }
      etimer_set(&periodic_timer, WAITING_INTERVAL);
      break;
    case STATE_WAITING_CONNECTION: /* Waiting for the broker to ack our connect */
      LOG_DBG("Waiting for connection...\n");
      leds_on(MQTT_CONNECTING_LED);
      ctimer_set(&ct, WAITING_INTERVAL >> 1, status_led_off, NULL);
      etimer_set(&periodic_timer, WAITING_INTERVAL);
      break;
    case STATE_CONNECTED: /* Connected, publish */
      if (mqtt_ready(&conn) && conn.out_buffer_sent) {
        leds_on(MQTT_PUBLISHING_LED);
        ctimer_set(&ct, WAITING_INTERVAL, status_led_off, NULL);
        LOG_DBG("Connected !\n");
        state = STATE_PUBLISHING;
      } else {
        /*
         * Our publish timer fired, but some MQTT packet is already in flight
         * (either not sent at all, or sent but not fully ACKd).
         */
        LOG_DBG("Publishing... (MQTT state=%d, q=%u)\n", conn.state,
                conn.out_queue_full);
      }
      etimer_set(&periodic_timer, WAITING_INTERVAL);
      break;
    case STATE_PUBLISHING: /* Publishing */
      leds_on(MQTT_PUBLISHING_LED);
      ctimer_set(&ct, WAITING_INTERVAL, status_led_off, NULL);
      LOG_DBG("Publishing\n");
      publish("/this/is/a/test/topic", "Hello World");
      etimer_set(&periodic_timer, DEFAULT_PUBLISH_INTERVAL);
      break;
    case STATE_DISCONNECTED: /* Disconnected */
      leds_on(MQTT_ERROR_LED);
      ctimer_set(&ct, WAITING_INTERVAL >> 1, status_led_off, NULL);
      state = STATE_INIT; /* Reconnect */
      etimer_set(&periodic_timer, WAITING_INTERVAL);
      break;
    default: /* should never happen */
      leds_on(MQTT_ERROR_LED);
      LOG_ERR("Default case: State=0x%02x\n", state);
      ctimer_set(&ct, WAITING_INTERVAL >> 1, status_led_off, NULL);
      etimer_set(&periodic_timer, WAITING_INTERVAL);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
