# Makefile for libusb based examples
BTSTACK_ROOT ?= ../..

# TI-WL183x requires TIInit_11.8.32.c

CFLAGS  += -g -Wall \
	-I. \
	-I$(BTSTACK_ROOT)/src \
	-I$(BTSTACK_ROOT)/platform/embedded \
	-I$(BTSTACK_ROOT)/platform/posix \
	-I$(BTSTACK_ROOT)/chipset/bcm \
	-I$(BTSTACK_ROOT)/chipset/cc256x \
	-I$(BTSTACK_ROOT)/chipset/csr \
	-I$(BTSTACK_ROOT)/chipset/em9301 \
	-I$(BTSTACK_ROOT)/chipset/stlc2500d \
	-I$(BTSTACK_ROOT)/chipset/tc3566x \
    -I${BTSTACK_ROOT}/3rd-party/rijndael \
    -I${BTSTACK_ROOT}/3rd-party/tinydir

VPATH += src
VPATH += ${BTSTACK_ROOT}/src
VPATH += ${BTSTACK_ROOT}/src/classic
VPATH += ${BTSTACK_ROOT}/3rd-party/rijndael

VPATH += ${BTSTACK_ROOT}/platform/posix
VPATH += ${BTSTACK_ROOT}/platform/embedded

VPATH += ${BTSTACK_ROOT}/chipset/bcm	
VPATH += ${BTSTACK_ROOT}/chipset/cc256x
VPATH += ${BTSTACK_ROOT}/chipset/csr
VPATH += ${BTSTACK_ROOT}/chipset/em9301
VPATH += ${BTSTACK_ROOT}/chipset/stlc2500d
VPATH += ${BTSTACK_ROOT}/chipset/tc3566x
VPATH += ${BTSTACK_ROOT}/example

EXAMPLES = ${EXAMPLES_GENERAL} ${EXAMPLES_CLASSIC_ONLY} ${EXAMPLES_LE_ONLY} ${EXAMPLES_DUAL_MODE}
EXAMPLES += pan_lwip_http_server

# use pkg-config for portaudio
# CFLAGS  += $(shell pkg-config portaudio-2.0 --cflags) -DHAVE_PORTAUDIO
# LDFLAGS += $(shell pkg-config portaudio-2.0 --libs)
# hard coded flags for portaudio in /usr/local/lib
# CFLAGS  += -I/usr/local/include -DHAVE_PORTAUDIO
# LDFLAGS += -L/sw/lib -lportaudio -Wl,-framework,CoreAudio -Wl,-framework,AudioToolbox -Wl,-framework,AudioUnit -Wl,-framework,Carbon

CORE   = \
    btstack_linked_list.c     \
    btstack_memory.c          \
    btstack_memory_pool.c       \
    btstack_run_loop.c		    \
    btstack_run_loop_embedded.c \
    btstack_tlv_none.c             \
    main.c 					  \
    btstack_util.c			          \
	log.c

COMMON = \
	ad_parser.c                        \
	btstack_link_key_db_static.c       \
    btstack_uart_block_embedded.c      \
    hal_uart_dma.c            		   \
    hci.c			          		   \
	hci_dump.c							\
    hci_cmd.c		          		   \
    hci_transport_h4.c                 \

SPP = \
    l2cap.c			          \
    l2cap_signaling.c         \
	rfcomm.c			      \
    sdp_util.c                \
    spp_server.c              \

SDP_CLIENT += \
    sdp_client.o		      \
	sdp_client_rfcomm.o		  \

BLE = \
	att_db.c 					  \
	att_server.c     		  \
	le_device_db_memory.c     \
	sm.c                      \
	att_dispatch.c            \
	l2cap.c 				  \
    l2cap_signaling.c         \
#	gatt_client.c             \

CORE_OBJ   = $(CORE:.c=.o)
COMMON_OBJ = $(COMMON:.c=.o)
SPP_OBJ    = $(SPP:.c=.o)
SDPCLIENT_OBJ    = $(SDP_CLIENT:.c=.o)
BLE_OBJ    = $(BLE:.c=.o)
all: btstack

btstack : ${CORE_OBJ} ${COMMON_OBJ}
	$(CC) $^ -o $@ 
