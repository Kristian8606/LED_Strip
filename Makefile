PROGRAM = main

EXTRA_COMPONENTS = \
	extras/http-parser \
	extras/rboot-ota \
	extras/dhcpserver \
	extras/i2s_dma \
	extras/ws2812_i2s \
	$(abspath ../../components/esp-8266/wifi_config) \
	$(abspath ../../components/esp-8266/cJSON) \
	$(abspath ../../components/common/wolfssl) \
	$(abspath ../../components/common/homekit)

ESPBAUD = 460800
FLASH_SIZE ?= 8
FLASH_MODE ?= dout
FLASH_SPEED ?= 40

HOMEKIT_SPI_FLASH_BASE_ADDR = 0x8c000
HOMEKIT_MAX_CLIENTS = 16
HOMEKIT_SMALL = 0
HOMEKIT_OVERCLOCK = 1
HOMEKIT_OVERCLOCK_PAIR_SETUP = 1
HOMEKIT_OVERCLOCK_PAIR_VERIFY = 1
EXTRA_CFLAGS += -I../.. -DHOMEKIT_SHORT_APPLE_UUIDS

include $(SDK_PATH)/common.mk

LIBS += m

monitor:
	$(FILTEROUTPUT) --port $(ESPPORT) --baud 115200 --elf $(PROGRAM_OUT)
