/*
 * Copyright (C) 2014 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at
 * contact@bluekitchen-gmbh.com
 *
 */

#define BTSTACK_FILE__ "hci_transport_h4.c"

/*
 *  hci_h4_transport.c
 *
 *  HCI Transport API implementation for H4 protocol over POSIX with optional support for eHCILL
 *
 *  Created by Matthias Ringwald on 4/29/09.
 */

#include <inttypes.h>

#include "btstack_config.h"
#include "btstack_state.h"

#include "btstack_debug.h"
#include "hci.h"
#include "hci_transport.h"
#include "bluetooth_company_id.h"
#include "btstack_uart_block.h"
#include "log.h"

#define ENABLE_LOG_EHCILL

#ifdef ENABLE_EHCILL

// eHCILL commands
enum EHCILL_MESSAGES {
	EHCILL_GO_TO_SLEEP_IND = 0x030,
	EHCILL_GO_TO_SLEEP_ACK = 0x031,
	EHCILL_WAKE_UP_IND     = 0x032,
	EHCILL_WAKE_UP_ACK     = 0x033,
	EHCILL_WAKEUP_SIGNAL   = 0x034,
};

static int  hci_transport_h4_ehcill_outgoing_packet_ready(void);
static void hci_transport_h4_echill_send_wakeup_ind(void);
static void hci_transport_h4_ehcill_handle_command(uint8_t action);
static void hci_transport_h4_ehcill_handle_ehcill_command_sent(void);
static void hci_transport_h4_ehcill_handle_packet_sent(void);
static void hci_transport_h4_ehcill_open(void);
static void hci_transport_h4_ehcill_reset_statemachine(void);
static void hci_transport_h4_ehcill_send_ehcill_command(void);
static void hci_transport_h4_ehcill_sleep_ack_timer_setup(void);
static void hci_transport_h4_ehcill_trigger_wakeup(void);

typedef enum {
    EHCILL_STATE_W2_SEND_SLEEP_ACK,
    EHCILL_STATE_SLEEP,
    EHCILL_STATE_W4_WAKEUP_IND_OR_ACK,
    EHCILL_STATE_AWAKE
} EHCILL_STATE;

// eHCILL state machine
static EHCILL_STATE ehcill_state;
static uint8_t      ehcill_command_to_send;

static btstack_uart_sleep_mode_t btstack_uart_sleep_mode;

// work around for eHCILL problem
static btstack_timer_source_t ehcill_sleep_ack_timer;

#endif


// assert pre-buffer for packet type is available
#if !defined(HCI_OUTGOING_PRE_BUFFER_SIZE) || (HCI_OUTGOING_PRE_BUFFER_SIZE == 0)
#error HCI_OUTGOING_PRE_BUFFER_SIZE not defined. Please update hci.h
#endif

typedef enum {
    H4_OFF,
    H4_W4_PACKET_TYPE,
    H4_W4_EVENT_HEADER,
    H4_W4_ACL_HEADER,
    H4_W4_SCO_HEADER,
    H4_W4_PAYLOAD,
} H4_STATE;

typedef enum {
    TX_OFF,
    TX_IDLE,
    TX_W4_PACKET_SENT,
#ifdef ENABLE_EHCILL
    TX_W4_WAKEUP,
    TX_W2_EHCILL_SEND,
    TX_W4_EHCILL_SENT,
#endif
} TX_STATE;

struct btstack_hci_h4_state {
    // UART Driver + Config
    const btstack_uart_block_t * btstack_uart;
    btstack_uart_config_t uart_config;

    // write state
    TX_STATE tx_state;
#ifdef ENABLE_EHCILL
    uint8_t * ehcill_tx_data;
    uint16_t  ehcill_tx_len;   // 0 == no outgoing packet
#endif

    void (*packet_handler)(btstack_state_t *btstack, uint8_t packet_type, uint8_t *packet, uint16_t size);

    // packet reader state machine
    H4_STATE h4_state;
    uint16_t bytes_to_read;
    uint16_t read_pos;

    // incoming packet buffer
    uint8_t hci_packet_with_pre_buffer[HCI_INCOMING_PRE_BUFFER_SIZE + HCI_INCOMING_PACKET_BUFFER_SIZE + 1]; // packet type + max(acl header + acl payload, event header + event data)
    uint8_t * hci_packet;
};

// Baudrate change bugs in TI CC256x and CYW20704
#ifdef ENABLE_CC256X_BAUDRATE_CHANGE_FLOWCONTROL_BUG_WORKAROUND
#define ENABLE_BAUDRATE_CHANGE_FLOWCONTROL_BUG_WORKAROUND
static const uint8_t baud_rate_command_prefix[]   = { 0x01, 0x36, 0xff, 0x04};
#endif

#ifdef ENABLE_CYPRESS_BAUDRATE_CHANGE_FLOWCONTROL_BUG_WORKAROUND
#ifdef ENABLE_BAUDRATE_CHANGE_FLOWCONTROL_BUG_WORKAROUND
#error "Please enable either ENABLE_CC256X_BAUDRATE_CHANGE_FLOWCONTROL_BUG_WORKAROUND or ENABLE_BAUDRATE_CHANGE_FLOWCONTROL_BUG_WORKAROUND"
#endif
#define ENABLE_BAUDRATE_CHANGE_FLOWCONTROL_BUG_WORKAROUND
static const uint8_t baud_rate_command_prefix[]   = { 0x01, 0x18, 0xfc, 0x06};
#endif

#ifdef ENABLE_BAUDRATE_CHANGE_FLOWCONTROL_BUG_WORKAROUND
static const uint8_t local_version_event_prefix[] = { 0x04, 0x0e, 0x0c, 0x01, 0x01, 0x10};
static enum {
    BAUDRATE_CHANGE_WORKAROUND_IDLE,
    BAUDRATE_CHANGE_WORKAROUND_CHIPSET_DETECTED,
    BAUDRATE_CHANGE_WORKAROUND_BAUDRATE_COMMAND_SENT,
    BAUDRATE_CHANGE_WORKAROUND_DONE
} baudrate_change_workaround_state;
#endif

static int hci_transport_h4_set_baudrate(btstack_state_t *btstack, uint32_t baudrate){
    log_info("hci_transport_h4_set_baudrate %"PRIu32, baudrate);
    return btstack->hci_h4->btstack_uart->set_baudrate(btstack, baudrate);
}

static void hci_transport_h4_reset_statemachine(btstack_state_t *btstack){
    btstack->hci_h4->h4_state = H4_W4_PACKET_TYPE;
    btstack->hci_h4->read_pos = 0;
    btstack->hci_h4->bytes_to_read = 1;
}

static void hci_transport_h4_trigger_next_read(btstack_state_t *btstack){
    log_info("hci_transport_h4_trigger_next_read: %u bytes", btstack->hci_h4->bytes_to_read);
    LH(__func__, __LINE__, btstack->hci_h4->bytes_to_read);
    btstack->hci_h4->btstack_uart->receive_block(btstack, &btstack->hci_h4->hci_packet[btstack->hci_h4->read_pos], btstack->hci_h4->bytes_to_read);
}

static void hci_transport_h4_packet_complete(btstack_state_t *btstack){
#ifdef ENABLE_BAUDRATE_CHANGE_FLOWCONTROL_BUG_WORKAROUND
    if (baudrate_change_workaround_state == BAUDRATE_CHANGE_WORKAROUND_IDLE
            && memcmp(hci_packet, local_version_event_prefix, sizeof(local_version_event_prefix)) == 0){
#ifdef ENABLE_CC256X_BAUDRATE_CHANGE_FLOWCONTROL_BUG_WORKAROUND
                if (little_endian_read_16(hci_packet, 11) == BLUETOOTH_COMPANY_ID_TEXAS_INSTRUMENTS_INC){
                    // detect TI CC256x controller based on manufacturer
                    log_info("Detected CC256x controller");
                    baudrate_change_workaround_state = BAUDRATE_CHANGE_WORKAROUND_CHIPSET_DETECTED;
                } else {
                    // work around not needed
                    log_info("Bluetooth controller not by TI");
                    baudrate_change_workaround_state = BAUDRATE_CHANGE_WORKAROUND_DONE;
                }
#endif
#ifdef ENABLE_CYPRESS_BAUDRATE_CHANGE_FLOWCONTROL_BUG_WORKAROUND
                if (little_endian_read_16(hci_packet, 11) == BLUETOOTH_COMPANY_ID_CYPRESS_SEMICONDUCTOR){
                    // detect Cypress controller based on manufacturer
                    log_info("Detected Cypress controller");
                    baudrate_change_workaround_state = BAUDRATE_CHANGE_WORKAROUND_CHIPSET_DETECTED;
                } else {
                    // work around not needed
                    log_info("Bluetooth controller not by Cypress");
                    baudrate_change_workaround_state = BAUDRATE_CHANGE_WORKAROUND_DONE;
                }
#endif
            }
#endif
    uint16_t packet_len = btstack->hci_h4->read_pos-1;

    // reset state machine before delivering packet to stack as it might close the transport
    hci_transport_h4_reset_statemachine(btstack);
    btstack->hci_h4->packet_handler(btstack, btstack->hci_h4->hci_packet[0], &btstack->hci_h4->hci_packet[1], packet_len);
}

static void hci_transport_h4_block_read(btstack_state_t *btstack){

    btstack->hci_h4->read_pos += btstack->hci_h4->bytes_to_read;

    switch (btstack->hci_h4->h4_state) {
        case H4_W4_PACKET_TYPE:
            switch (btstack->hci_h4->hci_packet[0]){
                case HCI_EVENT_PACKET:
                    btstack->hci_h4->bytes_to_read = HCI_EVENT_HEADER_SIZE;
                    btstack->hci_h4->h4_state = H4_W4_EVENT_HEADER;
                    break;
                case HCI_ACL_DATA_PACKET:
                    btstack->hci_h4->bytes_to_read = HCI_ACL_HEADER_SIZE;
                    btstack->hci_h4->h4_state = H4_W4_ACL_HEADER;
                    break;
                case HCI_SCO_DATA_PACKET:
                    btstack->hci_h4->bytes_to_read = HCI_SCO_HEADER_SIZE;
                    btstack->hci_h4->h4_state = H4_W4_SCO_HEADER;
                    break;
#ifdef ENABLE_EHCILL
                case EHCILL_GO_TO_SLEEP_IND:
                case EHCILL_GO_TO_SLEEP_ACK:
                case EHCILL_WAKE_UP_IND:
                case EHCILL_WAKE_UP_ACK:
                    hci_transport_h4_ehcill_handle_command(hci_packet[0]);
                    hci_transport_h4_reset_statemachine();
                    break;
#endif
                default:
                    log_error("hci_transport_h4: invalid packet type 0x%02x", btstack->hci_h4->hci_packet[0]);
                    hci_transport_h4_reset_statemachine(btstack);
                    break;
            }
            break;

        case H4_W4_EVENT_HEADER:
            btstack->hci_h4->bytes_to_read = btstack->hci_h4->hci_packet[2];
            // check Event length
            if (btstack->hci_h4->bytes_to_read > (HCI_INCOMING_PACKET_BUFFER_SIZE - HCI_EVENT_HEADER_SIZE)){
                log_error("hci_transport_h4: invalid Event len %d - only space for %u", btstack->hci_h4->bytes_to_read, HCI_INCOMING_PACKET_BUFFER_SIZE - HCI_EVENT_HEADER_SIZE);
                hci_transport_h4_reset_statemachine(btstack);
                break;
            }
            btstack->hci_h4->h4_state = H4_W4_PAYLOAD;
            break;

        case H4_W4_ACL_HEADER:
            btstack->hci_h4->bytes_to_read = little_endian_read_16(btstack->hci_h4->hci_packet, 3);
            // check ACL length
            if (btstack->hci_h4->bytes_to_read > (HCI_INCOMING_PACKET_BUFFER_SIZE - HCI_ACL_HEADER_SIZE)){
                log_error("hci_transport_h4: invalid ACL payload len %d - only space for %u", btstack->hci_h4->bytes_to_read, HCI_INCOMING_PACKET_BUFFER_SIZE - HCI_ACL_HEADER_SIZE);
                hci_transport_h4_reset_statemachine(btstack);
                break;
            }
            btstack->hci_h4->h4_state = H4_W4_PAYLOAD;
            break;

        case H4_W4_SCO_HEADER:
            btstack->hci_h4->bytes_to_read = btstack->hci_h4->hci_packet[3];
            // check SCO length
            if (btstack->hci_h4->bytes_to_read > (HCI_INCOMING_PACKET_BUFFER_SIZE - HCI_SCO_HEADER_SIZE)){
                log_error("hci_transport_h4: invalid SCO payload len %d - only space for %u", btstack->hci_h4->bytes_to_read, HCI_INCOMING_PACKET_BUFFER_SIZE - HCI_SCO_HEADER_SIZE);
                hci_transport_h4_reset_statemachine(btstack);
                break;
            }
            btstack->hci_h4->h4_state = H4_W4_PAYLOAD;
            break;

        case H4_W4_PAYLOAD:
            hci_transport_h4_packet_complete(btstack);
            break;

        case H4_OFF:
            btstack->hci_h4->bytes_to_read = 0;
            break;
    }

#ifdef ENABLE_BAUDRATE_CHANGE_FLOWCONTROL_BUG_WORKAROUND
    if (baudrate_change_workaround_state == BAUDRATE_CHANGE_WORKAROUND_BAUDRATE_COMMAND_SENT){
        baudrate_change_workaround_state = BAUDRATE_CHANGE_WORKAROUND_IDLE;
        // avoid flowcontrol problem by reading expected hci command complete event of 7 bytes in a single block read
        btstack->hci_h4->h4_state = H4_W4_PAYLOAD;
        btstack->hci_h4->bytes_to_read = 7;
    }
#endif

    // forward packet if payload size == 0
    if (btstack->hci_h4->h4_state == H4_W4_PAYLOAD && btstack->hci_h4->bytes_to_read == 0) {
        hci_transport_h4_packet_complete(btstack);
    }

    if (btstack->hci_h4->h4_state != H4_OFF) {
        hci_transport_h4_trigger_next_read(btstack);
    }
}

static void hci_transport_h4_block_sent(btstack_state_t *btstack){

    static const uint8_t packet_sent_event[] = { HCI_EVENT_TRANSPORT_PACKET_SENT, 0};

    switch (btstack->hci_h4->tx_state){
        case TX_W4_PACKET_SENT:
            // packet fully sent, reset state
#ifdef ENABLE_EHCILL
            ehcill_tx_len = 0;
#endif
            btstack->hci_h4->tx_state = TX_IDLE;

#ifdef ENABLE_EHCILL
            // notify eHCILL engine
            hci_transport_h4_ehcill_handle_packet_sent();
#endif
            // notify upper stack that it can send again
            btstack->hci_h4->packet_handler(btstack, HCI_EVENT_PACKET, (uint8_t *) &packet_sent_event[0], sizeof(packet_sent_event));
            break;

#ifdef ENABLE_EHCILL
        case TX_W4_EHCILL_SENT:
        case TX_W4_WAKEUP:
            hci_transport_h4_ehcill_handle_ehcill_command_sent();
            break;
#endif

        default:
            break;
    }
}

static int hci_transport_h4_can_send_now(btstack_state_t *btstack, uint8_t packet_type){
    UNUSED(packet_type);
    return btstack->hci_h4->tx_state == TX_IDLE;
}

static int hci_transport_h4_send_packet(btstack_state_t *btstack, uint8_t packet_type, uint8_t * packet, int size){

    // store packet type before actual data and increase size
    size++;
    packet--;
    *packet = packet_type;

#ifdef ENABLE_BAUDRATE_CHANGE_FLOWCONTROL_BUG_WORKAROUND
    if ((baudrate_change_workaround_state == BAUDRATE_CHANGE_WORKAROUND_CHIPSET_DETECTED)
    && (memcmp(packet, baud_rate_command_prefix, sizeof(baud_rate_command_prefix)) == 0)) {
        log_info("Baud rate command detected, expect command complete event next");
        baudrate_change_workaround_state = BAUDRATE_CHANGE_WORKAROUND_BAUDRATE_COMMAND_SENT;
    }
#endif

#ifdef ENABLE_EHCILL
    // store request for later
    ehcill_tx_len   = size;
    ehcill_tx_data  = packet;
    switch (ehcill_state){
        case EHCILL_STATE_SLEEP:
            hci_transport_h4_ehcill_trigger_wakeup();
            return 0;
        case EHCILL_STATE_W2_SEND_SLEEP_ACK:
            log_info("eHILL: send next packet, state EHCILL_STATE_W2_SEND_SLEEP_ACK");
            return 0;
        default:
            break;
    }
#endif

    // start sending
    btstack->hci_h4->tx_state = TX_W4_PACKET_SENT;
    btstack->hci_h4->btstack_uart->send_block(btstack, packet, size);
    return 0;
}

static void hci_transport_h4_init(btstack_state_t *btstack, const void * transport_config){
    // check for hci_transport_config_uart_t
    nwt_log("hci_transport_h4_init");
    if (!transport_config) {
        log_error("hci_transport_h4: no config!");
        return;
    }
    if (((hci_transport_config_t*)transport_config)->type != HCI_TRANSPORT_CONFIG_UART) {
        log_error("hci_transport_h4: config not of type != HCI_TRANSPORT_CONFIG_UART!");
        return;
    }

    // extract UART config from transport config
    hci_transport_config_uart_t * hci_transport_config_uart = (hci_transport_config_uart_t*) transport_config;
    btstack->hci_h4->uart_config.baudrate    = hci_transport_config_uart->baudrate_init;
    btstack->hci_h4->uart_config.flowcontrol = hci_transport_config_uart->flowcontrol;
    btstack->hci_h4->uart_config.device_name = hci_transport_config_uart->device_name;

    // set state to off
    btstack->hci_h4->tx_state = TX_OFF;
    btstack->hci_h4->h4_state = H4_OFF;

    // setup UART driver
    btstack->hci_h4->btstack_uart->init(btstack, &btstack->hci_h4->uart_config);
    btstack->hci_h4->btstack_uart->set_block_received(btstack, &hci_transport_h4_block_read);
    btstack->hci_h4->btstack_uart->set_block_sent(btstack, &hci_transport_h4_block_sent);
    btstack->hci_h4->hci_packet = &btstack->hci_h4->hci_packet_with_pre_buffer[HCI_INCOMING_PRE_BUFFER_SIZE];
}

static int hci_transport_h4_open(btstack_state_t *btstack){
    // open uart driver
    LH(__func__, __LINE__, 0);
    int res = btstack->hci_h4->btstack_uart->open(btstack);
    if (res){
        return res;
    }

    // init rx + tx state machines
    btstack->hci_h4->tx_state = TX_IDLE;
    hci_transport_h4_reset_statemachine(btstack);
    hci_transport_h4_trigger_next_read(btstack);

#ifdef ENABLE_EHCILL
    hci_transport_h4_ehcill_open();
#endif
    return 0;
}

static int hci_transport_h4_close(btstack_state_t *btstack){
    // set state to off
    btstack->hci_h4->tx_state = TX_OFF;
    btstack->hci_h4->h4_state = H4_OFF;

    // close uart driver
    return btstack->hci_h4->btstack_uart->close(btstack);
}

static void hci_transport_h4_register_packet_handler(btstack_state_t *btstack, void (*handler)(btstack_state_t *btstack, uint8_t packet_type, uint8_t *packet, uint16_t size)){
    btstack->hci_h4->packet_handler = handler;
}

//
// --- main part of eHCILL implementation ---
//

#ifdef ENABLE_EHCILL

static void hci_transport_h4_ehcill_emit_sleep_state(int sleep_active){
    static int last_state = 0;
    if (sleep_active == last_state) return;
    last_state = sleep_active;

    log_info("hci_transport_h4_ehcill_emit_sleep_state: %u", sleep_active);
    uint8_t event[3];
    event[0] = HCI_EVENT_TRANSPORT_SLEEP_MODE;
    event[1] = sizeof(event) - 2;
    event[2] = sleep_active;
    packet_handler(HCI_EVENT_PACKET, &event[0], sizeof(event));
}

static void hci_transport_h4_ehcill_wakeup_handler(void){
#ifdef ENABLE_LOG_EHCILL
    log_info("eHCILL: UART wakeup received");
#endif
    hci_transport_h4_ehcill_handle_command(EHCILL_WAKEUP_SIGNAL);
}

static void hci_transport_h4_ehcill_open(void){
    hci_transport_h4_ehcill_reset_statemachine();

    // find best sleep mode to use: wake on CTS, wake on RX, none
    btstack_uart_sleep_mode = BTSTACK_UART_SLEEP_OFF;
    int supported_sleep_modes = 0;
    if (btstack_uart->get_supported_sleep_modes){
        supported_sleep_modes = btstack_uart->get_supported_sleep_modes();
    }
    if (supported_sleep_modes & BTSTACK_UART_SLEEP_MASK_RTS_HIGH_WAKE_ON_CTS_PULSE){
        log_info("eHCILL: using wake on CTS");
        btstack_uart_sleep_mode = BTSTACK_UART_SLEEP_RTS_HIGH_WAKE_ON_CTS_PULSE;
    } else if (supported_sleep_modes & BTSTACK_UART_SLEEP_MASK_RTS_LOW_WAKE_ON_RX_EDGE){
        log_info("eHCILL: using wake on RX");
        btstack_uart_sleep_mode = BTSTACK_UART_SLEEP_RTS_LOW_WAKE_ON_RX_EDGE;
    } else {
        log_info("eHCILL: UART driver does not provide compatible sleep mode");
    }
    if (btstack_uart->set_wakeup_handler){
        btstack_uart->set_wakeup_handler(&hci_transport_h4_ehcill_wakeup_handler);
    }
}

static void hci_transport_h4_echill_send_wakeup_ind(void){
#ifdef ENABLE_LOG_EHCILL
    log_info("eHCILL: send WAKEUP_IND");
#endif
    // update state
    tx_state     = TX_W4_WAKEUP;
    ehcill_state = EHCILL_STATE_W4_WAKEUP_IND_OR_ACK;
    ehcill_command_to_send = EHCILL_WAKE_UP_IND;
    btstack_uart->send_block(&ehcill_command_to_send, 1);
}

static int hci_transport_h4_ehcill_outgoing_packet_ready(void){
    return ehcill_tx_len != 0;
}

static void hci_transport_h4_ehcill_reset_statemachine(void){
    ehcill_state = EHCILL_STATE_AWAKE;
}

static void hci_transport_h4_ehcill_send_ehcill_command(void){
#ifdef ENABLE_LOG_EHCILL
    log_info("eHCILL: send command %02x", ehcill_command_to_send);
#endif
    tx_state = TX_W4_EHCILL_SENT;
    if (ehcill_command_to_send == EHCILL_GO_TO_SLEEP_ACK){
        ehcill_state = EHCILL_STATE_SLEEP;
    }
    btstack_uart->send_block(&ehcill_command_to_send, 1);
}

static void hci_transport_h4_ehcill_sleep_ack_timer_handler(btstack_timer_source_t * timer){
	UNUSED(timer);
#ifdef ENABLE_LOG_EHCILL
    log_info("eHCILL: timer triggered");
#endif
    hci_transport_h4_ehcill_send_ehcill_command();
}

static void hci_transport_h4_ehcill_sleep_ack_timer_setup(void){
    // setup timer
#ifdef ENABLE_LOG_EHCILL
    log_info("eHCILL: set timer for sending command %02x", ehcill_command_to_send);
#endif
    btstack_run_loop_set_timer_handler(&ehcill_sleep_ack_timer, &hci_transport_h4_ehcill_sleep_ack_timer_handler);
    btstack_run_loop_set_timer(&ehcill_sleep_ack_timer, 50);
    btstack_run_loop_add_timer(&ehcill_sleep_ack_timer);
}

static void hci_transport_h4_ehcill_trigger_wakeup(void){
    switch (tx_state){
        case TX_W2_EHCILL_SEND:
        case TX_W4_EHCILL_SENT:
            // wake up / sleep ack in progress, nothing to do now
            return;
        case TX_IDLE:
        default:
            // all clear, prepare for wakeup
            break;
    }
    // UART needed again
    hci_transport_h4_ehcill_emit_sleep_state(0);
    if (btstack_uart_sleep_mode){
        btstack_uart->set_sleep(BTSTACK_UART_SLEEP_OFF);
    }
    hci_transport_h4_echill_send_wakeup_ind();
}

static void hci_transport_h4_ehcill_schedule_ehcill_command(uint8_t command){
#ifdef ENABLE_LOG_EHCILL
    log_info("eHCILL: schedule eHCILL command %02x", command);
#endif
    ehcill_command_to_send = command;
    switch (tx_state){
        case TX_IDLE:
            if (ehcill_command_to_send == EHCILL_WAKE_UP_ACK){
                // send right away
                hci_transport_h4_ehcill_send_ehcill_command();
            } else {
                // change state so BTstack cannot send and setup timer
                tx_state = TX_W2_EHCILL_SEND;
                hci_transport_h4_ehcill_sleep_ack_timer_setup();
            }
            break;
        default:
            break;
    }
}

static void hci_transport_h4_ehcill_handle_command(uint8_t action){
    // log_info("hci_transport_h4_ehcill_handle: %x, state %u, defer_rx %u", action, ehcill_state, ehcill_defer_rx_size);
    switch(ehcill_state){
        case EHCILL_STATE_AWAKE:
            switch(action){
                case EHCILL_GO_TO_SLEEP_IND:
                    ehcill_state = EHCILL_STATE_W2_SEND_SLEEP_ACK;
#ifdef ENABLE_LOG_EHCILL
                    log_info("eHCILL: Received GO_TO_SLEEP_IND RX");
#endif
                    hci_transport_h4_ehcill_schedule_ehcill_command(EHCILL_GO_TO_SLEEP_ACK);
                    break;
                default:
                    break;
            }
            break;

        case EHCILL_STATE_W2_SEND_SLEEP_ACK:
            switch(action){
                case EHCILL_WAKE_UP_IND:
                    ehcill_state = EHCILL_STATE_AWAKE;
                    hci_transport_h4_ehcill_emit_sleep_state(0);
                    if (btstack_uart_sleep_mode){
                        btstack_uart->set_sleep(BTSTACK_UART_SLEEP_OFF);
                    }
#ifdef ENABLE_LOG_EHCILL
                    log_info("eHCILL: Received WAKE_UP_IND RX");
#endif
                    hci_transport_h4_ehcill_schedule_ehcill_command(EHCILL_WAKE_UP_ACK);
                    break;

                default:
                    break;
            }
            break;

        case EHCILL_STATE_SLEEP:
            switch(action){
                case EHCILL_WAKEUP_SIGNAL:
                    hci_transport_h4_ehcill_emit_sleep_state(0);
                    if (btstack_uart_sleep_mode){
                        btstack_uart->set_sleep(BTSTACK_UART_SLEEP_OFF);
                    }
                    break;
                case EHCILL_WAKE_UP_IND:
                    ehcill_state = EHCILL_STATE_AWAKE;
                    hci_transport_h4_ehcill_emit_sleep_state(0);
                    if (btstack_uart_sleep_mode){
                        btstack_uart->set_sleep(BTSTACK_UART_SLEEP_OFF);
                    }
#ifdef ENABLE_LOG_EHCILL
                    log_info("eHCILL: Received WAKE_UP_IND RX");
#endif
                    hci_transport_h4_ehcill_schedule_ehcill_command(EHCILL_WAKE_UP_ACK);
                    break;

                default:
                    break;
            }
            break;

        case EHCILL_STATE_W4_WAKEUP_IND_OR_ACK:
            switch(action){
                case EHCILL_WAKE_UP_IND:
                case EHCILL_WAKE_UP_ACK:
#ifdef ENABLE_LOG_EHCILL
                    log_info("eHCILL: Received WAKE_UP (%02x)", action);
#endif
                    tx_state = TX_W4_PACKET_SENT;
                    ehcill_state = EHCILL_STATE_AWAKE;
                    btstack_uart->send_block(ehcill_tx_data, ehcill_tx_len);
                    break;
                default:
                    break;
            }
            break;
    }
}

static void hci_transport_h4_ehcill_handle_packet_sent(void){
#ifdef ENABLE_LOG_EHCILL
        log_info("eHCILL: handle packet sent, command to send %02x", ehcill_command_to_send);
#endif
    // now, send pending ehcill command if neccessary
    switch (ehcill_command_to_send){
        case EHCILL_GO_TO_SLEEP_ACK:
            hci_transport_h4_ehcill_sleep_ack_timer_setup();
            break;
        case EHCILL_WAKE_UP_IND:
            hci_transport_h4_ehcill_send_ehcill_command();
            break;
        default:
            break;
    }
}

static void hci_transport_h4_ehcill_handle_ehcill_command_sent(void){
    tx_state = TX_IDLE;
    int command = ehcill_command_to_send;
    ehcill_command_to_send = 0;

#ifdef ENABLE_LOG_EHCILL
        log_info("eHCILL: handle eHCILL sent, command was %02x", command);
#endif

    if (command == EHCILL_GO_TO_SLEEP_ACK) {
#ifdef ENABLE_LOG_EHCILL
        log_info("eHCILL: GO_TO_SLEEP_ACK sent, enter sleep mode");
#endif
        // UART not needed after EHCILL_GO_TO_SLEEP_ACK was sent
        if (btstack_uart_sleep_mode != BTSTACK_UART_SLEEP_OFF){
            btstack_uart->set_sleep(btstack_uart_sleep_mode);
        }
        hci_transport_h4_ehcill_emit_sleep_state(1);
    }
    // already packet ready? then start wakeup
    if (hci_transport_h4_ehcill_outgoing_packet_ready()){
        hci_transport_h4_ehcill_emit_sleep_state(0);
        if (btstack_uart_sleep_mode != BTSTACK_UART_SLEEP_OFF){
            btstack_uart->set_sleep(BTSTACK_UART_SLEEP_OFF);
        }
        if (command != EHCILL_WAKE_UP_IND){
            hci_transport_h4_echill_send_wakeup_ind();
        }
    }
}

#endif
// --- end of eHCILL implementation ---------


// configure and return h4 singleton
const hci_transport_t * hci_transport_h4_instance(btstack_state_t *btstack, const btstack_uart_block_t * uart_driver) {

    static const hci_transport_t hci_transport_h4 = {
            /* const char * name; */                                        "H4",
            /* void   (*init) (const void *transport_config); */            &hci_transport_h4_init,
            /* int    (*open)(void); */                                     &hci_transport_h4_open,
            /* int    (*close)(void); */                                    &hci_transport_h4_close,
            /* void   (*register_packet_handler)(void (*handler)(...); */   &hci_transport_h4_register_packet_handler,
            /* int    (*can_send_packet_now)(uint8_t packet_type); */       &hci_transport_h4_can_send_now,
            /* int    (*send_packet)(...); */                               &hci_transport_h4_send_packet,
            /* int    (*set_baudrate)(uint32_t baudrate); */                &hci_transport_h4_set_baudrate,
            /* void   (*reset_link)(void); */                               NULL,
            /* void   (*set_sco_config)(uint16_t voice_setting, int num_connections); */ NULL,
    };

    btstack->hci_h4 = calloc(1, sizeof(struct btstack_hci_h4_state));
    btstack->hci_h4->btstack_uart = uart_driver;
    return &hci_transport_h4;
}
