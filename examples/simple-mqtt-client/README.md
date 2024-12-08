## Simple MQTT Client

### Overview
- **`mqtt-publish.c`**: Implements an MQTT client that connects to a broker and publishes messages to a specified topic.
- **`mqtt-subscribe.c`**: Implements an MQTT client that connects to a broker, subscribes to multiple topics and processes incoming messages.

### Demonstration

- **Publishing**: Compile and flash `mqtt-publish.c` onto a node. By default, it publishes the message `"Hello World"` to the topic `/this/is/a/test/topic` on the broker `[fd00::2]:1883`. You can customize the broker address and port in `project-conf.h` and modify the topic and message on **line 220** of `mqtt-publish.c`.
- **Subscribing**: Compile and flash `mqtt-subscribe.c` onto a node. By default, it subscribes to topics `/this/is/a/test/topic`, `/this/is/another/topic` and `/this/is/a/third/topic` on the broker `[fd00::2]:1883`. Upon receiving a message, it logs the topic and message content. To customize, edit the broker settings in `project-conf.h` and update the topics array on **lines 59-60** in `mqtt-subscribe.c`.