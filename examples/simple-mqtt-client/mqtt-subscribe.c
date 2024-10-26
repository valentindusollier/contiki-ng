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
#define LOG_MODULE "MQTT"
#ifdef MQTT_CLIENT_CONF_LOG_LEVEL
#define LOG_LEVEL MQTT_CLIENT_CONF_LOG_LEVEL
#else
#define LOG_LEVEL LOG_LEVEL_INFO
#endif
/*---------------------------------------------------------------------------*/
/* Various states */
static uint8_t state;
#define STATE_INIT 0
#define STATE_WAITING_CONNECTIVITY 1
#define STATE_WAITING_CONNECTION 2
#define STATE_CONNECTED 3
#define STATE_SUBSCRIBING 11
#define STATE_SUBSCRIBED 12
#define STATE_DISCONNECTED 4
/*---------------------------------------------------------------------------*/
/* Default configuration values */
#define WAITING_INTERVAL CLOCK_SECOND
#define SUBSCRIBING_INTERVAL (2 * CLOCK_SECOND)
#define DEFAULT_SUBSCRIBED_INTERVAL (30 * CLOCK_SECOND)
/*---------------------------------------------------------------------------*/
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
static struct mqtt_connection conn;
/*---------------------------------------------------------------------------*/
#define CLIENT_ID_SIZE 25
char client_id[CLIENT_ID_SIZE];
/*---------------------------------------------------------------------------*/
/* Topics to subscribe to as a list of string ["/test/topic/", "/topic2", "/topic3/led"] */
static char *topics[] = {"/test/topic/", "/topic2", "/topic3/led"};
static int n_topics = 3;
static int first_unsubscribed_topic;
/*---------------------------------------------------------------------------*/
static struct etimer periodic_timer;
static struct ctimer ct;
/*---------------------------------------------------------------------------*/
PROCESS(mqtt_client_process, "MQTT Client");
/*---------------------------------------------------------------------------*/
static void status_led_off(void *d) {
  leds_off(LEDS_ALL);
}
/*---------------------------------------------------------------------------*/
static void construct_client_id() {
  uint8_t mac_address[8];
  linkaddr_copy((linkaddr_t *)mac_address, &linkaddr_node_addr);
  snprintf(client_id, CLIENT_ID_SIZE, "contiki-%02x%02x%02x%02x%02x%02x%02x%02x",
           mac_address[0], mac_address[1], mac_address[2], mac_address[3],
           mac_address[4], mac_address[5], mac_address[6], mac_address[7]);
}
/*---------------------------------------------------------------------------*/
static void message_handler(const char *topic, const uint8_t *chunk, uint16_t chunk_len) {
  LOG_DBG("Pub Handler: topic='%s', chunk_len=%u, chunk='%s'\n", topic, chunk_len, chunk);
}
/*---------------------------------------------------------------------------*/
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
  case MQTT_EVENT_PUBLISH:
  {
    struct mqtt_message *msg_ptr = data;
    LOG_INFO("Received message on topic %s (%i bytes): '%.*s'\n", msg_ptr->topic, msg_ptr->payload_chunk_length, msg_ptr->payload_chunk_length, msg_ptr->payload_chunk);
    message_handler(msg_ptr->topic, msg_ptr->payload_chunk, msg_ptr->payload_chunk_length);
    break;
  }
  case MQTT_EVENT_SUBACK:
  {
    LOG_DBG("Subscribed.\n");
    break;
  }
  case MQTT_EVENT_UNSUBACK:
  {
    LOG_DBG("Unsubscribed.\n");
    break;
  }
  default:
    LOG_DBG("Application got a unhandled MQTT event: %i\n", event);
    break;
  }
}
/*---------------------------------------------------------------------------*/
static int subscribe(char *topic) {
  LOG_INFO("Subscribing to topic %s.\n", topic);
  mqtt_status_t status = mqtt_subscribe(&conn, NULL, topic, MQTT_QOS_LEVEL_0);
  if(status == MQTT_STATUS_OUT_QUEUE_FULL) {
    LOG_ERR("Tried to subscribe on topic %s but command queue was full!\n", topic);
    return 0;
  }
  return 1;
}
/*---------------------------------------------------------------------------*/
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
      first_unsubscribed_topic = 0;
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
                     (DEFAULT_SUBSCRIBED_INTERVAL * 3) / CLOCK_SECOND, MQTT_CLEAN_SESSION_ON);

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
    case STATE_CONNECTED: /* Connected, try to subscribe */
      if (mqtt_ready(&conn) && conn.out_buffer_sent) {
        leds_on(MQTT_PUBLISHING_LED);
        ctimer_set(&ct, WAITING_INTERVAL, status_led_off, NULL);
        LOG_DBG("Connected\n");
        state = STATE_SUBSCRIBING;
        etimer_set(&periodic_timer, WAITING_INTERVAL);
      } else {
        LOG_DBG("Broker not ready yet... (MQTT state=%d, q=%u)\n", conn.state,
                conn.out_queue_full);
        etimer_set(&periodic_timer, WAITING_INTERVAL);
      }
      break;
    case STATE_SUBSCRIBING: /* Subscribing to topics */
      if (subscribe(topics[first_unsubscribed_topic]) && ++first_unsubscribed_topic == n_topics) {
        state = STATE_SUBSCRIBED;
      }
      etimer_set(&periodic_timer, SUBSCRIBING_INTERVAL);
      break;
    case STATE_SUBSCRIBED: /* Subscribed, wait for incoming messages */
      LOG_DBG("Subscribed, waiting for incoming messages...\n");
      etimer_set(&periodic_timer, DEFAULT_SUBSCRIBED_INTERVAL);
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
