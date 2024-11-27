CONTIKI_PROJECT = temp-sensor
all: $(CONTIKI_PROJECT)

CONTIKI = ../../..

include $(CONTIKI)/Makefile.dir-variables
MODULES += $(CONTIKI_NG_APP_LAYER_DIR)/mqtt

MODULES += $(CONTIKI_NG_DRIVERS_SENSOR_DIR)/dht11

include $(CONTIKI)/Makefile.identify-target

MODULES_REL += arch/platform/$(TARGET)

PLATFORMS_ONLY = cc26x0-cc13x0 cc2538dk openmote zoul native simplelink cooja

include $(CONTIKI)/Makefile.include
