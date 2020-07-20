NCT_PROJECTS    ?= ../../../../NewtonDev/NCT_Projects

BTSTACK_ROOT ?= ../..

BTSTACKINCLUDES := -I. -I src -I firmware -I$(BTSTACK_ROOT)/src -I$(BTSTACK_ROOT)/platform/embedded -I$(BTSTACK_ROOT)/platform/newton

NEWTONINCLUDES  := -I$(NCT_PROJECTS)/DDKIncludes -I$(NCT_PROJECTS)/DDKIncludes/Bootstrap -I$(NCT_PROJECTS)/DDKIncludes/CLibrary -I$(NCT_PROJECTS)/DDKIncludes/CommAPI -I$(NCT_PROJECTS)/DDKIncludes/Communications -I$(NCT_PROJECTS)/DDKIncludes/Frames -I$(NCT_PROJECTS)/DDKIncludes/HAL -I$(NCT_PROJECTS)/DDKIncludes/Lantern -I$(NCT_PROJECTS)/DDKIncludes/OS600 -I$(NCT_PROJECTS)/DDKIncludes/Packages -I$(NCT_PROJECTS)/DDKIncludes/Power -I$(NCT_PROJECTS)/DDKIncludes/QD -I$(NCT_PROJECTS)/DDKIncludes/Toolbox -I$(NCT_PROJECTS)/DDKIncludes/UtilityClasses -I$(NCT_PROJECTS)/DDKIncludes/PCMCIA -I$(NCT_PROJECTS)/Includes -I$(NCT_PROJECTS)/Includes/CLibrary -I$(NCT_PROJECTS)/Includes/Frames -I$(NCT_PROJECTS)/Includes/Utilities -I$(NCT_PROJECTS)/LanternDriver_DDK/Includes -I$(NCT_PROJECTS)/NSandDDK

CC              := arm-none-eabi-gcc
CXX             := arm-none-eabi-g++
AS              := arm-none-eabi-as
LINKER          := arm-none-eabi-ld
OBJCOPY         := arm-none-eabi-objcopy
SETFILE         := echo
SIZE            := arm-none-eabi-size
NM              := arm-none-eabi-nm

MACHINEFLAGS    := -march=armv4 -mbig-endian
ASFLAGS         := ${MACHINEFLAGS}
CODEGENFLAGS    := -fPIC -fdata-sections -ffunction-sections -O2 -Werror=incompatible-pointer-types -fpermissive
DEFINES         := -D__arm -DhasPCMCIA -DforARM -DNO_ERRNO_H -DSTDC
CFLAGS          := ${MACHINEFLAGS} ${CODEGENFLAGS} ${DEFINES} $(BTSTACKINCLUDES)
CXXFLAGS        := ${MACHINEFLAGS} ${CODEGENFLAGS} ${DEFINES} $(BTSTACKINCLUDES) $(NEWTONINCLUDES) -fno-exceptions -fno-rtti

RUNTIMESRCDIR   := NewtonRuntime/

VPATH += $(RUNTIMESRCDIR)
VPATH += example
VPATH += firmware
VPATH += src
VPATH += NewtonRuntime
VPATH += $(BTSTACK_ROOT)/src
VPATH += $(BTSTACK_ROOT)/src/ble
VPATH += $(BTSTACK_ROOT)/src/classic
VPATH += $(BTSTACK_ROOT)/chipset/cc256x
VPATH += $(BTSTACK_ROOT)/platform/embedded
VPATH += $(BTSTACK_ROOT)/platform/newton
VPATH += $(BTSTACK_ROOT)/example

CORE   = \
    btstack_linked_list.c     \
    btstack_memory.c          \
    btstack_memory_pool.c       \
    btstack_run_loop.c		    \
    btstack_run_loop_newton.c \
    btstack_tlv_none.c             \
    main.c 					  \
    btstack_util.c			          \
	hal_newton.c \
	log.c

COMMON_C = \
	ad_parser.c                        \
	btstack_link_key_db_static.c       \
    btstack_uart_block_newton.c      \
    hci.c			          		   \
	hci_dump.c							\
    hci_cmd.c		          		   \
    hci_transport_h4.c                 \

COMMON_CPP = \
    hal_timer_newton.cpp            		   \
    hal_uart_newton.cpp            		   \

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

RUNTIMECXX  := NewtonExceptions.cpp
RUNTIMES  := NewtonPublicAPIs.s NewtonPrivateAPIs.s
	
BTSTACK = btstack.cpp

CORE_OBJ   = $(CORE:.c=.o)
COMMON_CPP_OBJ := $(COMMON_CPP:.cpp=.o)
COMMON_C_OBJ := $(COMMON_C:.c=.o)
COMMON_OBJ = $(COMMON_C_OBJ) $(COMMON_CPP_OBJ)
SPP_OBJ    = $(SPP:.c=.o)
SDPCLIENT_OBJ    = $(SDP_CLIENT:.c=.o)
BLE_OBJ    = $(BLE:.c=.o)
RUNTIME_OBJ = $(RUNTIMECXX:.cpp=.o) $(RUNTIMES:.s=.o)
BTSTACK_OBJ = $(BTSTACK:.cpp=.o)
GLUE_OBJ = glue.o

OBJS = $(BTSTACK_OBJ) ${CORE_OBJ} ${COMMON_OBJ} $(RUNTIME_OBJ) $(GLUE_OBJ)

all : btstack.ntkc

# NTKC rules

LDFLAGS   := -EB -shared --gc-sections --no-allow-shlib-undefined --no-export-dynamic -x --no-undefined -Tbtstack.ld --version-script=btstack.ver

btstack.ntkc : btstack.elf btstack.exp
	rm -f $@
	ELFtoNTK -via btstack.exp -o $@ btstack.elf
	$(SETFILE) -c NTP1 -t ntkc $@

btstack.elf : $(OBJS) 
	$(LINKER) $(LDFLAGS) $^ -o $@

%.o : %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o : %.s
	$(AS) $(ASFLAGS) -c $< -o $@

# compile GATT database
%.h: %.gatt
	python ${BTSTACK_ROOT}/tool/compile_gatt.py $< $@ 

clean:
	rm -f $(OBJS) bstack.elf btstack.ntkc

size: all
	arm-none-eabi-size *.o
	arm-none-eabi-size *.elf
