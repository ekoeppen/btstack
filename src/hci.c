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

#define BTSTACK_FILE__ "hci.c"

/*
 *  hci.c
 *
 *  Created by Matthias Ringwald on 4/29/09.
 *
 */

#include "btstack_config.h"


#ifdef ENABLE_CLASSIC
#ifdef HAVE_EMBEDDED_TICK
#include "btstack_run_loop_embedded.h"
#endif
#endif

#ifdef HAVE_PLATFORM_IPHONE_OS
#include "../port/ios/src/btstack_control_iphone.h"
#endif

#ifdef ENABLE_BLE
#include "gap.h"
#endif

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "btstack_debug.h"
#include "btstack_event.h"
#include "btstack_linked_list.h"
#include "btstack_memory.h"
#include "bluetooth_company_id.h"
#include "bluetooth_data_types.h"
#include "gap.h"
#include "hci.h"
#include "hci_cmd.h"
#include "hci_dump.h"
#include "ad_parser.h"
#include "log.h"

#ifdef ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#ifndef HCI_HOST_ACL_PACKET_NUM
#error "ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL requires to define HCI_HOST_ACL_PACKET_NUM"
#endif
#ifndef HCI_HOST_ACL_PACKET_LEN
#error "ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL requires to define HCI_HOST_ACL_PACKET_LEN"
#endif
#ifndef HCI_HOST_SCO_PACKET_NUM
#error "ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL requires to define HCI_HOST_SCO_PACKET_NUM"
#endif
#ifndef HCI_HOST_SCO_PACKET_LEN
#error "ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL requires to define HCI_HOST_SCO_PACKET_LEN"
#endif
#endif

#define HCI_CONNECTION_TIMEOUT_MS 10000

#ifndef HCI_RESET_RESEND_TIMEOUT_MS
#define HCI_RESET_RESEND_TIMEOUT_MS 2000
#endif

// Names are arbitrarily shortened to 32 bytes if not requested otherwise
#ifndef GAP_INQUIRY_MAX_NAME_LEN
#define GAP_INQUIRY_MAX_NAME_LEN 32
#endif

// GAP inquiry state: 0 = off, 0x01 - 0x30 = requested duration, 0xfe = active, 0xff = stop requested
#define GAP_INQUIRY_DURATION_MIN 0x01
#define GAP_INQUIRY_DURATION_MAX 0x30
#define GAP_INQUIRY_STATE_ACTIVE 0x80
#define GAP_INQUIRY_STATE_IDLE 0
#define GAP_INQUIRY_STATE_W2_CANCEL 0x81
#define GAP_INQUIRY_STATE_W4_CANCELLED 0x82

// GAP Remote Name Request
#define GAP_REMOTE_NAME_STATE_IDLE 0
#define GAP_REMOTE_NAME_STATE_W2_SEND 1
#define GAP_REMOTE_NAME_STATE_W4_COMPLETE 2

// GAP Pairing
#define GAP_PAIRING_STATE_IDLE                       0
#define GAP_PAIRING_STATE_SEND_PIN                   1
#define GAP_PAIRING_STATE_SEND_PIN_NEGATIVE          2
#define GAP_PAIRING_STATE_SEND_PASSKEY               3
#define GAP_PAIRING_STATE_SEND_PASSKEY_NEGATIVE      4
#define GAP_PAIRING_STATE_SEND_CONFIRMATION          5
#define GAP_PAIRING_STATE_SEND_CONFIRMATION_NEGATIVE 6


// prototypes
#ifdef ENABLE_CLASSIC
static void hci_update_scan_enable(btstack_state_t *btstack);
static void hci_emit_discoverable_enabled(btstack_state_t *btstack, uint8_t enabled);
static int  hci_local_ssp_activated(btstack_state_t *btstack);
static int  hci_remote_ssp_supported(btstack_state_t *btstack, hci_con_handle_t con_handle);
static void hci_notify_if_sco_can_send_now(btstack_state_t *btstack);
static void hci_emit_connection_complete(btstack_state_t *btstack, bd_addr_t address, hci_con_handle_t con_handle, uint8_t status);
static gap_security_level_t gap_security_level_for_connection(btstack_state_t *btstack, hci_connection_t * connection);
static void hci_emit_security_level(btstack_state_t *btstack, hci_con_handle_t con_handle, gap_security_level_t level);
static void hci_connection_timeout_handler(btstack_state_t *btstack, btstack_timer_source_t *timer);
static void hci_connection_timestamp(btstack_state_t *btstack, hci_connection_t *connection);
static void hci_emit_l2cap_check_timeout(btstack_state_t *btstack, hci_connection_t *conn);
static void gap_inquiry_explode(btstack_state_t *btstack, uint8_t *packet, uint16_t size);
#endif

static int  hci_power_control_on(btstack_state_t *btstack);
static void hci_power_control_off(btstack_state_t *btstack);
static void hci_state_reset(btstack_state_t *btstack);
static void hci_emit_transport_packet_sent(btstack_state_t *btstack);
static void hci_emit_disconnection_complete(btstack_state_t *btstack, hci_con_handle_t con_handle, uint8_t reason);
static void hci_emit_nr_connections_changed(btstack_state_t *btstack);
static void hci_emit_hci_open_failed(btstack_state_t *btstack);
static void hci_emit_dedicated_bonding_result(btstack_state_t *btstack, bd_addr_t address, uint8_t status);
static void hci_emit_event(btstack_state_t *btstack, uint8_t * event, uint16_t size, int dump);
static void hci_emit_acl_packet(btstack_state_t *btstack, uint8_t * packet, uint16_t size);
static void hci_run(btstack_state_t *btstack);
static int  hci_is_le_connection(hci_connection_t * connection);
static int  hci_number_free_acl_slots_for_connection_type(btstack_state_t *btstack, bd_addr_type_t address_type);

#ifdef ENABLE_CLASSIC
static int hci_have_usb_transport(btstack_state_t *btstack);
#endif

#ifdef ENABLE_BLE
#ifdef ENABLE_LE_CENTRAL
// called from test/ble_client/advertising_data_parser.c
void le_handle_advertisement_report(btstack_state_t *btstack, uint8_t *packet, uint16_t size);
static void hci_remove_from_whitelist(btstack_state_t *btstack, bd_addr_type_t address_type, bd_addr_t address);
static hci_connection_t * gap_get_outgoing_connection(btstack_state_t *btstack);
#endif
#endif

#ifdef ENABLE_CLASSIC
// default name
static const char * default_classic_name = "BTstack 00:00:00:00:00:00";
#endif

/**
 * create connection for given address
 *
 * @return connection OR NULL, if no memory left
 */
static hci_connection_t * create_connection_for_bd_addr_and_type(btstack_state_t * btstack, bd_addr_t addr, bd_addr_type_t addr_type){
    log_info("create_connection_for_addr %s, type %x", bd_addr_to_str(addr), addr_type);
    hci_connection_t * conn = btstack_memory_hci_connection_get();
    if (!conn) return NULL;
    bd_addr_copy(conn->address, addr);
    conn->address_type = addr_type;
    conn->con_handle = 0xffff;
    conn->authentication_flags = AUTH_FLAGS_NONE;
    conn->bonding_flags = 0;
    conn->requested_security_level = LEVEL_0;
#ifdef ENABLE_CLASSIC
    btstack_run_loop_set_timer_handler(&conn->timeout, hci_connection_timeout_handler);
    btstack_run_loop_set_timer_context(&conn->timeout, conn);
    hci_connection_timestamp(btstack, conn);
#endif
    conn->acl_recombination_length = 0;
    conn->acl_recombination_pos = 0;
    conn->num_packets_sent = 0;

    conn->le_con_parameter_update_state = CON_PARAMETER_UPDATE_NONE;
#ifdef ENABLE_BLE
    conn->le_phy_update_all_phys = 0xff;
#endif
#ifdef ENABLE_LE_LIMIT_ACL_FRAGMENT_BY_MAX_OCTETS
    conn->le_max_tx_octets = 27;
#endif
    btstack_linked_list_add(&btstack->hci->connections, (btstack_linked_item_t *) conn);
    return conn;
}


/**
 * get le connection parameter range
*
 * @return le connection parameter range struct
 */
void gap_get_connection_parameter_range(btstack_state_t *btstack, le_connection_parameter_range_t * range){
    *range = btstack->hci->le_connection_parameter_range;
}

/**
 * set le connection parameter range
 *
 */

void gap_set_connection_parameter_range(btstack_state_t *btstack, le_connection_parameter_range_t *range){
    btstack->hci->le_connection_parameter_range = *range;
}

/**
 * @brief Test if connection parameters are inside in existing rage
 * @param conn_interval_min (unit: 1.25ms)
 * @param conn_interval_max (unit: 1.25ms)
 * @param conn_latency
 * @param supervision_timeout (unit: 10ms)
 * @returns 1 if included
 */
int gap_connection_parameter_range_included(le_connection_parameter_range_t * existing_range, uint16_t le_conn_interval_min, uint16_t le_conn_interval_max, uint16_t le_conn_latency, uint16_t le_supervision_timeout){
    if (le_conn_interval_min < existing_range->le_conn_interval_min) return 0;
    if (le_conn_interval_max > existing_range->le_conn_interval_max) return 0;

    if (le_conn_latency < existing_range->le_conn_latency_min) return 0;
    if (le_conn_latency > existing_range->le_conn_latency_max) return 0;

    if (le_supervision_timeout < existing_range->le_supervision_timeout_min) return 0;
    if (le_supervision_timeout > existing_range->le_supervision_timeout_max) return 0;

    return 1;
}

/**
 * @brief Set max number of connections in LE Peripheral role (if Bluetooth Controller supports it)
 * @note: default: 1
 * @param max_peripheral_connections
 */
#ifdef ENABLE_LE_PERIPHERAL
void gap_set_max_number_peripheral_connections(btstack_state_t *btstack, int max_peripheral_connections){
    btstack->hci->le_max_number_peripheral_connections = max_peripheral_connections;
}
#endif

/**
 * get hci connections iterator
 *
 * @return hci connections iterator
 */

void hci_connections_get_iterator(btstack_state_t *btstack, btstack_linked_list_iterator_t *it){
    btstack_linked_list_iterator_init(it, &btstack->hci->connections);
}

/**
 * get connection for a given handle
 *
 * @return connection OR NULL, if not found
 */
hci_connection_t * hci_connection_for_handle(btstack_state_t *btstack, hci_con_handle_t con_handle){
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, &btstack->hci->connections);
    while (btstack_linked_list_iterator_has_next(&it)){
        hci_connection_t * item = (hci_connection_t *) btstack_linked_list_iterator_next(&it);
        if ( item->con_handle == con_handle ) {
            return item;
        }
    }
    return NULL;
}

/**
 * get connection for given address
 *
 * @return connection OR NULL, if not found
 */
hci_connection_t * hci_connection_for_bd_addr_and_type(btstack_state_t *btstack, bd_addr_t  addr, bd_addr_type_t addr_type){
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, &btstack->hci->connections);
    while (btstack_linked_list_iterator_has_next(&it)){
        hci_connection_t * connection = (hci_connection_t *) btstack_linked_list_iterator_next(&it);
        if (connection->address_type != addr_type)  continue;
        if (memcmp(addr, connection->address, 6) != 0) continue;
        return connection;
    }
    return NULL;
}

inline static void connectionClearAuthenticationFlags(hci_connection_t * conn, hci_authentication_flags_t flags){
    conn->authentication_flags = (hci_authentication_flags_t)(conn->authentication_flags & ~flags);
}

inline static void connectionSetAuthenticationFlags(hci_connection_t * conn, hci_authentication_flags_t flags){
    conn->authentication_flags = (hci_authentication_flags_t)(conn->authentication_flags | flags);
}

#ifdef ENABLE_CLASSIC

#ifdef ENABLE_SCO_OVER_HCI
static int hci_number_sco_connections(btstack_state_t *btstack){
    int connections = 0;
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, btstack->hci->connections);
    while (btstack_linked_list_iterator_has_next(&it)){
        hci_connection_t * connection = (hci_connection_t *) btstack_linked_list_iterator_next(&it);
        if (connection->address_type != BD_ADDR_TYPE_SCO) continue;
        connections++;
    }
    return connections;
}
#endif

static void hci_connection_timeout_handler(btstack_state_t *btstack, btstack_timer_source_t *timer){
    hci_connection_t * connection = (hci_connection_t *) btstack_run_loop_get_timer_context(timer);
#ifdef HAVE_EMBEDDED_TICK
    if (btstack_run_loop_embedded_get_ticks() > connection->timestamp + btstack_run_loop_embedded_ticks_for_ms(HCI_CONNECTION_TIMEOUT_MS)){
        // connections might be timed out
        hci_emit_l2cap_check_timeout(connection);
    }
#else
    if (btstack_run_loop_get_time_ms(btstack) > (connection->timestamp + HCI_CONNECTION_TIMEOUT_MS)){
        // connections might be timed out
        hci_emit_l2cap_check_timeout(btstack, connection);
    }
#endif
}

static void hci_connection_timestamp(btstack_state_t *btstack, hci_connection_t *connection){
#ifdef HAVE_EMBEDDED_TICK
    connection->timestamp = btstack_run_loop_embedded_get_ticks();
#else
    connection->timestamp = btstack_run_loop_get_time_ms(btstack);
#endif
}

/**
 * add authentication flags and reset timer
 * @note: assumes classic connection
 * @note: bd_addr is passed in as litle endian uint8_t * as it is called from parsing packets
 */
static void hci_add_connection_flags_for_flipped_bd_addr(btstack_state_t *btstack, uint8_t *bd_addr, hci_authentication_flags_t flags){
    bd_addr_t addr;
    reverse_bd_addr(bd_addr, addr);
    hci_connection_t * conn = hci_connection_for_bd_addr_and_type(btstack, addr, BD_ADDR_TYPE_ACL);
    if (conn) {
        connectionSetAuthenticationFlags(conn, flags);
        hci_connection_timestamp(btstack, conn);
    }
}

int  hci_authentication_active_for_handle(btstack_state_t *btstack, hci_con_handle_t handle){
    hci_connection_t * conn = hci_connection_for_handle(btstack, handle);
    if (!conn) return 0;
    if (conn->authentication_flags & LEGACY_PAIRING_ACTIVE) return 1;
    if (conn->authentication_flags & SSP_PAIRING_ACTIVE) return 1;
    return 0;
}

void gap_drop_link_key_for_bd_addr(btstack_state_t *btstack, bd_addr_t addr){
    if (!btstack->hci->link_key_db) return;
    log_info("gap_drop_link_key_for_bd_addr: %s", bd_addr_to_str(addr));
    btstack->hci->link_key_db->delete_link_key(addr);
}

void gap_store_link_key_for_bd_addr(btstack_state_t *btstack, bd_addr_t addr, link_key_t link_key, link_key_type_t type){
    if (!btstack->hci->link_key_db) return;
    log_info("gap_store_link_key_for_bd_addr: %s, type %u", bd_addr_to_str(addr), type);
    btstack->hci->link_key_db->put_link_key(addr, link_key, type);
}

void gap_delete_all_link_keys(btstack_state_t *btstack){
    bd_addr_t  addr;
    link_key_t link_key;
    link_key_type_t type;
    btstack_link_key_iterator_t it;
    int ok = gap_link_key_iterator_init(btstack, &it);
    if (!ok) {
        log_error("could not initialize iterator");
        return;
    }
    while (gap_link_key_iterator_get_next(btstack, &it, addr, link_key, &type)){
        gap_drop_link_key_for_bd_addr(btstack, addr);
    }
    gap_link_key_iterator_done(btstack, &it);
}

int gap_link_key_iterator_init(btstack_state_t *btstack, btstack_link_key_iterator_t * it){
    if (!btstack->hci->link_key_db) return 0;
    if (!btstack->hci->link_key_db->iterator_init) return 0;
    return btstack->hci->link_key_db->iterator_init(it);
}
int gap_link_key_iterator_get_next(btstack_state_t *btstack, btstack_link_key_iterator_t * it, bd_addr_t bd_addr, link_key_t link_key, link_key_type_t * type){
    if (!btstack->hci->link_key_db) return 0;
    return btstack->hci->link_key_db->iterator_get_next(it, bd_addr, link_key, type);
}
void gap_link_key_iterator_done(btstack_state_t *btstack, btstack_link_key_iterator_t * it){
    if (!btstack->hci->link_key_db) return;
    btstack->hci->link_key_db->iterator_done(it);
}
#endif

static int hci_is_le_connection(hci_connection_t * connection){
    switch (connection->address_type){
        case BD_ADDR_TYPE_LE_PUBLIC:
        case BD_ADDR_TYPE_LE_RANDOM:
        case BD_ADDR_TYPE_LE_PRIVAT_FALLBACK_PUBLIC:
        case BD_ADDR_TYPE_LE_PRIVAT_FALLBACK_RANDOM:
            return 1;
        default:
            return 0;
    }
}

/**
 * count connections
 */
static int nr_hci_connections(btstack_state_t *btstack){
    int count = 0;
    btstack_linked_item_t *it;
    for (it = (btstack_linked_item_t *) btstack->hci->connections; it != NULL ; it = it->next){
        count++;
    }
    return count;
}

static int hci_number_free_acl_slots_for_connection_type(btstack_state_t *btstack, bd_addr_type_t address_type){

    unsigned int num_packets_sent_classic = 0;
    unsigned int num_packets_sent_le = 0;

    btstack_linked_item_t *it;
    for (it = (btstack_linked_item_t *) btstack->hci->connections; it != NULL; it = it->next){
        hci_connection_t * connection = (hci_connection_t *) it;
        if (hci_is_le_connection(connection)){
            num_packets_sent_le += connection->num_packets_sent;
        }
        if (connection->address_type == BD_ADDR_TYPE_ACL){
            num_packets_sent_classic += connection->num_packets_sent;
        }
    }
    log_debug("ACL classic buffers: %u used of %u", num_packets_sent_classic, btstack->hci->acl_packets_total_num);
    int free_slots_classic = btstack->hci->acl_packets_total_num - num_packets_sent_classic;
    int free_slots_le = 0;

    if (free_slots_classic < 0){
        log_error("hci_number_free_acl_slots: outgoing classic packets (%u) > total classic packets (%u)", num_packets_sent_classic, btstack->hci->acl_packets_total_num);
        return 0;
    }

    if (btstack->hci->le_acl_packets_total_num){
        // if we have LE slots, they are used
        free_slots_le = btstack->hci->le_acl_packets_total_num - num_packets_sent_le;
        if (free_slots_le < 0){
            log_error("hci_number_free_acl_slots: outgoing le packets (%u) > total le packets (%u)", num_packets_sent_le, btstack->hci->le_acl_packets_total_num);
            return 0;
        }
    } else {
        // otherwise, classic slots are used for LE, too
        free_slots_classic -= num_packets_sent_le;
        if (free_slots_classic < 0){
            log_error("hci_number_free_acl_slots: outgoing classic + le packets (%u + %u) > total packets (%u)", num_packets_sent_classic, num_packets_sent_le, btstack->hci->acl_packets_total_num);
            return 0;
        }
    }

    switch (address_type){
        case BD_ADDR_TYPE_UNKNOWN:
            log_error("hci_number_free_acl_slots: unknown address type");
            return 0;

        case BD_ADDR_TYPE_ACL:
            return free_slots_classic;

        default:
           if (btstack->hci->le_acl_packets_total_num){
               return free_slots_le;
           }
           return free_slots_classic;
    }
}

int hci_number_free_acl_slots_for_handle(btstack_state_t *btstack, hci_con_handle_t con_handle){
    // get connection type
    hci_connection_t * connection = hci_connection_for_handle(btstack, con_handle);
    if (!connection){
        log_error("hci_number_free_acl_slots: handle 0x%04x not in connection list", con_handle);
        return 0;
    }
    return hci_number_free_acl_slots_for_connection_type(btstack, connection->address_type);
}

#ifdef ENABLE_CLASSIC
static int hci_number_free_sco_slots(btstack_state_t *btstack){
    unsigned int num_sco_packets_sent  = 0;
    btstack_linked_item_t *it;
    if (btstack->hci->synchronous_flow_control_enabled){
        // explicit flow control
        for (it = (btstack_linked_item_t *) btstack->hci->connections; it ; it = it->next){
            hci_connection_t * connection = (hci_connection_t *) it;
            if (connection->address_type != BD_ADDR_TYPE_SCO) continue;
            num_sco_packets_sent += connection->num_packets_sent;
        }
        if (num_sco_packets_sent > btstack->hci->sco_packets_total_num){
            log_info("hci_number_free_sco_slots:packets (%u) > total packets (%u)", num_sco_packets_sent, btstack->hci->sco_packets_total_num);
            return 0;
        }
        return btstack->hci->sco_packets_total_num - num_sco_packets_sent;
    } else {
        // implicit flow control -- TODO
        int num_ready = 0;
        for (it = (btstack_linked_item_t *) btstack->hci->connections; it ; it = it->next){
            hci_connection_t * connection = (hci_connection_t *) it;
            if (connection->address_type != BD_ADDR_TYPE_SCO) continue;
            if (connection->sco_tx_ready == 0) continue;
            num_ready++;
        }
        return num_ready;
    }
}
#endif

// only used to send HCI Host Number Completed Packets
static int hci_can_send_comand_packet_transport(btstack_state_t *btstack){
    if (btstack->hci->hci_packet_buffer_reserved) return 0;

    // check for async hci transport implementations
    if (btstack->hci->hci_transport->can_send_packet_now){
        if (!btstack->hci->hci_transport->can_send_packet_now(btstack, HCI_COMMAND_DATA_PACKET)){
            return 0;
        }
    }
    return 1;
}

// new functions replacing hci_can_send_packet_now[_using_packet_buffer]
int hci_can_send_command_packet_now(btstack_state_t *btstack){
    if (hci_can_send_comand_packet_transport(btstack) == 0) return 0;
    return btstack->hci->num_cmd_packets > 0;
}

static int hci_transport_can_send_prepared_packet_now(btstack_state_t *btstack, uint8_t packet_type){
    // check for async hci transport implementations
    if (!btstack->hci->hci_transport->can_send_packet_now) return 1;
    return btstack->hci->hci_transport->can_send_packet_now(btstack, packet_type);
}

static int hci_can_send_prepared_acl_packet_for_address_type(btstack_state_t *btstack, bd_addr_type_t address_type){
    if (!hci_transport_can_send_prepared_packet_now(btstack, HCI_ACL_DATA_PACKET)) return 0;
    return hci_number_free_acl_slots_for_connection_type(btstack, address_type) > 0;
}

int hci_can_send_acl_le_packet_now(btstack_state_t *btstack){
    if (btstack->hci->hci_packet_buffer_reserved) return 0;
    return hci_can_send_prepared_acl_packet_for_address_type(btstack, BD_ADDR_TYPE_LE_PUBLIC);
}

int hci_can_send_prepared_acl_packet_now(btstack_state_t *btstack, hci_con_handle_t con_handle) {
    if (!hci_transport_can_send_prepared_packet_now(btstack, HCI_ACL_DATA_PACKET)) return 0;
    return hci_number_free_acl_slots_for_handle(btstack, con_handle) > 0;
}

int hci_can_send_acl_packet_now(btstack_state_t *btstack, hci_con_handle_t con_handle){
    if (btstack->hci->hci_packet_buffer_reserved) return 0;
    return hci_can_send_prepared_acl_packet_now(btstack, con_handle);
}

#ifdef ENABLE_CLASSIC
int hci_can_send_acl_classic_packet_now(btstack_state_t *btstack){
    if (btstack->hci->hci_packet_buffer_reserved) return 0;
    return hci_can_send_prepared_acl_packet_for_address_type(btstack, BD_ADDR_TYPE_ACL);
}

int hci_can_send_prepared_sco_packet_now(btstack_state_t *btstack){
    if (!hci_transport_can_send_prepared_packet_now(btstack, HCI_SCO_DATA_PACKET)) return 0;
    if (hci_have_usb_transport(btstack)){
        return btstack->hci->sco_can_send_now;
    } else {
        return hci_number_free_sco_slots(btstack) > 0;
    }
}

int hci_can_send_sco_packet_now(btstack_state_t *btstack){
    if (btstack->hci->hci_packet_buffer_reserved) return 0;
    return hci_can_send_prepared_sco_packet_now(btstack);
}

void hci_request_sco_can_send_now_event(btstack_state_t *btstack){
    btstack->hci->sco_waiting_for_can_send_now = 1;
    hci_notify_if_sco_can_send_now(btstack);
}
#endif

// used for internal checks in l2cap.c
int hci_is_packet_buffer_reserved(btstack_state_t *btstack){
    return btstack->hci->hci_packet_buffer_reserved;
}

// reserves outgoing packet buffer. @returns 1 if successful
int hci_reserve_packet_buffer(btstack_state_t *btstack){
    if (btstack->hci->hci_packet_buffer_reserved) {
        log_error("hci_reserve_packet_buffer called but buffer already reserved");
        return 0;
    }
    btstack->hci->hci_packet_buffer_reserved = 1;
    return 1;
}

void hci_release_packet_buffer(btstack_state_t *btstack){
    btstack->hci->hci_packet_buffer_reserved = 0;
}

// assumption: synchronous implementations don't provide can_send_packet_now as they don't keep the buffer after the call
static int hci_transport_synchronous(btstack_state_t *btstack){
    return btstack->hci->hci_transport->can_send_packet_now == NULL;
}

static int hci_send_acl_packet_fragments(btstack_state_t *btstack, hci_connection_t *connection){

    // log_info("hci_send_acl_packet_fragments  %u/%u (con 0x%04x)", btstack->hci->acl_fragmentation_pos, btstack->hci->acl_fragmentation_total_size, connection->con_handle);

    // max ACL data packet length depends on connection type (LE vs. Classic) and available buffers
    uint16_t max_acl_data_packet_length = btstack->hci->acl_data_packet_length;
    if (hci_is_le_connection(connection) && (btstack->hci->le_data_packets_length > 0)){
        max_acl_data_packet_length = btstack->hci->le_data_packets_length;
    }

#ifdef ENABLE_LE_LIMIT_ACL_FRAGMENT_BY_MAX_OCTETS
    if (hci_is_le_connection(connection)){
        max_acl_data_packet_length = connection->le_max_tx_octets;
    }
#endif

    log_debug("hci_send_acl_packet_fragments entered");

    int err;
    // multiple packets could be send on a synchronous HCI transport
    while (true){

        log_debug("hci_send_acl_packet_fragments loop entered");

        // get current data
        const uint16_t acl_header_pos = btstack->hci->acl_fragmentation_pos - 4;
        int current_acl_data_packet_length = btstack->hci->acl_fragmentation_total_size - btstack->hci->acl_fragmentation_pos;
        int more_fragments = 0;

        // if ACL packet is larger than Bluetooth packet buffer, only send max_acl_data_packet_length
        if (current_acl_data_packet_length > max_acl_data_packet_length){
            more_fragments = 1;
            current_acl_data_packet_length = max_acl_data_packet_length;
        }

        // copy handle_and_flags if not first fragment and update packet boundary flags to be 01 (continuing fragmnent)
        if (acl_header_pos > 0){
            uint16_t handle_and_flags = little_endian_read_16(btstack->hci->hci_packet_buffer, 0);
            handle_and_flags = (handle_and_flags & 0xcfff) | (1 << 12);
            little_endian_store_16(btstack->hci->hci_packet_buffer, acl_header_pos, handle_and_flags);
        }

        // update header len
        little_endian_store_16(btstack->hci->hci_packet_buffer, acl_header_pos + 2, current_acl_data_packet_length);

        // count packet
        connection->num_packets_sent++;
        log_debug("hci_send_acl_packet_fragments loop before send (more fragments %d)", more_fragments);

        // update state for next fragment (if any) as "transport done" might be sent during send_packet already
        if (more_fragments){
            // update start of next fragment to send
            btstack->hci->acl_fragmentation_pos += current_acl_data_packet_length;
        } else {
            // done
            btstack->hci->acl_fragmentation_pos = 0;
            btstack->hci->acl_fragmentation_total_size = 0;
        }

        // send packet
        uint8_t * packet = &btstack->hci->hci_packet_buffer[acl_header_pos];
        const int size = current_acl_data_packet_length + 4;
        hci_dump_packet(HCI_ACL_DATA_PACKET, 0, packet, size);
        btstack->hci->acl_fragmentation_tx_active = 1;
        err = btstack->hci->hci_transport->send_packet(btstack, HCI_ACL_DATA_PACKET, packet, size);

        log_debug("hci_send_acl_packet_fragments loop after send (more fragments %d)", more_fragments);

        // done yet?
        if (!more_fragments) break;

        // can send more?
        if (!hci_can_send_prepared_acl_packet_now(btstack, connection->con_handle)) return err;
    }

    log_debug("hci_send_acl_packet_fragments loop over");

    // release buffer now for synchronous transport
    if (hci_transport_synchronous(btstack)){
        btstack->hci->acl_fragmentation_tx_active = 0;
        hci_release_packet_buffer(btstack);
        hci_emit_transport_packet_sent(btstack);
    }

    return err;
}

// pre: caller has reserved the packet buffer
int hci_send_acl_packet_buffer(btstack_state_t *btstack, int size){

    // log_info("hci_send_acl_packet_buffer size %u", size);

    if (!btstack->hci->hci_packet_buffer_reserved) {
        log_error("hci_send_acl_packet_buffer called without reserving packet buffer");
        return 0;
    }

    uint8_t * packet = btstack->hci->hci_packet_buffer;
    hci_con_handle_t con_handle = READ_ACL_CONNECTION_HANDLE(packet);

    // check for free places on Bluetooth module
    if (!hci_can_send_prepared_acl_packet_now(btstack, con_handle)) {
        log_error("hci_send_acl_packet_buffer called but no free ACL buffers on controller");
        hci_release_packet_buffer(btstack);
        hci_emit_transport_packet_sent(btstack);
        return BTSTACK_ACL_BUFFERS_FULL;
    }

    hci_connection_t *connection = hci_connection_for_handle(btstack, con_handle);
    if (!connection) {
        log_error("hci_send_acl_packet_buffer called but no connection for handle 0x%04x", con_handle);
        hci_release_packet_buffer(btstack);
        hci_emit_transport_packet_sent(btstack);
        return 0;
    }

#ifdef ENABLE_CLASSIC
    hci_connection_timestamp(btstack, connection);
#endif

    // hci_dump_packet( HCI_ACL_DATA_PACKET, 0, packet, size);

    // setup data
    btstack->hci->acl_fragmentation_total_size = size;
    btstack->hci->acl_fragmentation_pos = 4;   // start of L2CAP packet

    return hci_send_acl_packet_fragments(btstack, connection);
}

#ifdef ENABLE_CLASSIC
// pre: caller has reserved the packet buffer
int hci_send_sco_packet_buffer(btstack_state_t *btstack, int size){

    // log_info("hci_send_acl_packet_buffer size %u", size);

    if (!btstack->hci->hci_packet_buffer_reserved) {
        log_error("hci_send_acl_packet_buffer called without reserving packet buffer");
        return 0;
    }

    uint8_t * packet = btstack->hci->hci_packet_buffer;

    // skip checks in loopback mode
    if (!btstack->hci->loopback_mode){
        hci_con_handle_t con_handle = READ_ACL_CONNECTION_HANDLE(packet);   // same for ACL and SCO

        // check for free places on Bluetooth module
        if (!hci_can_send_prepared_sco_packet_now(btstack)) {
            log_error("hci_send_sco_packet_buffer called but no free SCO buffers on controller");
            hci_release_packet_buffer(btstack);
            hci_emit_transport_packet_sent(btstack);
            return BTSTACK_ACL_BUFFERS_FULL;
        }

        // track send packet in connection struct
        hci_connection_t *connection = hci_connection_for_handle(btstack, con_handle);
        if (!connection) {
            log_error("hci_send_sco_packet_buffer called but no connection for handle 0x%04x", con_handle);
            hci_release_packet_buffer(btstack);
            hci_emit_transport_packet_sent(btstack);
            return 0;
        }

        if (hci_have_usb_transport(btstack)){
            // token used
            btstack->hci->sco_can_send_now = 0;
        } else {
            if (btstack->hci->synchronous_flow_control_enabled){
                connection->num_packets_sent++;
            } else {
                connection->sco_tx_ready--;
            }
        }
    }

    hci_dump_packet( HCI_SCO_DATA_PACKET, 0, packet, size);
    int err = btstack->hci->hci_transport->send_packet(btstack, HCI_SCO_DATA_PACKET, packet, size);

    if (hci_transport_synchronous(btstack)){
        hci_release_packet_buffer(btstack);
        hci_emit_transport_packet_sent(btstack);
    }

    return err;
}
#endif

static void acl_handler(btstack_state_t *btstack, uint8_t *packet, int size){

    // log_info("acl_handler: size %u", size);

    // get info
    hci_con_handle_t con_handle = READ_ACL_CONNECTION_HANDLE(packet);
    hci_connection_t *conn      = hci_connection_for_handle(btstack, con_handle);
    uint8_t  acl_flags          = READ_ACL_FLAGS(packet);
    uint16_t acl_length         = READ_ACL_LENGTH(packet);

    // ignore non-registered handle
    if (!conn){
        log_error( "hci.c: acl_handler called with non-registered handle %u!" , con_handle);
        return;
    }

    // assert packet is complete
    if ((acl_length + 4) != size){
        log_error("hci.c: acl_handler called with ACL packet of wrong size %d, expected %u => dropping packet", size, acl_length + 4);
        return;
    }

#ifdef ENABLE_CLASSIC
    // update idle timestamp
    hci_connection_timestamp(btstack, conn);
#endif

#ifdef ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
    btstack->hci->host_completed_packets = 1;
    conn->num_packets_completed++;
#endif

    // handle different packet types
    switch (acl_flags & 0x03) {

        case 0x01: // continuation fragment

            // sanity checks
            if (conn->acl_recombination_pos == 0) {
                log_error( "ACL Cont Fragment but no first fragment for handle 0x%02x", con_handle);
                return;
            }
            if ((conn->acl_recombination_pos + acl_length) > (4 + HCI_ACL_BUFFER_SIZE)){
                log_error( "ACL Cont Fragment to large: combined packet %u > buffer size %u for handle 0x%02x",
                    conn->acl_recombination_pos + acl_length, 4 + HCI_ACL_BUFFER_SIZE, con_handle);
                conn->acl_recombination_pos = 0;
                return;
            }

            // append fragment payload (header already stored)
            (void)memcpy(&conn->acl_recombination_buffer[HCI_INCOMING_PRE_BUFFER_SIZE + conn->acl_recombination_pos],
                         &packet[4], acl_length);
            conn->acl_recombination_pos += acl_length;

            // log_error( "ACL Cont Fragment: acl_len %u, combined_len %u, l2cap_len %u", acl_length,
            //        conn->acl_recombination_pos, conn->acl_recombination_length);

            // forward complete L2CAP packet if complete.
            if (conn->acl_recombination_pos >= (conn->acl_recombination_length + 4 + 4)){ // pos already incl. ACL header
                hci_emit_acl_packet(btstack, &conn->acl_recombination_buffer[HCI_INCOMING_PRE_BUFFER_SIZE], conn->acl_recombination_pos);
                // reset recombination buffer
                conn->acl_recombination_length = 0;
                conn->acl_recombination_pos = 0;
            }
            break;

        case 0x02: { // first fragment

            // sanity check
            if (conn->acl_recombination_pos) {
                log_error( "ACL First Fragment but data in buffer for handle 0x%02x, dropping stale fragments", con_handle);
                conn->acl_recombination_pos = 0;
            }

            // peek into L2CAP packet!
            uint16_t l2cap_length = READ_L2CAP_LENGTH( packet );

            // log_info( "ACL First Fragment: acl_len %u, l2cap_len %u", acl_length, l2cap_length);

            // compare fragment size to L2CAP packet size
            if (acl_length >= (l2cap_length + 4)){
                // forward fragment as L2CAP packet
                hci_emit_acl_packet(btstack, packet, acl_length + 4);
            } else {

                if (acl_length > HCI_ACL_BUFFER_SIZE){
                    log_error( "ACL First Fragment to large: fragment %u > buffer size %u for handle 0x%02x",
                        4 + acl_length, 4 + HCI_ACL_BUFFER_SIZE, con_handle);
                    return;
                }

                // store first fragment and tweak acl length for complete package
                (void)memcpy(&conn->acl_recombination_buffer[HCI_INCOMING_PRE_BUFFER_SIZE],
                             packet, acl_length + 4);
                conn->acl_recombination_pos    = acl_length + 4;
                conn->acl_recombination_length = l2cap_length;
                little_endian_store_16(conn->acl_recombination_buffer, HCI_INCOMING_PRE_BUFFER_SIZE + 2, l2cap_length +4);
            }
            break;

        }
        default:
            log_error( "hci.c: acl_handler called with invalid packet boundary flags %u", acl_flags & 0x03);
            return;
    }

    // execute main loop
    hci_run(btstack);
}

static void hci_shutdown_connection(btstack_state_t *btstack, hci_connection_t *conn){
    log_info("Connection closed: handle 0x%x, %s", conn->con_handle, bd_addr_to_str(conn->address));

#ifdef ENABLE_CLASSIC
#ifdef ENABLE_SCO_OVER_HCI
    int addr_type = conn->address_type;
#endif
#endif

    btstack_run_loop_remove_timer(btstack, &conn->timeout);

    btstack_linked_list_remove(&btstack->hci->connections, (btstack_linked_item_t *) conn);
    btstack_memory_hci_connection_free( conn );

    // now it's gone
    hci_emit_nr_connections_changed(btstack);

#ifdef ENABLE_CLASSIC
#ifdef ENABLE_SCO_OVER_HCI
    // update SCO
    if (addr_type == BD_ADDR_TYPE_SCO && btstack->hci->hci_transport && btstack->hci->hci_transport->set_sco_config){
        btstack->hci->hci_transport->set_sco_config(btstack->hci->sco_voice_setting_active, hci_number_sco_connections(btstack));
    }
#endif
#endif
}

#ifdef ENABLE_CLASSIC

static const uint16_t packet_type_sizes[] = {
    0, HCI_ACL_2DH1_SIZE, HCI_ACL_3DH1_SIZE, HCI_ACL_DM1_SIZE,
    HCI_ACL_DH1_SIZE, 0, 0, 0,
    HCI_ACL_2DH3_SIZE, HCI_ACL_3DH3_SIZE, HCI_ACL_DM3_SIZE, HCI_ACL_DH3_SIZE,
    HCI_ACL_2DH5_SIZE, HCI_ACL_3DH5_SIZE, HCI_ACL_DM5_SIZE, HCI_ACL_DH5_SIZE
};
static const uint8_t  packet_type_feature_requirement_bit[] = {
     0, // 3 slot packets
     1, // 5 slot packets
    25, // EDR 2 mpbs
    26, // EDR 3 mbps
    39, // 3 slot EDR packts
    40, // 5 slot EDR packet
};
static const uint16_t packet_type_feature_packet_mask[] = {
    0x0f00, // 3 slot packets
    0xf000, // 5 slot packets
    0x1102, // EDR 2 mpbs
    0x2204, // EDR 3 mbps
    0x0300, // 3 slot EDR packts
    0x3000, // 5 slot EDR packet
};

static uint16_t hci_acl_packet_types_for_buffer_size_and_local_features(uint16_t buffer_size, uint8_t * local_supported_features){
    // enable packet types based on size
    uint16_t packet_types = 0;
    unsigned int i;
    for (i=0;i<16;i++){
        if (packet_type_sizes[i] == 0) continue;
        if (packet_type_sizes[i] <= buffer_size){
            packet_types |= 1 << i;
        }
    }
    // disable packet types due to missing local supported features
    for (i=0;i<sizeof(packet_type_feature_requirement_bit);i++){
        unsigned int bit_idx = packet_type_feature_requirement_bit[i];
        int feature_set = (local_supported_features[bit_idx >> 3] & (1<<(bit_idx & 7))) != 0;
        if (feature_set) continue;
        log_info("Features bit %02u is not set, removing packet types 0x%04x", bit_idx, packet_type_feature_packet_mask[i]);
        packet_types &= ~packet_type_feature_packet_mask[i];
    }
    // flip bits for "may not be used"
    packet_types ^= 0x3306;
    return packet_types;
}

uint16_t hci_usable_acl_packet_types(btstack_state_t *btstack){
    return btstack->hci->packet_types;
}
#endif

uint8_t* hci_get_outgoing_packet_buffer(btstack_state_t *btstack){
    // hci packet buffer is >= acl data packet length
    return btstack->hci->hci_packet_buffer;
}

uint16_t hci_max_acl_data_packet_length(btstack_state_t *btstack){
    return btstack->hci->acl_data_packet_length;
}

#ifdef ENABLE_CLASSIC
int hci_extended_sco_link_supported(btstack_state_t *btstack){
    // No. 31, byte 3, bit 7
    return (btstack->hci->local_supported_features[3] & (1 << 7)) != 0;
}
#endif

int hci_non_flushable_packet_boundary_flag_supported(btstack_state_t *btstack){
    // No. 54, byte 6, bit 6
    return (btstack->hci->local_supported_features[6] & (1 << 6)) != 0;
}

static int gap_ssp_supported(btstack_state_t *btstack){
    // No. 51, byte 6, bit 3
    return (btstack->hci->local_supported_features[6] & (1 << 3)) != 0;
}

static int hci_classic_supported(btstack_state_t *btstack){
#ifdef ENABLE_CLASSIC
    // No. 37, byte 4, bit 5, = No BR/EDR Support
    return (btstack->hci->local_supported_features[4] & (1 << 5)) == 0;
#else
    return 0;
#endif
}

static int hci_le_supported(btstack_state_t *btstack){
#ifdef ENABLE_BLE
    // No. 37, byte 4, bit 6 = LE Supported (Controller)
    return (btstack->hci->local_supported_features[4] & (1 << 6)) != 0;
#else
    return 0;
#endif
}

#ifdef ENABLE_BLE

/**
 * @brief Get addr type and address used for LE in Advertisements, Scan Responses,
 */
void gap_le_get_own_address(btstack_state_t *btstack, uint8_t * addr_type, bd_addr_t addr){
    *addr_type = btstack->hci->le_own_addr_type;
    if (btstack->hci->le_own_addr_type){
        (void)memcpy(addr, btstack->hci->le_random_address, 6);
    } else {
        (void)memcpy(addr, btstack->hci->local_bd_addr, 6);
    }
}

#ifdef ENABLE_LE_CENTRAL
void le_handle_advertisement_report(btstack_state_t *btstack, uint8_t *packet, uint16_t size){

    int offset = 3;
    int num_reports = packet[offset];
    offset += 1;

    int i;
    // log_info("HCI: handle adv report with num reports: %d", num_reports);
    uint8_t event[12 + LE_ADVERTISING_DATA_SIZE]; // use upper bound to avoid var size automatic var
    for (i=0; (i<num_reports) && (offset < size);i++){
        // sanity checks on data_length:
        uint8_t data_length = packet[offset + 8];
        if (data_length > LE_ADVERTISING_DATA_SIZE) return;
        if ((offset + 9 + data_length + 1) > size)    return;
        // setup event
        uint8_t event_size = 10 + data_length;
        int pos = 0;
        event[pos++] = GAP_EVENT_ADVERTISING_REPORT;
        event[pos++] = event_size;
        (void)memcpy(&event[pos], &packet[offset], 1 + 1 + 6); // event type + address type + address
        offset += 8;
        pos += 8;
        event[pos++] = packet[offset + 1 + data_length]; // rssi
        event[pos++] = data_length;
        offset++;
        (void)memcpy(&event[pos], &packet[offset], data_length);
        pos +=    data_length;
        offset += data_length + 1; // rssi
        hci_emit_event(btstack, event, pos, 1);
    }
}
#endif
#endif

#ifdef ENABLE_BLE
#ifdef ENABLE_LE_PERIPHERAL
static void hci_reenable_advertisements_if_needed(void){
    if (!btstack->hci->le_advertisements_active && btstack->hci->le_advertisements_enabled){
        // get number of active le slave connections
        int num_slave_connections = 0;
        btstack_linked_list_iterator_t it;
        btstack_linked_list_iterator_init(&it, btstack->hci->connections);
        while (btstack_linked_list_iterator_has_next(&it)){
            hci_connection_t * con = (hci_connection_t*) btstack_linked_list_iterator_next(&it);
            log_info("state %u, role %u, le_con %u", con->state, con->role, hci_is_le_connection(con));
            if (con->state != OPEN) continue;
            if (con->role  != HCI_ROLE_SLAVE) continue;
            if (!hci_is_le_connection(con)) continue;
            num_slave_connections++;
        }
        log_info("Num LE Peripheral roles: %u of %u", num_slave_connections, btstack->hci->le_max_number_peripheral_connections);
        if (num_slave_connections < btstack->hci->le_max_number_peripheral_connections){
            btstack->hci->le_advertisements_todo |= LE_ADVERTISEMENT_TASKS_ENABLE;
        }
    }
}
#endif
#endif

#if !defined(HAVE_PLATFORM_IPHONE_OS) && !defined (HAVE_HOST_CONTROLLER_API)

static uint32_t hci_transport_uart_get_main_baud_rate(btstack_state_t *btstack){
    if (!btstack->hci->config) return 0;
    uint32_t baud_rate = ((hci_transport_config_uart_t *)btstack->hci->config)->baudrate_main;
    // Limit baud rate for Broadcom chipsets to 3 mbps
    if ((btstack->hci->manufacturer == BLUETOOTH_COMPANY_ID_BROADCOM_CORPORATION) && (baud_rate > 3000000)){
        baud_rate = 3000000;
    }
    return baud_rate;
}

static void hci_initialization_timeout_handler(btstack_state_t *btstack, btstack_timer_source_t * ds){
    UNUSED(ds);

    switch (btstack->hci->substate){
        case HCI_INIT_W4_SEND_RESET:
            log_info("Resend HCI Reset");
            btstack->hci->substate = HCI_INIT_SEND_RESET;
            btstack->hci->num_cmd_packets = 1;
            hci_run(btstack);
            break;
        case HCI_INIT_W4_CUSTOM_INIT_CSR_WARM_BOOT_LINK_RESET:
            log_info("Resend HCI Reset - CSR Warm Boot with Link Reset");
            if (btstack->hci->hci_transport->reset_link){
                btstack->hci->hci_transport->reset_link();
            }

            /* fall through */

        case HCI_INIT_W4_CUSTOM_INIT_CSR_WARM_BOOT:
            log_info("Resend HCI Reset - CSR Warm Boot");
            btstack->hci->substate = HCI_INIT_SEND_RESET_CSR_WARM_BOOT;
            btstack->hci->num_cmd_packets = 1;
            hci_run(btstack);
            break;
        case HCI_INIT_W4_SEND_BAUD_CHANGE:
            if (btstack->hci->hci_transport->set_baudrate){
                uint32_t baud_rate = hci_transport_uart_get_main_baud_rate(btstack);
                log_info("Local baud rate change to %"PRIu32"(timeout handler)", baud_rate);
                btstack->hci->hci_transport->set_baudrate(btstack, baud_rate);
            }
            // For CSR, HCI Reset is sent on new baud rate. Don't forget to reset link for H5/BCSP
            if (btstack->hci->manufacturer == BLUETOOTH_COMPANY_ID_CAMBRIDGE_SILICON_RADIO){
                if (btstack->hci->hci_transport->reset_link){
                    log_info("Link Reset");
                    btstack->hci->hci_transport->reset_link();
                }
                btstack->hci->substate = HCI_INIT_SEND_RESET_CSR_WARM_BOOT;
                hci_run(btstack);
            }
            break;
        case HCI_INIT_W4_CUSTOM_INIT_BCM_DELAY:
            // otherwise continue
            btstack->hci->substate = HCI_INIT_W4_READ_LOCAL_SUPPORTED_COMMANDS;
            hci_send_cmd(btstack, &hci_read_local_supported_commands);
            break;
        default:
            break;
    }
}
#endif

static void hci_initializing_next_state(btstack_state_t *btstack){
    btstack->hci->substate = (hci_substate_t )( ((int) btstack->hci->substate) + 1);
}

// assumption: hci_can_send_command_packet_now() == true
static void hci_initializing_run(btstack_state_t *btstack){
    einstein_log(90, __func__, __LINE__, "%d", btstack->hci->substate);
    log_debug("hci_initializing_run: substate %u, can send %u", btstack->hci->substate, hci_can_send_command_packet_now(btstack));
    switch (btstack->hci->substate){
        case HCI_INIT_SEND_RESET:
            hci_state_reset(btstack);

#if !defined(HAVE_PLATFORM_IPHONE_OS) && !defined (HAVE_HOST_CONTROLLER_API)
            // prepare reset if command complete not received in 100ms
            btstack_run_loop_set_timer(btstack, &btstack->hci->timeout, HCI_RESET_RESEND_TIMEOUT_MS);
            btstack_run_loop_set_timer_handler(&btstack->hci->timeout, hci_initialization_timeout_handler);
            btstack_run_loop_add_timer(btstack, &btstack->hci->timeout);
#endif
            // send command
            btstack->hci->substate = HCI_INIT_W4_SEND_RESET;
            hci_send_cmd(btstack, &hci_reset);
            break;
        case HCI_INIT_SEND_READ_LOCAL_VERSION_INFORMATION:
            hci_send_cmd(btstack, &hci_read_local_version_information);
            btstack->hci->substate = HCI_INIT_W4_SEND_READ_LOCAL_VERSION_INFORMATION;
            break;
        case HCI_INIT_SEND_READ_LOCAL_NAME:
            hci_send_cmd(btstack, &hci_read_local_name);
            btstack->hci->substate = HCI_INIT_W4_SEND_READ_LOCAL_NAME;
            break;

#if !defined(HAVE_PLATFORM_IPHONE_OS) && !defined (HAVE_HOST_CONTROLLER_API)
        case HCI_INIT_SEND_RESET_CSR_WARM_BOOT:
            hci_state_reset(btstack);
            // prepare reset if command complete not received in 100ms
            btstack_run_loop_set_timer(btstack, &btstack->hci->timeout, HCI_RESET_RESEND_TIMEOUT_MS);
            btstack_run_loop_set_timer_handler(&btstack->hci->timeout, hci_initialization_timeout_handler);
            btstack_run_loop_add_timer(btstack, &btstack->hci->timeout);
            // send command
            btstack->hci->substate = HCI_INIT_W4_CUSTOM_INIT_CSR_WARM_BOOT;
            hci_send_cmd(btstack, &hci_reset);
            break;
        case HCI_INIT_SEND_RESET_ST_WARM_BOOT:
            hci_state_reset(btstack);
            btstack->hci->substate = HCI_INIT_W4_SEND_RESET_ST_WARM_BOOT;
            hci_send_cmd(btstack, &hci_reset);
            break;
        case HCI_INIT_SEND_BAUD_CHANGE: {
            uint32_t baud_rate = hci_transport_uart_get_main_baud_rate(btstack);
            btstack->hci->chipset->set_baudrate_command(baud_rate, btstack->hci->hci_packet_buffer);
            btstack->hci->last_cmd_opcode = little_endian_read_16(btstack->hci->hci_packet_buffer, 0);
            btstack->hci->substate = HCI_INIT_W4_SEND_BAUD_CHANGE;
            hci_send_cmd_packet(btstack, btstack->hci->hci_packet_buffer, 3 + btstack->hci->hci_packet_buffer[2]);
            // STLC25000D: baudrate change happens within 0.5 s after command was send,
            // use timer to update baud rate after 100 ms (knowing exactly, when command was sent is non-trivial)
            if (btstack->hci->manufacturer == BLUETOOTH_COMPANY_ID_ST_MICROELECTRONICS){
                btstack_run_loop_set_timer(btstack, &btstack->hci->timeout, HCI_RESET_RESEND_TIMEOUT_MS);
                btstack_run_loop_add_timer(btstack, &btstack->hci->timeout);
            }
            break;
        }
        case HCI_INIT_SEND_BAUD_CHANGE_BCM: {
            uint32_t baud_rate = hci_transport_uart_get_main_baud_rate(btstack);
            btstack->hci->chipset->set_baudrate_command(baud_rate, btstack->hci->hci_packet_buffer);
            btstack->hci->last_cmd_opcode = little_endian_read_16(btstack->hci->hci_packet_buffer, 0);
            btstack->hci->substate = HCI_INIT_W4_SEND_BAUD_CHANGE_BCM;
            hci_send_cmd_packet(btstack, btstack->hci->hci_packet_buffer, 3 + btstack->hci->hci_packet_buffer[2]);
            break;
        }
        case HCI_INIT_CUSTOM_INIT:
            // Custom initialization
            if (btstack->hci->chipset && btstack->hci->chipset->next_command){
                btstack->hci->chipset_result = (*btstack->hci->chipset->next_command)(btstack->hci->hci_packet_buffer);
                int send_cmd = 0;
                switch (btstack->hci->chipset_result){
                    case BTSTACK_CHIPSET_VALID_COMMAND:
                        send_cmd = 1;
                        btstack->hci->substate = HCI_INIT_W4_CUSTOM_INIT;
                        break;
                    case BTSTACK_CHIPSET_WARMSTART_REQUIRED:
                        send_cmd = 1;
                        // CSR Warm Boot: Wait a bit, then send HCI Reset until HCI Command Complete
                        log_info("CSR Warm Boot");
                        btstack_run_loop_set_timer(btstack, &btstack->hci->timeout, HCI_RESET_RESEND_TIMEOUT_MS);
                        btstack_run_loop_set_timer_handler(&btstack->hci->timeout, hci_initialization_timeout_handler);
                        btstack_run_loop_add_timer(btstack, &btstack->hci->timeout);
                        if ((btstack->hci->manufacturer == BLUETOOTH_COMPANY_ID_CAMBRIDGE_SILICON_RADIO)
                            && btstack->hci->config
                            && btstack->hci->chipset
                            // && btstack->hci->chipset->set_baudrate_command -- there's no such command
                            && btstack->hci->hci_transport->set_baudrate
                            && hci_transport_uart_get_main_baud_rate(btstack)){
                            btstack->hci->substate = HCI_INIT_W4_SEND_BAUD_CHANGE;
                        } else {
                           btstack->hci->substate = HCI_INIT_W4_CUSTOM_INIT_CSR_WARM_BOOT_LINK_RESET;
                        }
                        break;
                    default:
                        break;
                }

                if (send_cmd){
                    int size = 3 + btstack->hci->hci_packet_buffer[2];
                    btstack->hci->last_cmd_opcode = little_endian_read_16(btstack->hci->hci_packet_buffer, 0);
                    hci_dump_packet(HCI_COMMAND_DATA_PACKET, 0, btstack->hci->hci_packet_buffer, size);
                    btstack->hci->hci_transport->send_packet(btstack, HCI_COMMAND_DATA_PACKET, btstack->hci->hci_packet_buffer, size);
                    break;
                }
                log_info("Init script done");

                // Init script download on Broadcom chipsets causes:
                if ( (btstack->hci->chipset_result != BTSTACK_CHIPSET_NO_INIT_SCRIPT) &&
                   (  (btstack->hci->manufacturer == BLUETOOTH_COMPANY_ID_BROADCOM_CORPORATION)
                ||    (btstack->hci->manufacturer == BLUETOOTH_COMPANY_ID_EM_MICROELECTRONIC_MARIN_SA)) ){

                    // - baud rate to reset, restore UART baud rate if needed
                    int need_baud_change = btstack->hci->config
                        && btstack->hci->chipset
                        && btstack->hci->chipset->set_baudrate_command
                        && btstack->hci->hci_transport->set_baudrate
                        && ((hci_transport_config_uart_t *)btstack->hci->config)->baudrate_main;
                    if (need_baud_change) {
                        uint32_t baud_rate = ((hci_transport_config_uart_t *)btstack->hci->config)->baudrate_init;
                        log_info("Local baud rate change to %"PRIu32" after init script (bcm)", baud_rate);
                        btstack->hci->hci_transport->set_baudrate(btstack, baud_rate);
                    }

                    uint16_t bcm_delay_ms = 300;
                    // - UART may or may not be disabled during update and Controller RTS may or may not be high during this time
                    //   -> Work around: wait here.
                    log_info("BCM delay (%u ms) after init script", bcm_delay_ms);
                    btstack->hci->substate = HCI_INIT_W4_CUSTOM_INIT_BCM_DELAY;
                    btstack_run_loop_set_timer(btstack, &btstack->hci->timeout, bcm_delay_ms);
                    btstack_run_loop_set_timer_handler(&btstack->hci->timeout, hci_initialization_timeout_handler);
                    btstack_run_loop_add_timer(btstack, &btstack->hci->timeout);
                    break;
                }
            }
            // otherwise continue
            btstack->hci->substate = HCI_INIT_W4_READ_LOCAL_SUPPORTED_COMMANDS;
            hci_send_cmd(btstack, &hci_read_local_supported_commands);
            break;
        case HCI_INIT_SET_BD_ADDR:
            log_info("Set Public BD ADDR to %s", bd_addr_to_str(btstack->hci->custom_bd_addr));
            btstack->hci->chipset->set_bd_addr_command(btstack->hci->custom_bd_addr, btstack->hci->hci_packet_buffer);
            btstack->hci->last_cmd_opcode = little_endian_read_16(btstack->hci->hci_packet_buffer, 0);
            btstack->hci->substate = HCI_INIT_W4_SET_BD_ADDR;
            hci_send_cmd_packet(btstack, btstack->hci->hci_packet_buffer, 3 + btstack->hci->hci_packet_buffer[2]);
            break;
#endif

        case HCI_INIT_READ_LOCAL_SUPPORTED_COMMANDS:
            log_info("Resend hci_read_local_supported_commands after CSR Warm Boot double reset");
            btstack->hci->substate = HCI_INIT_W4_READ_LOCAL_SUPPORTED_COMMANDS;
            hci_send_cmd(btstack, &hci_read_local_supported_commands);
            break;
        case HCI_INIT_READ_BD_ADDR:
            btstack->hci->substate = HCI_INIT_W4_READ_BD_ADDR;
            hci_send_cmd(btstack, &hci_read_bd_addr);
            break;
        case HCI_INIT_READ_BUFFER_SIZE:
            btstack->hci->substate = HCI_INIT_W4_READ_BUFFER_SIZE;
            hci_send_cmd(btstack, &hci_read_buffer_size);
            break;
        case HCI_INIT_READ_LOCAL_SUPPORTED_FEATURES:
            btstack->hci->substate = HCI_INIT_W4_READ_LOCAL_SUPPORTED_FEATURES;
            hci_send_cmd(btstack, &hci_read_local_supported_features);
            break;

#ifdef ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
        case HCI_INIT_SET_CONTROLLER_TO_HOST_FLOW_CONTROL:
            btstack->hci->substate = HCI_INIT_W4_SET_CONTROLLER_TO_HOST_FLOW_CONTROL;
            hci_send_cmd(&hci_set_controller_to_host_flow_control, 3);  // ACL + SCO Flow Control
            break;
        case HCI_INIT_HOST_BUFFER_SIZE:
            btstack->hci->substate = HCI_INIT_W4_HOST_BUFFER_SIZE;
            hci_send_cmd(&hci_host_buffer_size, HCI_HOST_ACL_PACKET_LEN, HCI_HOST_SCO_PACKET_LEN,
                                                HCI_HOST_ACL_PACKET_NUM, HCI_HOST_SCO_PACKET_NUM);
            break;
#endif

        case HCI_INIT_SET_EVENT_MASK:
            btstack->hci->substate = HCI_INIT_W4_SET_EVENT_MASK;
            if (hci_le_supported(btstack)){
                hci_send_cmd(btstack, &hci_set_event_mask,0xffffffff, 0x3FFFFFFF);
            } else {
                // Kensington Bluetooth 2.1 USB Dongle (CSR Chipset) returns an error for 0xffff...
                hci_send_cmd(btstack, &hci_set_event_mask,0xffffffff, 0x1FFFFFFF);
            }
            break;

#ifdef ENABLE_CLASSIC
        case HCI_INIT_WRITE_SIMPLE_PAIRING_MODE:
            btstack->hci->substate = HCI_INIT_W4_WRITE_SIMPLE_PAIRING_MODE;
            hci_send_cmd(btstack, &hci_write_simple_pairing_mode, btstack->hci->ssp_enable);
            break;
        case HCI_INIT_WRITE_PAGE_TIMEOUT:
            btstack->hci->substate = HCI_INIT_W4_WRITE_PAGE_TIMEOUT;
            hci_send_cmd(btstack, &hci_write_page_timeout, 0x6000);  // ca. 15 sec
            break;
        case HCI_INIT_WRITE_DEFAULT_LINK_POLICY_SETTING:
            btstack->hci->substate = HCI_INIT_W4_WRITE_DEFAULT_LINK_POLICY_SETTING;
            hci_send_cmd(btstack, &hci_write_default_link_policy_setting, btstack->hci->default_link_policy_settings);
            break;
        case HCI_INIT_WRITE_CLASS_OF_DEVICE:
            btstack->hci->substate = HCI_INIT_W4_WRITE_CLASS_OF_DEVICE;
            hci_send_cmd(btstack, &hci_write_class_of_device, btstack->hci->class_of_device);
            break;
        case HCI_INIT_WRITE_LOCAL_NAME: {
            btstack->hci->substate = HCI_INIT_W4_WRITE_LOCAL_NAME;
            hci_reserve_packet_buffer(btstack);
            uint8_t * packet = btstack->hci->hci_packet_buffer;
            // construct HCI Command and send
            uint16_t opcode = hci_write_local_name.opcode;
            btstack->hci->last_cmd_opcode = opcode;
            packet[0] = opcode & 0xff;
            packet[1] = opcode >> 8;
            packet[2] = DEVICE_NAME_LEN;
            memset(&packet[3], 0, DEVICE_NAME_LEN);
            uint16_t name_len = (uint16_t) strlen(btstack->hci->local_name);
            uint16_t bytes_to_copy = btstack_min(name_len, DEVICE_NAME_LEN);
            // if shorter than DEVICE_NAME_LEN, it's implicitly NULL-terminated by memset call
            (void)memcpy(&packet[3], btstack->hci->local_name, bytes_to_copy);
            // expand '00:00:00:00:00:00' in name with bd_addr
            btstack_replace_bd_addr_placeholder(&packet[3], bytes_to_copy, btstack->hci->local_bd_addr);
            hci_send_cmd_packet(btstack, packet, HCI_CMD_HEADER_SIZE + DEVICE_NAME_LEN);
            break;
        }
        case HCI_INIT_WRITE_EIR_DATA: {
            btstack->hci->substate = HCI_INIT_W4_WRITE_EIR_DATA;
            hci_reserve_packet_buffer(btstack);
            uint8_t * packet = btstack->hci->hci_packet_buffer;
            // construct HCI Command in-place and send
            uint16_t opcode = hci_write_extended_inquiry_response.opcode;
            btstack->hci->last_cmd_opcode = opcode;
            uint16_t offset = 0;
            packet[offset++] = opcode & 0xff;
            packet[offset++] = opcode >> 8;
            packet[offset++] = 1 + EXTENDED_INQUIRY_RESPONSE_DATA_LEN;
            packet[offset++] = 0;  // FEC not required
            memset(&packet[offset], 0, EXTENDED_INQUIRY_RESPONSE_DATA_LEN);
            if (btstack->hci->eir_data){
                // copy items and expand '00:00:00:00:00:00' in name with bd_addr
                ad_context_t context;
                for (ad_iterator_init(&context, EXTENDED_INQUIRY_RESPONSE_DATA_LEN, btstack->hci->eir_data) ; ad_iterator_has_more(&context) ; ad_iterator_next(&context)) {
                    uint8_t data_type   = ad_iterator_get_data_type(&context);
                    uint8_t size        = ad_iterator_get_data_len(&context);
                    const uint8_t *data = ad_iterator_get_data(&context);
                    // copy item
                    packet[offset++] = size + 1;
                    packet[offset++] = data_type;
                    memcpy(&packet[offset], data, size);
                    // update name item
                    if ((data_type == BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME) || (data_type == BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME)){
                        btstack_replace_bd_addr_placeholder(&packet[offset], size, btstack->hci->local_bd_addr);
                    }
                    offset += size;
                }
            } else {
                uint16_t name_len = (uint16_t) strlen(btstack->hci->local_name);
                uint16_t bytes_to_copy = btstack_min(name_len, EXTENDED_INQUIRY_RESPONSE_DATA_LEN - 2);
                packet[offset++] = bytes_to_copy + 1;
                packet[offset++] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
                (void)memcpy(&packet[6], btstack->hci->local_name, bytes_to_copy);
                // expand '00:00:00:00:00:00' in name with bd_addr
                btstack_replace_bd_addr_placeholder(&packet[offset], bytes_to_copy, btstack->hci->local_bd_addr);
            }
            hci_send_cmd_packet(btstack, packet, HCI_CMD_HEADER_SIZE + 1 + EXTENDED_INQUIRY_RESPONSE_DATA_LEN);
            break;
        }
        case HCI_INIT_WRITE_INQUIRY_MODE:
            btstack->hci->substate = HCI_INIT_W4_WRITE_INQUIRY_MODE;
            hci_send_cmd(btstack, &hci_write_inquiry_mode, (int) btstack->hci->inquiry_mode);
            break;
        case HCI_INIT_WRITE_SECURE_CONNECTIONS_HOST_ENABLE:
            hci_send_cmd(btstack, &hci_write_secure_connections_host_support, 1);
            btstack->hci->substate = HCI_INIT_W4_WRITE_SECURE_CONNECTIONS_HOST_ENABLE;
            break;
        case HCI_INIT_WRITE_SCAN_ENABLE:
            hci_send_cmd(btstack, &hci_write_scan_enable, (btstack->hci->connectable << 1) | btstack->hci->discoverable); // page scan
            btstack->hci->substate = HCI_INIT_W4_WRITE_SCAN_ENABLE;
            break;
        // only sent if ENABLE_SCO_OVER_HCI is defined
        case HCI_INIT_WRITE_SYNCHRONOUS_FLOW_CONTROL_ENABLE:
            btstack->hci->substate = HCI_INIT_W4_WRITE_SYNCHRONOUS_FLOW_CONTROL_ENABLE;
            hci_send_cmd(btstack, &hci_write_synchronous_flow_control_enable, 1); // SCO tracking enabled
            break;
        case HCI_INIT_WRITE_DEFAULT_ERRONEOUS_DATA_REPORTING:
            btstack->hci->substate = HCI_INIT_W4_WRITE_DEFAULT_ERRONEOUS_DATA_REPORTING;
            hci_send_cmd(btstack, &hci_write_default_erroneous_data_reporting, 1);
            break;
        // only sent if ENABLE_SCO_OVER_HCI and manufacturer is Broadcom
        case HCI_INIT_BCM_WRITE_SCO_PCM_INT:
            btstack->hci->substate = HCI_INIT_W4_BCM_WRITE_SCO_PCM_INT;
            log_info("BCM: Route SCO data via HCI transport");
            hci_send_cmd(btstack, &hci_bcm_write_sco_pcm_int, 1, 0, 0, 0, 0);
            break;

#endif
#ifdef ENABLE_BLE
        // LE INIT
        case HCI_INIT_LE_READ_BUFFER_SIZE:
            btstack->hci->substate = HCI_INIT_W4_LE_READ_BUFFER_SIZE;
            hci_send_cmd(btstack, &hci_le_read_buffer_size);
            break;
        case HCI_INIT_LE_SET_EVENT_MASK:
            btstack->hci->substate = HCI_INIT_W4_LE_SET_EVENT_MASK;
            hci_send_cmd(btstack, &hci_le_set_event_mask, 0x809FF, 0x0); // bits 0-8, 11, 19
            break;
        case HCI_INIT_WRITE_LE_HOST_SUPPORTED:
            // LE Supported Host = 1, Simultaneous Host = 0
            btstack->hci->substate = HCI_INIT_W4_WRITE_LE_HOST_SUPPORTED;
            hci_send_cmd(btstack, &hci_write_le_host_supported, 1, 0);
            break;
#endif

#ifdef ENABLE_LE_DATA_LENGTH_EXTENSION
        case HCI_INIT_LE_READ_MAX_DATA_LENGTH:
            btstack->hci->substate = HCI_INIT_W4_LE_READ_MAX_DATA_LENGTH;
            hci_send_cmd(&hci_le_read_maximum_data_length);
            break;
        case HCI_INIT_LE_WRITE_SUGGESTED_DATA_LENGTH:
            btstack->hci->substate = HCI_INIT_W4_LE_WRITE_SUGGESTED_DATA_LENGTH;
            hci_send_cmd(&hci_le_write_suggested_default_data_length, btstack->hci->le_supported_max_tx_octets, btstack->hci->le_supported_max_tx_time);
            break;
#endif

#ifdef ENABLE_LE_CENTRAL
        case HCI_INIT_READ_WHITE_LIST_SIZE:
            btstack->hci->substate = HCI_INIT_W4_READ_WHITE_LIST_SIZE;
            hci_send_cmd(btstack, &hci_le_read_white_list_size);
            break;
        case HCI_INIT_LE_SET_SCAN_PARAMETERS:
            // LE Scan Parameters: active scanning, 300 ms interval, 30 ms window, own address type, accept all advs
            btstack->hci->substate = HCI_INIT_W4_LE_SET_SCAN_PARAMETERS;
            hci_send_cmd(btstack, &hci_le_set_scan_parameters, 1, btstack->hci->le_scan_interval, btstack->hci->le_scan_window, btstack->hci->le_own_addr_type, 0);
            break;
#endif
        default:
            return;
    }
}

static void hci_init_done(btstack_state_t *btstack){
    // done. tell the app
    einstein_here(31, __func__, __LINE__);
    log_info("hci_init_done -> HCI_STATE_WORKING");
    btstack->hci->state = HCI_STATE_WORKING;
    hci_emit_state(btstack);
    hci_run(btstack);
}

static bool hci_initializing_event_handler_command_completed(btstack_state_t *btstack, const uint8_t * packet){
    bool command_completed = false;
    if (hci_event_packet_get_type(packet) == HCI_EVENT_COMMAND_COMPLETE){
        uint16_t opcode = little_endian_read_16(packet,3);
        einstein_log(31, __func__, __LINE__, "%04x %04x", opcode, btstack->hci->last_cmd_opcode);
        if (opcode == btstack->hci->last_cmd_opcode){
            command_completed = true;
            log_debug("Command complete for expected opcode %04x at substate %u", opcode, btstack->hci->substate);
        } else {
            log_info("Command complete for different opcode %04x, expected %04x, at substate %u", opcode, btstack->hci->last_cmd_opcode, btstack->hci->substate);
        }
    }

    if (hci_event_packet_get_type(packet) == HCI_EVENT_COMMAND_STATUS){
        uint8_t  status = packet[2];
        uint16_t opcode = little_endian_read_16(packet,4);
        if (opcode == btstack->hci->last_cmd_opcode){
            if (status){
                command_completed = true;
                log_debug("Command status error 0x%02x for expected opcode %04x at substate %u", status, opcode, btstack->hci->substate);
            } else {
                log_info("Command status OK for expected opcode %04x, waiting for command complete", opcode);
            }
        } else {
            log_debug("Command status for opcode %04x, expected %04x", opcode, btstack->hci->last_cmd_opcode);
        }
    }
#if !defined(HAVE_PLATFORM_IPHONE_OS) && !defined (HAVE_HOST_CONTROLLER_API)
    // Vendor == CSR
    if ((btstack->hci->substate == HCI_INIT_W4_CUSTOM_INIT) && (hci_event_packet_get_type(packet) == HCI_EVENT_VENDOR_SPECIFIC)){
        // TODO: track actual command
        command_completed = true;
    }

    // Vendor == Toshiba
    if ((btstack->hci->substate == HCI_INIT_W4_SEND_BAUD_CHANGE) && (hci_event_packet_get_type(packet) == HCI_EVENT_VENDOR_SPECIFIC)){
        // TODO: track actual command
        command_completed = true;
        // Fix: no HCI Command Complete received, so num_cmd_packets not reset
        btstack->hci->num_cmd_packets = 1;
    }
#endif

    return command_completed;
}

static void hci_initializing_event_handler(btstack_state_t *btstack, const uint8_t * packet, uint16_t size){

    UNUSED(size);   // ok: less than 6 bytes are read from our buffer

    einstein_log(90, __func__, __LINE__, "%d", btstack->hci->substate);
    bool command_completed =  hci_initializing_event_handler_command_completed(btstack, packet);

#if !defined(HAVE_PLATFORM_IPHONE_OS) && !defined (HAVE_HOST_CONTROLLER_API)

    // Late response (> 100 ms) for HCI Reset e.g. on Toshiba TC35661:
    // Command complete for HCI Reset arrives after we've resent the HCI Reset command
    //
    // HCI Reset
    // Timeout 100 ms
    // HCI Reset
    // Command Complete Reset
    // HCI Read Local Version Information
    // Command Complete Reset - but we expected Command Complete Read Local Version Information
    // hang...
    //
    // Fix: Command Complete for HCI Reset in HCI_INIT_W4_SEND_READ_LOCAL_VERSION_INFORMATION trigger resend
    if (!command_completed
            && (hci_event_packet_get_type(packet) == HCI_EVENT_COMMAND_COMPLETE)
            && (btstack->hci->substate == HCI_INIT_W4_SEND_READ_LOCAL_VERSION_INFORMATION)){

        uint16_t opcode = little_endian_read_16(packet,3);
        if (opcode == hci_reset.opcode){
            btstack->hci->substate = HCI_INIT_SEND_READ_LOCAL_VERSION_INFORMATION;
            return;
        }
    }

    // CSR & H5
    // Fix: Command Complete for HCI Reset in HCI_INIT_W4_SEND_READ_LOCAL_VERSION_INFORMATION trigger resend
    if (!command_completed
            && (hci_event_packet_get_type(packet) == HCI_EVENT_COMMAND_COMPLETE)
            && (btstack->hci->substate == HCI_INIT_W4_READ_LOCAL_SUPPORTED_COMMANDS)){

        uint16_t opcode = little_endian_read_16(packet,3);
        if (opcode == hci_reset.opcode){
            btstack->hci->substate = HCI_INIT_READ_LOCAL_SUPPORTED_COMMANDS;
            return;
        }
    }

    // on CSR with BCSP/H5, the reset resend timeout leads to substate == HCI_INIT_SEND_RESET or HCI_INIT_SEND_RESET_CSR_WARM_BOOT
    // fix: Correct substate and behave as command below
    if (command_completed){
        switch (btstack->hci->substate){
            case HCI_INIT_SEND_RESET:
                btstack->hci->substate = HCI_INIT_W4_SEND_RESET;
                break;
            case HCI_INIT_SEND_RESET_CSR_WARM_BOOT:
                btstack->hci->substate = HCI_INIT_W4_CUSTOM_INIT_CSR_WARM_BOOT;
                break;
            default:
                break;
        }
    }

#endif

    if (!command_completed) return;

    bool need_baud_change = false;
    bool need_addr_change = false;

#if !defined(HAVE_PLATFORM_IPHONE_OS) && !defined (HAVE_HOST_CONTROLLER_API)
    need_baud_change = btstack->hci->config
                        && btstack->hci->chipset
                        && btstack->hci->chipset->set_baudrate_command
                        && btstack->hci->hci_transport->set_baudrate
                        && ((hci_transport_config_uart_t *)btstack->hci->config)->baudrate_main;

    need_addr_change = btstack->hci->custom_bd_addr_set
                        && btstack->hci->chipset
                        && btstack->hci->chipset->set_bd_addr_command;
#endif

    switch(btstack->hci->substate){

#if !defined(HAVE_PLATFORM_IPHONE_OS) && !defined (HAVE_HOST_CONTROLLER_API)
        case HCI_INIT_SEND_RESET:
            // on CSR with BCSP/H5, resend triggers resend of HCI Reset and leads to substate == HCI_INIT_SEND_RESET
            // fix: just correct substate and behave as command below
            btstack->hci->substate = HCI_INIT_W4_SEND_RESET;
            btstack_run_loop_remove_timer(btstack, &btstack->hci->timeout);
            break;
        case HCI_INIT_W4_SEND_RESET:
            btstack_run_loop_remove_timer(btstack, &btstack->hci->timeout);
            break;
        case HCI_INIT_W4_SEND_READ_LOCAL_NAME:
            log_info("Received local name, need baud change %d", (int) need_baud_change);
            if (need_baud_change){
                btstack->hci->substate = HCI_INIT_SEND_BAUD_CHANGE;
                return;
            }
            // skip baud change
            btstack->hci->substate = HCI_INIT_CUSTOM_INIT;
            return;
        case HCI_INIT_W4_SEND_BAUD_CHANGE:
            // for STLC2500D, baud rate change already happened.
            // for others, baud rate gets changed now
            if ((btstack->hci->manufacturer != BLUETOOTH_COMPANY_ID_ST_MICROELECTRONICS) && need_baud_change){
                uint32_t baud_rate = hci_transport_uart_get_main_baud_rate(btstack);
                log_info("Local baud rate change to %"PRIu32"(w4_send_baud_change)", baud_rate);
                btstack->hci->hci_transport->set_baudrate(btstack, baud_rate);
            }
            btstack->hci->substate = HCI_INIT_CUSTOM_INIT;
            return;
        case HCI_INIT_W4_CUSTOM_INIT_CSR_WARM_BOOT:
            btstack_run_loop_remove_timer(btstack, &btstack->hci->timeout);
            btstack->hci->substate = HCI_INIT_CUSTOM_INIT;
            return;
        case HCI_INIT_W4_CUSTOM_INIT:
            // repeat custom init
            btstack->hci->substate = HCI_INIT_CUSTOM_INIT;
            return;
#else
        case HCI_INIT_W4_SEND_RESET:
            btstack->hci->substate = HCI_INIT_READ_LOCAL_SUPPORTED_COMMANDS;
            return ;
#endif

        case HCI_INIT_W4_READ_LOCAL_SUPPORTED_COMMANDS:
            if (need_baud_change && (btstack->hci->chipset_result != BTSTACK_CHIPSET_NO_INIT_SCRIPT) &&
              ((btstack->hci->manufacturer == BLUETOOTH_COMPANY_ID_BROADCOM_CORPORATION) ||
               (btstack->hci->manufacturer == BLUETOOTH_COMPANY_ID_EM_MICROELECTRONIC_MARIN_SA))) {
                btstack->hci->substate = HCI_INIT_SEND_BAUD_CHANGE_BCM;
                return;
            }
            if (need_addr_change){
                btstack->hci->substate = HCI_INIT_SET_BD_ADDR;
                return;
            }
            btstack->hci->substate = HCI_INIT_READ_BD_ADDR;
            return;
#if !defined(HAVE_PLATFORM_IPHONE_OS) && !defined (HAVE_HOST_CONTROLLER_API)
        case HCI_INIT_W4_SEND_BAUD_CHANGE_BCM:
            if (need_baud_change){
                uint32_t baud_rate = hci_transport_uart_get_main_baud_rate(btstack);
                log_info("Local baud rate change to %"PRIu32"(w4_send_baud_change_bcm))", baud_rate);
                btstack->hci->hci_transport->set_baudrate(btstack, baud_rate);
            }
            if (need_addr_change){
                btstack->hci->substate = HCI_INIT_SET_BD_ADDR;
                return;
            }
            btstack->hci->substate = HCI_INIT_READ_BD_ADDR;
            return;
        case HCI_INIT_W4_SET_BD_ADDR:
            // for STLC2500D + ATWILC3000, bd addr change only gets active after sending reset command
            if ((btstack->hci->manufacturer == BLUETOOTH_COMPANY_ID_ST_MICROELECTRONICS)
            ||  (btstack->hci->manufacturer == BLUETOOTH_COMPANY_ID_ATMEL_CORPORATION)){
                btstack->hci->substate = HCI_INIT_SEND_RESET_ST_WARM_BOOT;
                return;
            }
            // skipping st warm boot
            btstack->hci->substate = HCI_INIT_READ_BD_ADDR;
            return;
        case HCI_INIT_W4_SEND_RESET_ST_WARM_BOOT:
            btstack->hci->substate = HCI_INIT_READ_BD_ADDR;
            return;
#endif
        case HCI_INIT_W4_READ_BD_ADDR:
            // only read buffer size if supported
            if (btstack->hci->local_supported_commands[0] & 0x01) {
                btstack->hci->substate = HCI_INIT_READ_BUFFER_SIZE;
                return;
            }
            // skipping read buffer size
            btstack->hci->substate = HCI_INIT_READ_LOCAL_SUPPORTED_FEATURES;
            return;
        case HCI_INIT_W4_SET_EVENT_MASK:
            // skip Classic init commands for LE only chipsets
            if (!hci_classic_supported(btstack)){
#ifdef ENABLE_BLE
                if (hci_le_supported(btstack)){
                    btstack->hci->substate = HCI_INIT_LE_READ_BUFFER_SIZE; // skip all classic command
                    return;
                }
#endif
                log_error("Neither BR/EDR nor LE supported");
                hci_init_done(btstack);
                return;
            }
            if (!gap_ssp_supported(btstack)){
                btstack->hci->substate = HCI_INIT_WRITE_PAGE_TIMEOUT;
                return;
            }
            break;
#ifdef ENABLE_BLE
        case HCI_INIT_W4_LE_READ_BUFFER_SIZE:
            // skip write le host if not supported (e.g. on LE only EM9301)
            if (btstack->hci->local_supported_commands[0] & 0x02) break;
            btstack->hci->substate = HCI_INIT_LE_SET_EVENT_MASK;
            return;

#ifdef ENABLE_LE_DATA_LENGTH_EXTENSION
        case HCI_INIT_W4_WRITE_LE_HOST_SUPPORTED:
            log_info("Supported commands %x", btstack->hci->local_supported_commands[0] & 0x30);
            if ((btstack->hci->local_supported_commands[0] & 0x30) == 0x30){
                btstack->hci->substate = HCI_INIT_LE_SET_EVENT_MASK;
                return;
            }
            // explicit fall through to reduce repetitions

#ifdef ENABLE_LE_CENTRAL
            btstack->hci->substate = HCI_INIT_READ_WHITE_LIST_SIZE;
#else
            hci_init_done();
#endif
            return;
#endif  /* ENABLE_LE_DATA_LENGTH_EXTENSION */

#endif  /* ENABLE_BLE */

        case HCI_INIT_W4_WRITE_INQUIRY_MODE:
            // skip write secure connections host support if not supported or disabled
            if (!btstack->hci->secure_connections_enable || (btstack->hci->local_supported_commands[1] & 0x02) == 0) {
                btstack->hci->substate = HCI_INIT_WRITE_SCAN_ENABLE;
                return;
            }
            break;

#ifdef ENABLE_SCO_OVER_HCI
        case HCI_INIT_W4_WRITE_SCAN_ENABLE:
            // skip write synchronous flow control if not supported
            if (btstack->hci->local_supported_commands[0] & 0x04) break;
            btstack->hci->substate = HCI_INIT_W4_WRITE_SYNCHRONOUS_FLOW_CONTROL_ENABLE;

            /* fall through */

        case HCI_INIT_W4_WRITE_SYNCHRONOUS_FLOW_CONTROL_ENABLE:
            // skip write default erroneous data reporting if not supported
            if (btstack->hci->local_supported_commands[0] & 0x08) break;
            btstack->hci->substate = HCI_INIT_W4_WRITE_DEFAULT_ERRONEOUS_DATA_REPORTING;

            /* fall through */

        case HCI_INIT_W4_WRITE_DEFAULT_ERRONEOUS_DATA_REPORTING:
            // skip bcm set sco pcm config on non-Broadcom chipsets
            if (btstack->hci->manufacturer == BLUETOOTH_COMPANY_ID_BROADCOM_CORPORATION) break;
            btstack->hci->substate = HCI_INIT_W4_BCM_WRITE_SCO_PCM_INT;

            /* fall through */

        case HCI_INIT_W4_BCM_WRITE_SCO_PCM_INT:
            if (!hci_le_supported()){
                // SKIP LE init for Classic only configuration
                hci_init_done();
                return;
            }
            break;

#else /* !ENABLE_SCO_OVER_HCI */

        case HCI_INIT_W4_WRITE_SCAN_ENABLE:
#ifdef ENABLE_BLE
            if (hci_le_supported(btstack)){
                btstack->hci->substate = HCI_INIT_LE_READ_BUFFER_SIZE;
                return;
            }
#endif
            // SKIP LE init for Classic only configuration
            hci_init_done(btstack);
            return;
#endif /* ENABLE_SCO_OVER_HCI */

// avoid compile error due to duplicate cases: HCI_INIT_W4_BCM_WRITE_SCO_PCM_INT == HCI_INIT_DONE-1
#if defined(ENABLE_BLE) || defined(ENABLE_LE_DATA_LENGTH_EXTENSION) || defined(ENABLE_LE_CENTRAL)
        // Response to command before init done state -> init done
        case (HCI_INIT_DONE-1):
            hci_init_done(btstack);
            return;
#endif

        default:
            break;
    }
    hci_initializing_next_state(btstack);
}

static void hci_handle_connection_failed(btstack_state_t *btstack, hci_connection_t * conn, uint8_t status){
    log_info("Outgoing connection to %s failed", bd_addr_to_str(conn->address));
    bd_addr_t bd_address;
    (void)memcpy(&bd_address, conn->address, 6);

#ifdef ENABLE_CLASSIC
    // cache needed data
    int notify_dedicated_bonding_failed = conn->bonding_flags & BONDING_DEDICATED;
#endif

    // connection failed, remove entry
    btstack_linked_list_remove(&btstack->hci->connections, (btstack_linked_item_t *) conn);
    btstack_memory_hci_connection_free( conn );

#ifdef ENABLE_CLASSIC
    // notify client if dedicated bonding
    if (notify_dedicated_bonding_failed){
        log_info("hci notify_dedicated_bonding_failed");
        hci_emit_dedicated_bonding_result(btstack, bd_address, status);
    }

    // if authentication error, also delete link key
    if (status == ERROR_CODE_AUTHENTICATION_FAILURE) {
        gap_drop_link_key_for_bd_addr(btstack, bd_address);
    }
#endif
}

static void handle_event_for_current_stack_state(btstack_state_t *btstack, const uint8_t * packet, uint16_t size) {
    einstein_log(90, __func__, __LINE__, "%d", btstack->hci->substate);
    // handle BT initialization
    if (btstack->hci->state == HCI_STATE_INITIALIZING) {
        hci_initializing_event_handler(btstack, packet, size);
    }

    // help with BT sleep
    if ((btstack->hci->state == HCI_STATE_FALLING_ASLEEP)
        && (btstack->hci->substate == HCI_FALLING_ASLEEP_W4_WRITE_SCAN_ENABLE)
        && HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_write_scan_enable)) {
        hci_initializing_next_state(btstack);
    }
}

void event_handler(btstack_state_t *btstack, uint8_t *packet, int size){

    uint16_t event_length = packet[1];

    // assert packet is complete
    if (size != (event_length + 2)){
        log_error("event_handler called with packet of wrong size %d, expected %u => dropping packet", size, event_length + 2);
        return;
    }

    bd_addr_t addr;
    bd_addr_type_t addr_type;
    hci_con_handle_t handle;
    hci_connection_t * conn;
    int i;
    int create_connection_cmd;

#ifdef ENABLE_CLASSIC
    uint8_t link_type;
#endif

    // log_info("HCI:EVENT:%02x", hci_event_packet_get_type(packet));

    switch (hci_event_packet_get_type(packet)) {

        case HCI_EVENT_COMMAND_COMPLETE:
            // get num cmd packets - limit to 1 to reduce complexity
            btstack->hci->num_cmd_packets = packet[2] ? 1 : 0;
            einstein_log(90, __func__, __LINE__, "%d", btstack->hci->num_cmd_packets);

            if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_local_name)){
            }
            einstein_here(90, __func__, __LINE__);
            if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_local_name)){
                einstein_here(90, __func__, __LINE__);
                if (packet[5]) break;
                // terminate, name 248 chars
                packet[6+248] = 0;
                log_info("local name: %s", &packet[6]);
            }
            else if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_buffer_size)){
                einstein_here(90, __func__, __LINE__);
                // "The HC_ACL_Data_Packet_Length return parameter will be used to determine the size of the L2CAP segments contained in ACL Data Packets"
                if (btstack->hci->state == HCI_STATE_INITIALIZING){
                    uint16_t acl_len = little_endian_read_16(packet, 6);
                    uint16_t sco_len = packet[8];

                    // determine usable ACL/SCO payload size
                    btstack->hci->acl_data_packet_length = btstack_min(acl_len, HCI_ACL_PAYLOAD_SIZE);
                    btstack->hci->sco_data_packet_length = btstack_min(sco_len, HCI_ACL_PAYLOAD_SIZE);

                    btstack->hci->acl_packets_total_num  = little_endian_read_16(packet, 9);
                    btstack->hci->sco_packets_total_num  = little_endian_read_16(packet, 11);

                    log_info("hci_read_buffer_size: ACL size module %u -> used %u, count %u / SCO size %u, count %u",
                             acl_len, btstack->hci->acl_data_packet_length, btstack->hci->acl_packets_total_num,
                             btstack->hci->sco_data_packet_length, btstack->hci->sco_packets_total_num);
                }
            }
            else if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_rssi)){
                einstein_here(90, __func__, __LINE__);
                if (packet[5] == 0){
                    uint8_t event[5];
                    event[0] = GAP_EVENT_RSSI_MEASUREMENT;
                    event[1] = 3;
                    (void)memcpy(&event[2], &packet[6], 3);
                    hci_emit_event(btstack, event, sizeof(event), 1);
                }
            }
#ifdef ENABLE_BLE
            else if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_le_read_buffer_size)){
                einstein_here(90, __func__, __LINE__);
                btstack->hci->le_data_packets_length = little_endian_read_16(packet, 6);
                btstack->hci->le_acl_packets_total_num  = packet[8];
                // determine usable ACL payload size
                if (HCI_ACL_PAYLOAD_SIZE < btstack->hci->le_data_packets_length){
                    btstack->hci->le_data_packets_length = HCI_ACL_PAYLOAD_SIZE;
                }
                log_info("hci_le_read_buffer_size: size %u, count %u", btstack->hci->le_data_packets_length, btstack->hci->le_acl_packets_total_num);
            }
#endif
#ifdef ENABLE_LE_DATA_LENGTH_EXTENSION
            else if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_le_read_maximum_data_length)){
                einstein_here(90, __func__, __LINE__);
                btstack->hci->le_supported_max_tx_octets = little_endian_read_16(packet, 6);
                btstack->hci->le_supported_max_tx_time = little_endian_read_16(packet, 8);
                log_info("hci_le_read_maximum_data_length: tx octets %u, tx time %u us", btstack->hci->le_supported_max_tx_octets, btstack->hci->le_supported_max_tx_time);
            }
#endif
#ifdef ENABLE_LE_CENTRAL
            else if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_le_read_white_list_size)){
                einstein_here(90, __func__, __LINE__);
                btstack->hci->le_whitelist_capacity = packet[6];
                log_info("hci_le_read_white_list_size: size %u", btstack->hci->le_whitelist_capacity);
            }
#endif
            else if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_bd_addr)) {
                einstein_here(90, __func__, __LINE__);
                reverse_bd_addr(&packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE + 1],
				btstack->hci->local_bd_addr);
                log_info("Local Address, Status: 0x%02x: Addr: %s",
                    packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE], bd_addr_to_str(btstack->hci->local_bd_addr));
#ifdef ENABLE_CLASSIC
                if (btstack->hci->link_key_db){
                    btstack->hci->link_key_db->set_local_bd_addr(btstack->hci->local_bd_addr);
                }
#endif
            }
#ifdef ENABLE_CLASSIC
            else if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_write_scan_enable)){
                einstein_here(90, __func__, __LINE__);
                hci_emit_discoverable_enabled(btstack, btstack->hci->discoverable);
            }
            else if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_inquiry_cancel)){
                einstein_here(90, __func__, __LINE__);
                if (btstack->hci->inquiry_state == GAP_INQUIRY_STATE_W4_CANCELLED){
                    btstack->hci->inquiry_state = GAP_INQUIRY_STATE_IDLE;
                    uint8_t event[] = { GAP_EVENT_INQUIRY_COMPLETE, 1, 0};
                    hci_emit_event(btstack, event, sizeof(event), 1);
                }
            }
#endif

            // Note: HCI init checks
            else if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_local_supported_features)){
                einstein_here(90, __func__, __LINE__);
                (void)memcpy(btstack->hci->local_supported_features,
			     &packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE + 1],
			     8);

#ifdef ENABLE_CLASSIC
                // determine usable ACL packet types based on host buffer size and supported features
                btstack->hci->packet_types = hci_acl_packet_types_for_buffer_size_and_local_features(HCI_ACL_PAYLOAD_SIZE, &btstack->hci->local_supported_features[0]);
                log_info("Packet types %04x, eSCO %u", btstack->hci->packet_types, hci_extended_sco_link_supported(btstack));
#endif
                // Classic/LE
                log_info("BR/EDR support %u, LE support %u", hci_classic_supported(btstack), hci_le_supported());
            }
            else if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_local_version_information)){
                einstein_here(90, __func__, __LINE__);
                // btstack->hci->hci_version    = little_endian_read_16(packet, 4);
                // btstack->hci->hci_revision   = little_endian_read_16(packet, 6);
                uint16_t manufacturer = little_endian_read_16(packet, 10);
                // map Cypress to Broadcom
                if (manufacturer  == BLUETOOTH_COMPANY_ID_CYPRESS_SEMICONDUCTOR){
                    log_info("Treat Cypress as Broadcom");
                    manufacturer = BLUETOOTH_COMPANY_ID_BROADCOM_CORPORATION;
                    little_endian_store_16(packet, 10, manufacturer);
                }
                btstack->hci->manufacturer = manufacturer;
                // btstack->hci->lmp_version    = little_endian_read_16(packet, 8);
                // btstack->hci->lmp_subversion = little_endian_read_16(packet, 12);
                log_info("Manufacturer: 0x%04x", btstack->hci->manufacturer);
            }
            else if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_local_supported_commands)){
                einstein_here(90, __func__, __LINE__);
                btstack->hci->local_supported_commands[0] =
                    ((packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE+1+14] & 0x80) >> 7) |  // bit 0 = Octet 14, bit 7 / Read Buffer Size
                    ((packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE+1+24] & 0x40) >> 5) |  // bit 1 = Octet 24, bit 6 / Write Le Host Supported
                    ((packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE+1+10] & 0x10) >> 2) |  // bit 2 = Octet 10, bit 4 / Write Synchronous Flow Control Enable
                    ((packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE+1+18] & 0x08)     ) |  // bit 3 = Octet 18, bit 3 / Write Default Erroneous Data Reporting
                    ((packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE+1+34] & 0x01) << 4) |  // bit 4 = Octet 34, bit 0 / LE Write Suggested Default Data Length
                    ((packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE+1+35] & 0x08) << 2) |  // bit 5 = Octet 35, bit 3 / LE Read Maximum Data Length
                    ((packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE+1+35] & 0x20) << 1) |  // bit 6 = Octet 35, bit 5 / LE Set Default PHY
                    ((packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE+1+20] & 0x10) << 3);   // bit 7 = Octet 20, bit 4 / Read Encryption Key Size
                btstack->hci->local_supported_commands[1] =
                    ((packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE+1+ 2] & 0x40) >> 6) |  // bit 8 = Octet  2, bit 6 / Read Remote Extended Features
                    ((packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE+1+32] & 0x08) >> 2);   // bit 9 = Octet 32, bit 3 / Write Secure Connections Host
                log_info("Local supported commands summary %02x - %02x", btstack->hci->local_supported_commands[0],  btstack->hci->local_supported_commands[1]);
            }
#ifdef ENABLE_CLASSIC
            else if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_write_synchronous_flow_control_enable)){
                einstein_here(90, __func__, __LINE__);
                if (packet[5] == 0){
                    btstack->hci->synchronous_flow_control_enabled = 1;
                }
            }
            else if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_encryption_key_size)){
                einstein_here(90, __func__, __LINE__);
                uint8_t status = packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE];
                handle = little_endian_read_16(packet, OFFSET_OF_DATA_IN_COMMAND_COMPLETE+1);
                conn   = hci_connection_for_handle(btstack, handle);
                if (!conn) break;
                if (status == 0){
                    uint8_t key_size = packet[OFFSET_OF_DATA_IN_COMMAND_COMPLETE+3];
                    log_info("Handle %x04x key Size: %u", handle, key_size);
                    conn->encryption_key_size = key_size;
                } else {
                    log_info("Read Encryption Key Size failed -> assuming insecure connection with key size of 1");
                    conn->encryption_key_size = 1;
                }
                conn->authentication_flags |= CONNECTION_ENCRYPTED;
                hci_emit_security_level(btstack, handle, gap_security_level_for_connection(btstack, conn));
            }
#endif
            einstein_here(90, __func__, __LINE__);
            break;

        case HCI_EVENT_COMMAND_STATUS:
            // get num cmd packets - limit to 1 to reduce complexity
            btstack->hci->num_cmd_packets = packet[3] ? 1 : 0;

            // check command status to detected failed outgoing connections
            create_connection_cmd = 0;
#ifdef ENABLE_CLASSIC
            if (HCI_EVENT_IS_COMMAND_STATUS(packet, hci_create_connection)){
                create_connection_cmd = 1;
            }
#endif
#ifdef ENABLE_LE_CENTRAL
            if (HCI_EVENT_IS_COMMAND_STATUS(packet, hci_le_create_connection)){
                create_connection_cmd = 1;
            }
#endif
            if (create_connection_cmd) {
                uint8_t status = hci_event_command_status_get_status(packet);
                conn = hci_connection_for_bd_addr_and_type(btstack, btstack->hci->outgoing_addr, btstack->hci->outgoing_addr_type);
                log_info("command status (create connection), status %x, connection %p, addr %s, type %x", status, conn, bd_addr_to_str(btstack->hci->outgoing_addr), btstack->hci->outgoing_addr_type);

                // reset outgoing address info
                memset(btstack->hci->outgoing_addr, 0, 6);
                btstack->hci->outgoing_addr_type = BD_ADDR_TYPE_UNKNOWN;

                // error => outgoing connection failed
                if ((conn != NULL) && (status != 0)){
                    hci_handle_connection_failed(btstack, conn, status);
                }
            }
            break;

        case HCI_EVENT_NUMBER_OF_COMPLETED_PACKETS:{
            einstein_here(90, __func__, __LINE__);
            if (size < 3) return;
            uint16_t num_handles = packet[2];
            if (size != (3 + num_handles * 4)) return;
            uint16_t offset = 3;
            for (i=0; i<num_handles;i++){
                handle = little_endian_read_16(packet, offset) & 0x0fff;
                offset += 2;
                uint16_t num_packets = little_endian_read_16(packet, offset);
                offset += 2;

                conn = hci_connection_for_handle(btstack, handle);
                if (!conn){
                    log_error("hci_number_completed_packet lists unused con handle %u", handle);
                    continue;
                }

                if (conn->num_packets_sent >= num_packets){
                    conn->num_packets_sent -= num_packets;
                } else {
                    log_error("hci_number_completed_packets, more packet slots freed then sent.");
                    conn->num_packets_sent = 0;
                }
                // log_info("hci_number_completed_packet %u processed for handle %u, outstanding %u", num_packets, handle, conn->num_packets_sent);

#ifdef ENABLE_CLASSIC
                // For SCO, we do the can_send_now_check here
                hci_notify_if_sco_can_send_now(btstack);
#endif
            }
            break;
        }

#ifdef ENABLE_CLASSIC
        case HCI_EVENT_INQUIRY_COMPLETE:
            if (btstack->hci->inquiry_state == GAP_INQUIRY_STATE_ACTIVE){
                btstack->hci->inquiry_state = GAP_INQUIRY_STATE_IDLE;
                uint8_t event[] = { GAP_EVENT_INQUIRY_COMPLETE, 1, 0};
                hci_emit_event(btstack, event, sizeof(event), 1);
            }
            break;
        case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
            if (btstack->hci->remote_name_state == GAP_REMOTE_NAME_STATE_W4_COMPLETE){
                btstack->hci->remote_name_state = GAP_REMOTE_NAME_STATE_IDLE;
            }
            break;
        case HCI_EVENT_CONNECTION_REQUEST:
            reverse_bd_addr(&packet[2], addr);
            if (btstack->hci->gap_classic_accept_callback != NULL){
                if ((*btstack->hci->gap_classic_accept_callback)(addr) == 0){
                    btstack->hci->decline_reason = ERROR_CODE_CONNECTION_REJECTED_DUE_TO_UNACCEPTABLE_BD_ADDR;
                    bd_addr_copy(btstack->hci->decline_addr, addr);
                    break;
                }
            }

            // TODO: eval COD 8-10
            link_type = packet[11];
            log_info("Connection_incoming: %s, type %u", bd_addr_to_str(addr), link_type);
            addr_type = (link_type == 1) ? BD_ADDR_TYPE_ACL : BD_ADDR_TYPE_SCO;
            conn = hci_connection_for_bd_addr_and_type(btstack, addr, addr_type);
            if (!conn) {
                conn = create_connection_for_bd_addr_and_type(btstack, addr, addr_type);
            }
            if (!conn) {
                // CONNECTION REJECTED DUE TO LIMITED RESOURCES (0X0D)
                btstack->hci->decline_reason = ERROR_CODE_CONNECTION_REJECTED_DUE_TO_LIMITED_RESOURCES;
                bd_addr_copy(btstack->hci->decline_addr, addr);
                break;
            }
            conn->role  = HCI_ROLE_SLAVE;
            conn->state = RECEIVED_CONNECTION_REQUEST;
            // store info about eSCO
            if (link_type == 0x02){
                conn->remote_supported_feature_eSCO = 1;
            }
            hci_run(btstack);
            break;

        case HCI_EVENT_CONNECTION_COMPLETE:
            // Connection management
            reverse_bd_addr(&packet[5], addr);
            log_info("Connection_complete (status=%u) %s", packet[2], bd_addr_to_str(addr));
            addr_type = BD_ADDR_TYPE_ACL;
            conn = hci_connection_for_bd_addr_and_type(btstack, addr, addr_type);
            if (conn) {
                if (!packet[2]){
                    conn->state = OPEN;
                    conn->con_handle = little_endian_read_16(packet, 3);

                    // queue get remote feature
                    conn->bonding_flags |= BONDING_REQUEST_REMOTE_FEATURES;

                    // queue set supervision timeout if we're master
                    if ((btstack->hci->link_supervision_timeout != 0) && (conn->role == HCI_ROLE_MASTER)){
                        connectionSetAuthenticationFlags(conn, WRITE_SUPERVISION_TIMEOUT);
                    }

                    // restart timer
                    btstack_run_loop_set_timer(btstack, &conn->timeout, HCI_CONNECTION_TIMEOUT_MS);
                    btstack_run_loop_add_timer(btstack, &conn->timeout);

                    log_info("New connection: handle %u, %s", conn->con_handle, bd_addr_to_str(conn->address));

                    hci_emit_nr_connections_changed(btstack);
                } else {
                    // connection failed
                    hci_handle_connection_failed(btstack, conn, packet[2]);
                }
            }
            break;

        case HCI_EVENT_SYNCHRONOUS_CONNECTION_COMPLETE:
            reverse_bd_addr(&packet[5], addr);
            log_info("Synchronous Connection Complete (status=%u) %s", packet[2], bd_addr_to_str(addr));
            if (packet[2]){
                // connection failed
                break;
            }
            conn = hci_connection_for_bd_addr_and_type(btstack, addr, BD_ADDR_TYPE_SCO);
            if (!conn) {
                conn = create_connection_for_bd_addr_and_type(btstack, addr, BD_ADDR_TYPE_SCO);
            }
            if (!conn) {
                break;
            }
            conn->state = OPEN;
            conn->con_handle = little_endian_read_16(packet, 3);

#ifdef ENABLE_SCO_OVER_HCI
            // update SCO
            if (conn->address_type == BD_ADDR_TYPE_SCO && btstack->hci->hci_transport && btstack->hci->hci_transport->set_sco_config){
                btstack->hci->hci_transport->set_sco_config(btstack->hci->sco_voice_setting_active, hci_number_sco_connections());
            }
            // trigger can send now
            if (hci_have_usb_transport()){
                btstack->hci->sco_can_send_now = 1;
            }
#endif
            break;

        case HCI_EVENT_READ_REMOTE_SUPPORTED_FEATURES_COMPLETE:
            handle = little_endian_read_16(packet, 3);
            conn = hci_connection_for_handle(btstack, handle);
            if (!conn) break;
            if (!packet[2]){
                uint8_t * features = &packet[5];
                if (features[6] & (1 << 3)){
                    conn->bonding_flags |= BONDING_REMOTE_SUPPORTS_SSP;
                }
                if (features[3] & (1<<7)){
                    conn->remote_supported_feature_eSCO = 1;
                }
            }
            conn->bonding_flags |= BONDING_RECEIVED_REMOTE_FEATURES;
            log_info("HCI_EVENT_READ_REMOTE_SUPPORTED_FEATURES_COMPLETE, bonding flags %x, eSCO %u", conn->bonding_flags, conn->remote_supported_feature_eSCO);
            if (conn->bonding_flags & BONDING_DEDICATED){
                conn->bonding_flags |= BONDING_SEND_AUTHENTICATE_REQUEST;
            }
            break;

        case HCI_EVENT_LINK_KEY_REQUEST:
            log_info("HCI_EVENT_LINK_KEY_REQUEST");
            hci_add_connection_flags_for_flipped_bd_addr(btstack, &packet[2], RECV_LINK_KEY_REQUEST);
            // non-bondable mode: link key negative reply will be sent by HANDLE_LINK_KEY_REQUEST
            if (btstack->hci->bondable && !btstack->hci->link_key_db) break;
            hci_add_connection_flags_for_flipped_bd_addr(btstack, &packet[2], HANDLE_LINK_KEY_REQUEST);
            hci_run(btstack);
            // request handled by hci_run(btstack) as HANDLE_LINK_KEY_REQUEST gets set
            return;

        case HCI_EVENT_LINK_KEY_NOTIFICATION: {
            reverse_bd_addr(&packet[2], addr);
            conn = hci_connection_for_bd_addr_and_type(btstack, addr, BD_ADDR_TYPE_ACL);
            if (!conn) break;
            conn->authentication_flags |= RECV_LINK_KEY_NOTIFICATION;
            link_key_type_t link_key_type = (link_key_type_t)packet[24];
            // Change Connection Encryption keeps link key type
            if (link_key_type != CHANGED_COMBINATION_KEY){
                conn->link_key_type = link_key_type;
            }
            gap_store_link_key_for_bd_addr(btstack, addr, &packet[8], conn->link_key_type);
            // still forward event to allow dismiss of pairing dialog
            break;
        }

        case HCI_EVENT_PIN_CODE_REQUEST:
            hci_add_connection_flags_for_flipped_bd_addr(btstack, &packet[2], LEGACY_PAIRING_ACTIVE);
            // non-bondable mode: pin code negative reply will be sent
            if (!btstack->hci->bondable){
                hci_add_connection_flags_for_flipped_bd_addr(btstack, &packet[2], DENY_PIN_CODE_REQUEST);
                hci_run(btstack);
                return;
            }
            // PIN CODE REQUEST means the link key request didn't succee -> delete stored link key
            if (!btstack->hci->link_key_db) break;
            hci_event_pin_code_request_get_bd_addr(packet, addr);
            btstack->hci->link_key_db->delete_link_key(addr);
            break;

        case HCI_EVENT_IO_CAPABILITY_REQUEST:
            hci_add_connection_flags_for_flipped_bd_addr(btstack, &packet[2], RECV_IO_CAPABILITIES_REQUEST);
            hci_add_connection_flags_for_flipped_bd_addr(btstack, &packet[2], SEND_IO_CAPABILITIES_REPLY);
            break;

        case HCI_EVENT_USER_CONFIRMATION_REQUEST:
            hci_add_connection_flags_for_flipped_bd_addr(btstack, &packet[2], SSP_PAIRING_ACTIVE);
            if (!btstack->hci->ssp_auto_accept) break;
            hci_add_connection_flags_for_flipped_bd_addr(btstack, &packet[2], SEND_USER_CONFIRM_REPLY);
            break;

        case HCI_EVENT_USER_PASSKEY_REQUEST:
            hci_add_connection_flags_for_flipped_bd_addr(btstack, &packet[2], SSP_PAIRING_ACTIVE);
            if (!btstack->hci->ssp_auto_accept) break;
            hci_add_connection_flags_for_flipped_bd_addr(btstack, &packet[2], SEND_USER_PASSKEY_REPLY);
            break;
        case HCI_EVENT_MODE_CHANGE:
            handle = hci_event_mode_change_get_handle(packet);
            conn = hci_connection_for_handle(btstack, handle);
            if (!conn) break;
            conn->connection_mode = hci_event_mode_change_get_mode(packet);
            log_info("HCI_EVENT_MODE_CHANGE, handle 0x%04x, mode %u", handle, conn->connection_mode);
            break;
#endif

        case HCI_EVENT_ENCRYPTION_CHANGE:
            handle = hci_event_encryption_change_get_connection_handle(packet);
            conn = hci_connection_for_handle(btstack, handle);
            if (!conn) break;
            if (hci_event_encryption_change_get_status(packet) == 0) {
                uint8_t encryption_enabled = hci_event_encryption_change_get_encryption_enabled(packet);
                if (encryption_enabled){
                    if (hci_is_le_connection(conn)){
                        // For LE, we accept connection as encrypted
                        conn->authentication_flags |= CONNECTION_ENCRYPTED;
                    }
#ifdef ENABLE_CLASSIC
                    else {
                        // Detect Secure Connection -> Legacy Connection Downgrade Attack (BIAS)
                        bool sc_used_during_pairing = gap_secure_connection_for_link_key_type(conn->link_key_type) != 0;
                        bool connected_uses_aes_ccm = encryption_enabled == 2;
                        if (sc_used_during_pairing && !connected_uses_aes_ccm){
                            log_info("SC during pairing, but only E0 now -> abort");
                            conn->bonding_flags |= BONDING_DISCONNECT_SECURITY_BLOCK;
                            break;
                        }

                        if ((btstack->hci->local_supported_commands[0] & 0x80) != 0){
                            // For Classic, we need to validate encryption key size first, if possible (== supported by Controller)
                            conn->bonding_flags |= BONDING_SEND_READ_ENCRYPTION_KEY_SIZE;
                        } else {
                            // if not, pretend everything is perfect
                            conn->encryption_key_size = 16;
                            conn->authentication_flags |= CONNECTION_ENCRYPTED;
                            hci_emit_security_level(btstack, handle, gap_security_level_for_connection(btstack, conn));
                        }
                    }
#endif
                } else {
                    conn->authentication_flags &= ~CONNECTION_ENCRYPTED;
                }
            }

            break;

#ifdef ENABLE_CLASSIC
        case HCI_EVENT_AUTHENTICATION_COMPLETE_EVENT:
            handle = little_endian_read_16(packet, 3);
            conn = hci_connection_for_handle(btstack, handle);
            if (!conn) break;

            // dedicated bonding: send result and disconnect
            if (conn->bonding_flags & BONDING_DEDICATED){
                conn->bonding_flags &= ~BONDING_DEDICATED;
                conn->bonding_flags |= BONDING_DISCONNECT_DEDICATED_DONE;
                conn->bonding_status = packet[2];
                break;
            }

            if ((packet[2] == 0) && (gap_security_level_for_link_key_type(conn->link_key_type) >= conn->requested_security_level)){
                // link key sufficient for requested security
                conn->bonding_flags |= BONDING_SEND_ENCRYPTION_REQUEST;
                break;
            }
            // not enough
            hci_emit_security_level(btstack, handle, gap_security_level_for_connection(btstack, conn));
            break;
#endif

        // HCI_EVENT_DISCONNECTION_COMPLETE
        // has been split, to first notify stack before shutting connection down
        // see end of function, too.
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            if (packet[2]) break;   // status != 0
            handle = little_endian_read_16(packet, 3);
            // drop outgoing ACL fragments if it is for closed connection and release buffer if tx not active
            if (btstack->hci->acl_fragmentation_total_size > 0) {
                if (handle == READ_ACL_CONNECTION_HANDLE(btstack->hci->hci_packet_buffer)){
                    int release_buffer = btstack->hci->acl_fragmentation_tx_active == 0;
                    log_info("drop fragmented ACL data for closed connection, release buffer %u", release_buffer);
                    btstack->hci->acl_fragmentation_total_size = 0;
                    btstack->hci->acl_fragmentation_pos = 0;
                    if (release_buffer){
                        hci_release_packet_buffer(btstack);
                    }
                }
            }

            conn = hci_connection_for_handle(btstack, handle);
            if (!conn) break;
            // mark connection for shutdown
            conn->state = RECEIVED_DISCONNECTION_COMPLETE;

            // emit dedicatd bonding event
            if (conn->bonding_flags & BONDING_EMIT_COMPLETE_ON_DISCONNECT){
                hci_emit_dedicated_bonding_result(btstack, conn->address, conn->bonding_status);
            }

#ifdef ENABLE_BLE
#ifdef ENABLE_LE_PERIPHERAL
            // re-enable advertisements for le connections if active
            if (hci_is_le_connection(conn)){
                hci_reenable_advertisements_if_needed();
            }
#endif
#endif
            break;

        case HCI_EVENT_HARDWARE_ERROR:
            log_error("Hardware Error: 0x%02x", packet[2]);
            if (btstack->hci->hardware_error_callback){
                (*btstack->hci->hardware_error_callback)(packet[2]);
            } else {
                // if no special requests, just reboot stack
                hci_power_control_off(btstack);
                hci_power_control_on(btstack);
            }
            break;

#ifdef ENABLE_CLASSIC
        case HCI_EVENT_ROLE_CHANGE:
            if (packet[2]) break;   // status != 0
            reverse_bd_addr(&packet[3], addr);
            addr_type = BD_ADDR_TYPE_ACL;
            conn = hci_connection_for_bd_addr_and_type(btstack, addr, addr_type);
            if (!conn) break;
            conn->role = packet[9];
            break;
#endif

        case HCI_EVENT_TRANSPORT_PACKET_SENT:
            // release packet buffer only for asynchronous transport and if there are not further fragements
            if (hci_transport_synchronous(btstack)) {
                log_error("Synchronous HCI Transport shouldn't send HCI_EVENT_TRANSPORT_PACKET_SENT");
                return; // instead of break: to avoid re-entering hci_run(btstack)
            }
            btstack->hci->acl_fragmentation_tx_active = 0;
            if (btstack->hci->acl_fragmentation_total_size) break;
            hci_release_packet_buffer(btstack);

            // L2CAP receives this event via the hci_emit_event below

#ifdef ENABLE_CLASSIC
            // For SCO, we do the can_send_now_check here
            hci_notify_if_sco_can_send_now(btstack);
#endif
            break;

#ifdef ENABLE_CLASSIC
        case HCI_EVENT_SCO_CAN_SEND_NOW:
            // For SCO, we do the can_send_now_check here
            btstack->hci->sco_can_send_now = 1;
            hci_notify_if_sco_can_send_now(btstack);
            return;

        // explode inquriy results for easier consumption
        case HCI_EVENT_INQUIRY_RESULT:
        case HCI_EVENT_INQUIRY_RESULT_WITH_RSSI:
        case HCI_EVENT_EXTENDED_INQUIRY_RESPONSE:
            gap_inquiry_explode(btstack, packet, size);
            break;
#endif

#ifdef ENABLE_BLE
        case HCI_EVENT_LE_META:
            switch (packet[2]){
#ifdef ENABLE_LE_CENTRAL
                case HCI_SUBEVENT_LE_ADVERTISING_REPORT:
                    // log_info("advertising report received");
                    if (!btstack->hci->le_scanning_enabled) break;
                    le_handle_advertisement_report(btstack, packet, size);
                    break;
#endif
                case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
                    // Connection management
                    reverse_bd_addr(&packet[8], addr);
                    addr_type = (bd_addr_type_t)packet[7];
                    log_info("LE Connection_complete (status=%u) type %u, %s", packet[3], addr_type, bd_addr_to_str(addr));
                    conn = hci_connection_for_bd_addr_and_type(btstack, addr, addr_type);

#ifdef ENABLE_LE_CENTRAL
                    // if auto-connect, remove from whitelist in both roles
                    if (btstack->hci->le_connecting_state == LE_CONNECTING_WHITELIST){
                        hci_remove_from_whitelist(btstack, addr_type, addr);
                    }
                    // handle error: error is reported only to the initiator -> outgoing connection
                    if (packet[3]){

                        // handle cancelled outgoing connection
                        // "If the cancellation was successful then, after the Command Complete event for the LE_Create_Connection_Cancel command,
                        //  either an LE Connection Complete or an LE Enhanced Connection Complete event shall be generated.
                        //  In either case, the event shall be sent with the error code Unknown Connection Identifier (0x02)."
                        if (packet[3] == ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER){
                            conn = gap_get_outgoing_connection(btstack);
                        }

                        // outgoing connection establishment is done
                        btstack->hci->le_connecting_state = LE_CONNECTING_IDLE;
                        // remove entry
                        if (conn){
                            btstack_linked_list_remove(&btstack->hci->connections, (btstack_linked_item_t *) conn);
                            btstack_memory_hci_connection_free( conn );
                        }
                        break;
                    }
#endif
                    // on success, both hosts receive connection complete event
                    if (packet[6] == HCI_ROLE_MASTER){
#ifdef ENABLE_LE_CENTRAL
                        // if we're master, it was an outgoing connection and we're done with it
                        btstack->hci->le_connecting_state = LE_CONNECTING_IDLE;
#endif
                    } else {
#ifdef ENABLE_LE_PERIPHERAL
                        // if we're slave, it was an incoming connection, advertisements have stopped
                        btstack->hci->le_advertisements_active = 0;
#endif
                    }
                    // LE connections are auto-accepted, so just create a connection if there isn't one already
                    if (!conn){
                        conn = create_connection_for_bd_addr_and_type(btstack, addr, addr_type);
                    }
                    // no memory, sorry.
                    if (!conn){
                        break;
                    }

                    conn->state = OPEN;
                    conn->role  = packet[6];
                    conn->con_handle             = hci_subevent_le_connection_complete_get_connection_handle(packet);
                    conn->le_connection_interval = hci_subevent_le_connection_complete_get_conn_interval(packet);

#ifdef ENABLE_LE_PERIPHERAL
                    if (packet[6] == HCI_ROLE_SLAVE){
                        hci_reenable_advertisements_if_needed();
                    }
#endif

                    // TODO: store - role, peer address type, conn_interval, conn_latency, supervision timeout, master clock

                    // restart timer
                    // btstack_run_loop_set_timer(&conn->timeout, HCI_CONNECTION_TIMEOUT_MS);
                    // btstack_run_loop_add_timer(&conn->timeout);

                    log_info("New connection: handle %u, %s", conn->con_handle, bd_addr_to_str(conn->address));

                    hci_emit_nr_connections_changed(btstack);
                    break;

                // log_info("LE buffer size: %u, count %u", little_endian_read_16(packet,6), packet[8]);
                case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
                    handle = hci_subevent_le_connection_update_complete_get_connection_handle(packet);
                    conn = hci_connection_for_handle(btstack, handle);
                    if (!conn) break;
                    conn->le_connection_interval = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
                    break;

                case HCI_SUBEVENT_LE_REMOTE_CONNECTION_PARAMETER_REQUEST:
                    // connection
                    handle = hci_subevent_le_remote_connection_parameter_request_get_connection_handle(packet);
                    conn = hci_connection_for_handle(btstack, handle);
                    if (conn) {
                        // read arguments
                        uint16_t le_conn_interval_min   = hci_subevent_le_remote_connection_parameter_request_get_interval_min(packet);
                        uint16_t le_conn_interval_max   = hci_subevent_le_remote_connection_parameter_request_get_interval_max(packet);
                        uint16_t le_conn_latency        = hci_subevent_le_remote_connection_parameter_request_get_latency(packet);
                        uint16_t le_supervision_timeout = hci_subevent_le_remote_connection_parameter_request_get_timeout(packet);

                        // validate against current connection parameter range
                        le_connection_parameter_range_t existing_range;
                        gap_get_connection_parameter_range(btstack, &existing_range);
                        int update_parameter = gap_connection_parameter_range_included(&existing_range, le_conn_interval_min, le_conn_interval_max, le_conn_latency, le_supervision_timeout);
                        if (update_parameter){
                            conn->le_con_parameter_update_state = CON_PARAMETER_UPDATE_REPLY;
                            conn->le_conn_interval_min = le_conn_interval_min;
                            conn->le_conn_interval_max = le_conn_interval_max;
                            conn->le_conn_latency = le_conn_latency;
                            conn->le_supervision_timeout = le_supervision_timeout;
                        } else {
                            conn->le_con_parameter_update_state = CON_PARAMETER_UPDATE_DENY;
                        }
                    }
                    break;
#ifdef ENABLE_LE_LIMIT_ACL_FRAGMENT_BY_MAX_OCTETS
                case HCI_SUBEVENT_LE_DATA_LENGTH_CHANGE:
                    handle = hci_subevent_le_data_length_change_get_connection_handle(packet);
                    conn = hci_connection_for_handle(handle);
                    if (conn) {
                        conn->le_max_tx_octets = hci_subevent_le_data_length_change_get_max_tx_octets(packet);
                    }
                    break;
#endif
                default:
                    break;
            }
            break;
#endif
        case HCI_EVENT_VENDOR_SPECIFIC:
            // Vendor specific commands often create vendor specific event instead of num completed packets
            // To avoid getting stuck as num_cmds_packets is zero, reset it to 1 for controllers with this behaviour
            switch (btstack->hci->manufacturer){
                case BLUETOOTH_COMPANY_ID_CAMBRIDGE_SILICON_RADIO:
                    btstack->hci->num_cmd_packets = 1;
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    einstein_here(90, __func__, __LINE__);
    handle_event_for_current_stack_state(btstack, packet, size);

    // notify upper stack
	hci_emit_event(btstack, packet, size, 0);   // don't dump, already happened in packet handler

    // moved here to give upper stack a chance to close down everything with hci_connection_t intact
    if (hci_event_packet_get_type(packet) == HCI_EVENT_DISCONNECTION_COMPLETE){
        if (!packet[2]){
            handle = little_endian_read_16(packet, 3);
            hci_connection_t * aConn = hci_connection_for_handle(btstack, handle);
            if (aConn) {
                // discard connection if app did not trigger a reconnect in the event handler
                if (aConn->state == RECEIVED_DISCONNECTION_COMPLETE){
                    hci_shutdown_connection(btstack, aConn);
                }
            }
        }
    }

	// execute main loop
	hci_run(btstack);
}

#ifdef ENABLE_CLASSIC

static void sco_tx_timeout_handler(btstack_state_t *btstack, btstack_timer_source_t * ts);
static void sco_schedule_tx(btstack_state_t *btstack, hci_connection_t * conn);

static void sco_tx_timeout_handler(btstack_state_t *btstack, btstack_timer_source_t * ts){
    log_debug("SCO TX Timeout");
    hci_con_handle_t con_handle = (hci_con_handle_t) (uintptr_t) btstack_run_loop_get_timer_context(ts);
    hci_connection_t * conn = hci_connection_for_handle(btstack, con_handle);
    if (!conn) return;

    // trigger send
    conn->sco_tx_ready = 1;
    // extra packet if CVSD but SCO buffer is too short
    if (((btstack->hci->sco_voice_setting_active & 0x03) != 0x03) && (btstack->hci->sco_data_packet_length < 123)){
        conn->sco_tx_ready++;
    }
    hci_notify_if_sco_can_send_now(btstack);
}


#define SCO_TX_AFTER_RX_MS (6)

static void sco_schedule_tx(btstack_state_t *btstack, hci_connection_t * conn){

    uint32_t now = btstack_run_loop_get_time_ms(btstack);
    uint32_t sco_tx_ms = conn->sco_rx_ms + SCO_TX_AFTER_RX_MS;
    int time_delta_ms = sco_tx_ms - now;

    btstack_timer_source_t * timer = (conn->sco_rx_count & 1) ? &conn->timeout : &conn->timeout_sco;

    // log_error("SCO TX at %u in %u", (int) sco_tx_ms, time_delta_ms);
    btstack_run_loop_set_timer(btstack, timer, time_delta_ms);
    btstack_run_loop_set_timer_context(timer, (void *) (uintptr_t) conn->con_handle);
    btstack_run_loop_set_timer_handler(timer, &sco_tx_timeout_handler);
    btstack_run_loop_add_timer(btstack, timer);
}

static void sco_handler(btstack_state_t *btstack, uint8_t * packet, uint16_t size){
    // lookup connection struct
    hci_con_handle_t con_handle = READ_SCO_CONNECTION_HANDLE(packet);
    hci_connection_t * conn     = hci_connection_for_handle(btstack, con_handle);
    if (!conn) return;

    // CSR 8811 prefixes 60 byte SCO packet in transparent mode with 20 zero bytes -> skip first 20 payload bytes
    if (btstack->hci->manufacturer == BLUETOOTH_COMPANY_ID_CAMBRIDGE_SILICON_RADIO){
        if ((size == 83) && ((btstack->hci->sco_voice_setting_active & 0x03) == 0x03)){
            packet[2] = 0x3c;
            memmove(&packet[3], &packet[23], 63);
            size = 63;
        }
    }

    if (hci_have_usb_transport(btstack)){
        // Nothing to do
    } else {
        // log_debug("sco flow %u, handle 0x%04x, packets sent %u, bytes send %u", btstack->hci->synchronous_flow_control_enabled, (int) con_handle, conn->num_packets_sent, conn->num_sco_bytes_sent);
        if (btstack->hci->synchronous_flow_control_enabled == 0){
            uint32_t now = btstack_run_loop_get_time_ms(btstack);

            if (!conn->sco_rx_valid){
                // ignore first 10 packets
                conn->sco_rx_count++;
                // log_debug("sco rx count %u", conn->sco_rx_count);
                if (conn->sco_rx_count == 10) {
                    // use first timestamp as is and pretent it just started
                    conn->sco_rx_ms = now;
                    conn->sco_rx_valid = 1;
                    conn->sco_rx_count = 0;
                    sco_schedule_tx(btstack, conn);
                }
            } else {
                // track expected arrival timme
                conn->sco_rx_count++;
                conn->sco_rx_ms += 7;
                int delta = (int32_t) (now - conn->sco_rx_ms);
                if (delta > 0){
                    conn->sco_rx_ms++;
                }
                // log_debug("sco rx %u", conn->sco_rx_ms);
                sco_schedule_tx(btstack, conn);
            }
        }
    }
    // deliver to app
    if (btstack->hci->sco_packet_handler) {
        btstack->hci->sco_packet_handler(btstack, HCI_SCO_DATA_PACKET, 0, packet, size);
    }

#ifdef ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
    conn->num_packets_completed++;
    btstack->hci->host_completed_packets = 1;
    hci_run(btstack);
#endif
}
#endif

static void packet_handler(btstack_state_t *btstack, uint8_t packet_type, uint8_t *packet, uint16_t size){
    hci_dump_packet(packet_type, 1, packet, size);
    switch (packet_type) {
        case HCI_EVENT_PACKET:
            event_handler(btstack, packet, size);
            break;
        case HCI_ACL_DATA_PACKET:
            acl_handler(btstack, packet, size);
            break;
#ifdef ENABLE_CLASSIC
        case HCI_SCO_DATA_PACKET:
            sco_handler(btstack, packet, size);
            break;
#endif
        default:
            break;
    }
}

/**
 * @brief Add event packet handler.
 */
void hci_add_event_handler(btstack_state_t *btstack, btstack_packet_callback_registration_t * callback_handler){
    btstack_linked_list_add_tail(&btstack->hci->event_handlers, (btstack_linked_item_t*) callback_handler);
}


/** Register HCI packet handlers */
void hci_register_acl_packet_handler(btstack_state_t *btstack, btstack_packet_handler_t handler){
    btstack->hci->acl_packet_handler = handler;
}

#ifdef ENABLE_CLASSIC
/**
 * @brief Registers a packet handler for SCO data. Used for HSP and HFP profiles.
 */
void hci_register_sco_packet_handler(btstack_state_t *btstack, btstack_packet_handler_t handler){
    btstack->hci->sco_packet_handler = handler;
}
#endif

static void hci_state_reset(btstack_state_t *btstack){
    // no connections yet
    btstack->hci->connections = NULL;

    // keep discoverable/connectable as this has been requested by the client(s)
    // btstack->hci->discoverable = 0;
    // btstack->hci->connectable = 0;
    // btstack->hci->bondable = 1;
    // btstack->hci->own_addr_type = 0;

    // buffer is free
    btstack->hci->hci_packet_buffer_reserved = 0;

    // no pending cmds
    btstack->hci->decline_reason = 0;
    btstack->hci->new_scan_enable_value = 0xff;

    // LE
#ifdef ENABLE_BLE
    memset(btstack->hci->le_random_address, 0, 6);
    btstack->hci->le_random_address_set = 0;
#endif
#ifdef ENABLE_LE_CENTRAL
    btstack->hci->le_scanning_active  = 0;
    btstack->hci->le_scan_type = 0xff;
    btstack->hci->le_connecting_state = LE_CONNECTING_IDLE;
    btstack->hci->le_whitelist = 0;
    btstack->hci->le_whitelist_capacity = 0;
#endif
}

#ifdef ENABLE_CLASSIC
/**
 * @brief Configure Bluetooth hardware control. Has to be called before power on.
 */
void hci_set_link_key_db(btstack_state_t *btstack, btstack_link_key_db_t const * link_key_db){
    // store and open remote device db
    btstack->hci->link_key_db = link_key_db;
    if (btstack->hci->link_key_db) {
        btstack->hci->link_key_db->open();
    }
}
#endif

void hci_init(btstack_state_t * btstack, const hci_transport_t *transport, const void *config){

    btstack->hci = (hci_stack_t*) calloc(1, sizeof(hci_stack_t));
    btstack->hci->hci_packet_buffer_data = calloc(1, HCI_OUTGOING_PRE_BUFFER_SIZE + HCI_OUTGOING_PACKET_BUFFER_SIZE);

    // reference to use transport layer implementation
    btstack->hci->hci_transport = transport;

    // reference to used config
    btstack->hci->config = config;

    // setup pointer for outgoing packet buffer
    btstack->hci->hci_packet_buffer = &btstack->hci->hci_packet_buffer_data[HCI_OUTGOING_PRE_BUFFER_SIZE];

    // max acl payload size defined in config.h
    btstack->hci->acl_data_packet_length = HCI_ACL_PAYLOAD_SIZE;

    // register packet handlers with transport
    transport->register_packet_handler(btstack, &packet_handler);

    btstack->hci->state = HCI_STATE_OFF;

    // class of device
    btstack->hci->class_of_device = 0x007a020c; // Smartphone

    // bondable by default
    btstack->hci->bondable = 1;

#ifdef ENABLE_CLASSIC
    // classic name
    btstack->hci->local_name = default_classic_name;

    // Master slave policy
    btstack->hci->master_slave_policy = 1;

    // Allow Role Switch
    btstack->hci->allow_role_switch = 1;

    // Default / minimum security level = 2
    btstack->hci->gap_security_level = LEVEL_2;

    // Errata-11838 mandates 7 bytes for GAP Security Level 1-3, we use 16 as default
    btstack->hci->gap_required_encyrption_key_size = 16;
#endif

    // Secure Simple Pairing default: enable, no I/O capabilities, general bonding, mitm not required, auto accept
    btstack->hci->ssp_enable = 1;
    btstack->hci->ssp_io_capability = SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT;
    btstack->hci->ssp_authentication_requirement = SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_GENERAL_BONDING;
    btstack->hci->ssp_auto_accept = 1;

    // Secure Connections: enable (requires support from Controller)
    btstack->hci->secure_connections_enable = true;

    // voice setting - signed 16 bit pcm data with CVSD over the air
    btstack->hci->sco_voice_setting = 0x60;

#ifdef ENABLE_LE_CENTRAL
    // connection parameter to use for outgoing connections
    btstack->hci->le_connection_scan_interval = 0x0060;   // 60ms
    btstack->hci->le_connection_scan_window  = 0x0030;    // 30ms
    btstack->hci->le_connection_interval_min = 0x0008;    // 10 ms
    btstack->hci->le_connection_interval_max = 0x0018;    // 30 ms
    btstack->hci->le_connection_latency      = 4;         // 4
    btstack->hci->le_supervision_timeout     = 0x0048;    // 720 ms
    btstack->hci->le_minimum_ce_length       = 2;         // 1.25 ms
    btstack->hci->le_maximum_ce_length       = 0x0030;    // 30 ms

    // default LE Scanning
    btstack->hci->le_scan_interval = 0x1e0;
    btstack->hci->le_scan_window   =  0x30;
#endif

#ifdef ENABLE_LE_PERIPHERAL
    btstack->hci->le_max_number_peripheral_connections = 1; // only single connection as peripheral
#endif

    // connection parameter range used to answer connection parameter update requests in l2cap
    btstack->hci->le_connection_parameter_range.le_conn_interval_min =          6;
    btstack->hci->le_connection_parameter_range.le_conn_interval_max =       3200;
    btstack->hci->le_connection_parameter_range.le_conn_latency_min =           0;
    btstack->hci->le_connection_parameter_range.le_conn_latency_max =         500;
    btstack->hci->le_connection_parameter_range.le_supervision_timeout_min =   10;
    btstack->hci->le_connection_parameter_range.le_supervision_timeout_max = 3200;

    hci_state_reset(btstack);
}

/**
 * @brief Configure Bluetooth chipset driver. Has to be called before power on, or right after receiving the local version information
 */
void hci_set_chipset(btstack_state_t *btstack, const btstack_chipset_t *chipset_driver){
    btstack->hci->chipset = chipset_driver;

    // reset chipset driver - init is also called on power_up
    if (btstack->hci->chipset && btstack->hci->chipset->init){
        btstack->hci->chipset->init(btstack->hci->config);
    }
}

/**
 * @brief Configure Bluetooth hardware control. Has to be called after hci_init() but before power on.
 */
void hci_set_control(btstack_state_t *btstack, const btstack_control_t *hardware_control){
    // references to used control implementation
    btstack->hci->control = hardware_control;
    // init with transport config
    hardware_control->init(btstack->hci->config);
}

void hci_close(btstack_state_t *btstack){
    // close remote device db
    if (btstack->hci->link_key_db) {
        btstack->hci->link_key_db->close();
    }

    btstack_linked_list_iterator_t lit;
    btstack_linked_list_iterator_init(&lit, &btstack->hci->connections);
    while (btstack_linked_list_iterator_has_next(&lit)){
        // cancel all l2cap connections by emitting dicsconnection complete before shutdown (free) connection
        hci_connection_t * connection = (hci_connection_t*) btstack_linked_list_iterator_next(&lit);
        hci_emit_disconnection_complete(btstack, connection->con_handle, 0x16); // terminated by local host
        hci_shutdown_connection(btstack, connection);
    }

    hci_power_control(btstack, HCI_POWER_OFF);

    free(btstack->hci);
}

#ifdef ENABLE_CLASSIC
void gap_set_required_encryption_key_size(btstack_state_t *btstack, uint8_t encryption_key_size){
    // validate ranage and set
    if (encryption_key_size < 7)  return;
    if (encryption_key_size > 16) return;
    btstack->hci->gap_required_encyrption_key_size = encryption_key_size;
}

void gap_set_security_level(btstack_state_t *btstack, gap_security_level_t security_level){
    btstack->hci->gap_security_level = security_level;
}

gap_security_level_t gap_get_security_level(btstack_state_t *btstack){
    return btstack->hci->gap_security_level;
}
#endif

#ifdef ENABLE_CLASSIC
void gap_set_class_of_device(btstack_state_t *btstack, uint32_t class_of_device){
    btstack->hci->class_of_device = class_of_device;
}

void gap_set_default_link_policy_settings(btstack_state_t *btstack, uint16_t default_link_policy_settings){
    btstack->hci->default_link_policy_settings = default_link_policy_settings;
}

void gap_set_allow_role_switch(btstack_state_t *btstack, bool allow_role_switch){
    btstack->hci->allow_role_switch = allow_role_switch ? 1 : 0;
}

uint8_t hci_get_allow_role_switch(btstack_state_t *btstack){
    return  btstack->hci->allow_role_switch;
}

void gap_set_link_supervision_timeout(btstack_state_t *btstack, uint16_t link_supervision_timeout){
    btstack->hci->link_supervision_timeout = link_supervision_timeout;
}

void hci_disable_l2cap_timeout_check(btstack_state_t *btstack){
    btstack->hci->disable_l2cap_timeouts = 1;
}
#endif

#if !defined(HAVE_PLATFORM_IPHONE_OS) && !defined (HAVE_HOST_CONTROLLER_API)
// Set Public BD ADDR - passed on to Bluetooth chipset if supported in bt_control_h
void hci_set_bd_addr(btstack_state_t *btstack, bd_addr_t addr){
    (void)memcpy(btstack->hci->custom_bd_addr, addr, 6);
    btstack->hci->custom_bd_addr_set = 1;
}
#endif

// State-Module-Driver overview
// state                    module  low-level
// HCI_STATE_OFF             off      close
// HCI_STATE_INITIALIZING,   on       open
// HCI_STATE_WORKING,        on       open
// HCI_STATE_HALTING,        on       open
// HCI_STATE_SLEEPING,    off/sleep   close
// HCI_STATE_FALLING_ASLEEP  on       open

static int hci_power_control_on(btstack_state_t *btstack){

    // power on
    int err = 0;
    if (btstack->hci->control && btstack->hci->control->on){
        err = (*btstack->hci->control->on)();
    }
    if (err){
        log_error( "POWER_ON failed");
        hci_emit_hci_open_failed(btstack);
        return err;
    }

    // int chipset driver
    if (btstack->hci->chipset && btstack->hci->chipset->init){
        btstack->hci->chipset->init(btstack->hci->config);
    }

    // init transport
    if (btstack->hci->hci_transport->init){
        btstack->hci->hci_transport->init(btstack, btstack->hci->config);
    }

    // open transport
    err = btstack->hci->hci_transport->open(btstack);
    if (err){
        log_error( "HCI_INIT failed, turning Bluetooth off again");
        if (btstack->hci->control && btstack->hci->control->off){
            (*btstack->hci->control->off)();
        }
        hci_emit_hci_open_failed(btstack);
        return err;
    }
    return 0;
}

static void hci_power_control_off(btstack_state_t *btstack){

    log_info("hci_power_control_off");

    // close low-level device
    btstack->hci->hci_transport->close(btstack);

    log_info("hci_power_control_off - hci_transport closed");

    // power off
    if (btstack->hci->control && btstack->hci->control->off){
        (*btstack->hci->control->off)();
    }

    log_info("hci_power_control_off - control closed");

    btstack->hci->state = HCI_STATE_OFF;
}

static void hci_power_control_sleep(btstack_state_t *btstack){

    log_info("hci_power_control_sleep");

#if 0
    // don't close serial port during sleep

    // close low-level device
    btstack->hci->hci_transport->close(btstack->hci->config);
#endif

    // sleep mode
    if (btstack->hci->control && btstack->hci->control->sleep){
        (*btstack->hci->control->sleep)();
    }

    btstack->hci->state = HCI_STATE_SLEEPING;
}

static int hci_power_control_wake(btstack_state_t *btstack){

    log_info("hci_power_control_wake");

    // wake on
    if (btstack->hci->control && btstack->hci->control->wake){
        (*btstack->hci->control->wake)();
    }

#if 0
    // open low-level device
    int err = btstack->hci->hci_transport->open(btstack->hci->config);
    if (err){
        log_error( "HCI_INIT failed, turning Bluetooth off again");
        if (btstack->hci->control && btstack->hci->control->off){
            (*btstack->hci->control->off)();
        }
        hci_emit_hci_open_failed();
        return err;
    }
#endif

    return 0;
}

static void hci_power_transition_to_initializing(btstack_state_t *btstack){
    // set up state machine
    btstack->hci->num_cmd_packets = 1; // assume that one cmd can be sent
    btstack->hci->hci_packet_buffer_reserved = 0;
    btstack->hci->state = HCI_STATE_INITIALIZING;
    btstack->hci->substate = HCI_INIT_SEND_RESET;
}

int hci_power_control(btstack_state_t *btstack, HCI_POWER_MODE power_mode){

    log_info("hci_power_control: %d, current mode %u", power_mode, btstack->hci->state);

    int err = 0;
    switch (btstack->hci->state){

        case HCI_STATE_OFF:
            switch (power_mode){
                case HCI_POWER_ON:
                    err = hci_power_control_on(btstack);
                    if (err) {
                        log_error("hci_power_control_on() error %d", err);
                        return err;
                    }
                    hci_power_transition_to_initializing(btstack);
                    break;
                case HCI_POWER_OFF:
                    // do nothing
                    break;
                case HCI_POWER_SLEEP:
                    // do nothing (with SLEEP == OFF)
                    break;
            }
            break;

        case HCI_STATE_INITIALIZING:
            switch (power_mode){
                case HCI_POWER_ON:
                    // do nothing
                    break;
                case HCI_POWER_OFF:
                    // no connections yet, just turn it off
                    hci_power_control_off(btstack);
                    break;
                case HCI_POWER_SLEEP:
                    // no connections yet, just turn it off
                    hci_power_control_sleep(btstack);
                    break;
            }
            break;

        case HCI_STATE_WORKING:
            switch (power_mode){
                case HCI_POWER_ON:
                    // do nothing
                    break;
                case HCI_POWER_OFF:
                    // see hci_run
                    btstack->hci->state = HCI_STATE_HALTING;
                    btstack->hci->substate = HCI_HALTING_DISCONNECT_ALL_NO_TIMER;
                    break;
                case HCI_POWER_SLEEP:
                    // see hci_run
                    btstack->hci->state = HCI_STATE_FALLING_ASLEEP;
                    btstack->hci->substate = HCI_FALLING_ASLEEP_DISCONNECT;
                    break;
            }
            break;

        case HCI_STATE_HALTING:
            switch (power_mode){
                case HCI_POWER_ON:
                    hci_power_transition_to_initializing(btstack);
                    break;
                case HCI_POWER_OFF:
                    // do nothing
                    break;
                case HCI_POWER_SLEEP:
                    // see hci_run
                    btstack->hci->state = HCI_STATE_FALLING_ASLEEP;
                    btstack->hci->substate = HCI_FALLING_ASLEEP_DISCONNECT;
                    break;
            }
            break;

        case HCI_STATE_FALLING_ASLEEP:
            switch (power_mode){
                case HCI_POWER_ON:

#ifdef HAVE_PLATFORM_IPHONE_OS
                    // nothing to do, if H4 supports power management
                    if (btstack_control_iphone_power_management_enabled()){
                        btstack->hci->state = HCI_STATE_INITIALIZING;
                        btstack->hci->substate = HCI_INIT_WRITE_SCAN_ENABLE;   // init after sleep
                        break;
                    }
#endif
                    hci_power_transition_to_initializing(btstack);
                    break;
                case HCI_POWER_OFF:
                    // see hci_run
                    btstack->hci->state = HCI_STATE_HALTING;
                    btstack->hci->substate = HCI_HALTING_DISCONNECT_ALL_NO_TIMER;
                    break;
                case HCI_POWER_SLEEP:
                    // do nothing
                    break;
            }
            break;

        case HCI_STATE_SLEEPING:
            switch (power_mode){
                case HCI_POWER_ON:

#ifdef HAVE_PLATFORM_IPHONE_OS
                    // nothing to do, if H4 supports power management
                    if (btstack_control_iphone_power_management_enabled()){
                        btstack->hci->state = HCI_STATE_INITIALIZING;
                        btstack->hci->substate = HCI_INIT_AFTER_SLEEP;
                        hci_update_scan_enable();
                        break;
                    }
#endif
                    err = hci_power_control_wake(btstack);
                    if (err) return err;
                    hci_power_transition_to_initializing(btstack);
                    break;
                case HCI_POWER_OFF:
                    btstack->hci->state = HCI_STATE_HALTING;
                    btstack->hci->substate = HCI_HALTING_DISCONNECT_ALL_NO_TIMER;
                    break;
                case HCI_POWER_SLEEP:
                    // do nothing
                    break;
            }
            break;
    }

    // create internal event
	hci_emit_state(btstack);

	// trigger next/first action
	hci_run(btstack);

    return 0;
}


#ifdef ENABLE_CLASSIC

static void hci_update_scan_enable(btstack_state_t *btstack){
    // 2 = page scan, 1 = inq scan
    btstack->hci->new_scan_enable_value  = (btstack->hci->connectable << 1) | btstack->hci->discoverable;
    hci_run(btstack);
}

void gap_discoverable_control(btstack_state_t *btstack, uint8_t enable){
    if (enable) enable = 1; // normalize argument

    if (btstack->hci->discoverable == enable){
        hci_emit_discoverable_enabled(btstack, btstack->hci->discoverable);
        return;
    }

    btstack->hci->discoverable = enable;
    hci_update_scan_enable(btstack);
}

void gap_connectable_control(btstack_state_t *btstack, uint8_t enable){
    if (enable) enable = 1; // normalize argument

    // don't emit event
    if (btstack->hci->connectable == enable) return;

    btstack->hci->connectable = enable;
    hci_update_scan_enable(btstack);
}
#endif

void gap_local_bd_addr(btstack_state_t *btstack, bd_addr_t address_buffer){
    (void)memcpy(address_buffer, btstack->hci->local_bd_addr, 6);
}

#ifdef ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
static void hci_host_num_completed_packets(void){

    // create packet manually as arrays are not supported and num_commands should not get reduced
    hci_reserve_packet_buffer();
    uint8_t * packet = hci_get_outgoing_packet_buffer();

    uint16_t size = 0;
    uint16_t num_handles = 0;
    packet[size++] = 0x35;
    packet[size++] = 0x0c;
    size++;  // skip param len
    size++;  // skip num handles

    // add { handle, packets } entries
    btstack_linked_item_t * it;
    for (it = (btstack_linked_item_t *) btstack->hci->connections; it ; it = it->next){
        hci_connection_t * connection = (hci_connection_t *) it;
        if (connection->num_packets_completed){
            little_endian_store_16(packet, size, connection->con_handle);
            size += 2;
            little_endian_store_16(packet, size, connection->num_packets_completed);
            size += 2;
            //
            num_handles++;
            connection->num_packets_completed = 0;
        }
    }

    packet[2] = size - 3;
    packet[3] = num_handles;

    btstack->hci->host_completed_packets = 0;

    hci_dump_packet(HCI_COMMAND_DATA_PACKET, 0, packet, size);
    btstack->hci->hci_transport->send_packet(HCI_COMMAND_DATA_PACKET, packet, size);

    // release packet buffer for synchronous transport implementations
    if (hci_transport_synchronous()){
        hci_release_packet_buffer();
        hci_emit_transport_packet_sent();
    }
}
#endif

static void hci_halting_timeout_handler(btstack_state_t *btstack, btstack_timer_source_t * ds){
    UNUSED(ds);
    btstack->hci->substate = HCI_HALTING_CLOSE;
    // allow packet handlers to defer final shutdown
    hci_emit_state(btstack);
    hci_run(btstack);
}

static bool hci_run_acl_fragments(btstack_state_t *btstack){
    if (btstack->hci->acl_fragmentation_total_size > 0) {
        hci_con_handle_t con_handle = READ_ACL_CONNECTION_HANDLE(btstack->hci->hci_packet_buffer);
        hci_connection_t *connection = hci_connection_for_handle(btstack, con_handle);
        if (connection) {
            if (hci_can_send_prepared_acl_packet_now(btstack, con_handle)){
                hci_send_acl_packet_fragments(btstack, connection);
                return true;
            }
        } else {
            // connection gone -> discard further fragments
            log_info("hci_run: fragmented ACL packet no connection -> discard fragment");
            btstack->hci->acl_fragmentation_total_size = 0;
            btstack->hci->acl_fragmentation_pos = 0;
        }
    }
    return false;
}

#ifdef ENABLE_CLASSIC
static bool hci_run_general_gap_classic(btstack_state_t *btstack){

    // decline incoming connections
    if (btstack->hci->decline_reason){
        uint8_t reason = btstack->hci->decline_reason;
        btstack->hci->decline_reason = 0;
        hci_send_cmd(btstack, &hci_reject_connection_request, btstack->hci->decline_addr, reason);
        return true;
    }
    // send scan enable
    if ((btstack->hci->state == HCI_STATE_WORKING) && (btstack->hci->new_scan_enable_value != 0xff) && hci_classic_supported(btstack)){
        hci_send_cmd(btstack, &hci_write_scan_enable, btstack->hci->new_scan_enable_value);
        btstack->hci->new_scan_enable_value = 0xff;
        return true;
    }
    // start/stop inquiry
    if ((btstack->hci->inquiry_state >= GAP_INQUIRY_DURATION_MIN) && (btstack->hci->inquiry_state <= GAP_INQUIRY_DURATION_MAX)){
        uint8_t duration = btstack->hci->inquiry_state;
        einstein_log(31, __func__, __LINE__, "%d", duration);
        btstack->hci->inquiry_state = GAP_INQUIRY_STATE_ACTIVE;
        hci_send_cmd(btstack, &hci_inquiry, GAP_IAC_GENERAL_INQUIRY, duration, 0);
        return true;
    }
    if (btstack->hci->inquiry_state == GAP_INQUIRY_STATE_W2_CANCEL){
        btstack->hci->inquiry_state = GAP_INQUIRY_STATE_W4_CANCELLED;
        hci_send_cmd(btstack, &hci_inquiry_cancel);
        return true;
    }
    // remote name request
    if (btstack->hci->remote_name_state == GAP_REMOTE_NAME_STATE_W2_SEND){
        btstack->hci->remote_name_state = GAP_REMOTE_NAME_STATE_W4_COMPLETE;
        hci_send_cmd(btstack, &hci_remote_name_request, btstack->hci->remote_name_addr,
                     btstack->hci->remote_name_page_scan_repetition_mode, 0, btstack->hci->remote_name_clock_offset);
        return true;
    }
    // pairing
    if (btstack->hci->gap_pairing_state != GAP_PAIRING_STATE_IDLE){
        uint8_t state = btstack->hci->gap_pairing_state;
        btstack->hci->gap_pairing_state = GAP_PAIRING_STATE_IDLE;
        switch (state){
            case GAP_PAIRING_STATE_SEND_PIN:
                hci_send_cmd(btstack, &hci_pin_code_request_reply, btstack->hci->gap_pairing_addr, strlen(btstack->hci->gap_pairing_input.gap_pairing_pin), btstack->hci->gap_pairing_input.gap_pairing_pin);
                break;
            case GAP_PAIRING_STATE_SEND_PIN_NEGATIVE:
                hci_send_cmd(btstack, &hci_pin_code_request_negative_reply, btstack->hci->gap_pairing_addr);
                break;
            case GAP_PAIRING_STATE_SEND_PASSKEY:
                hci_send_cmd(btstack, &hci_user_passkey_request_reply, btstack->hci->gap_pairing_addr, btstack->hci->gap_pairing_input.gap_pairing_passkey);
                break;
            case GAP_PAIRING_STATE_SEND_PASSKEY_NEGATIVE:
                hci_send_cmd(btstack, &hci_user_passkey_request_negative_reply, btstack->hci->gap_pairing_addr);
                break;
            case GAP_PAIRING_STATE_SEND_CONFIRMATION:
                hci_send_cmd(btstack, &hci_user_confirmation_request_reply, btstack->hci->gap_pairing_addr);
                break;
            case GAP_PAIRING_STATE_SEND_CONFIRMATION_NEGATIVE:
                hci_send_cmd(btstack, &hci_user_confirmation_request_negative_reply, btstack->hci->gap_pairing_addr);
                break;
            default:
                break;
        }
        return true;
    }
    return false;
}
#endif

#ifdef ENABLE_BLE
static bool hci_run_general_gap_le(btstack_state_t *btstack){

    // advertisements, active scanning, and creating connections requires random address to be set if using private address

    if (btstack->hci->state != HCI_STATE_WORKING) return false;
    if ( (btstack->hci->le_own_addr_type != BD_ADDR_TYPE_LE_PUBLIC) && (btstack->hci->le_random_address_set == 0) ) return false;

#ifdef ENABLE_LE_CENTRAL
    // parameter change requires scanning to be stopped first
    if (btstack->hci->le_scan_type != 0xff) {
        if (btstack->hci->le_scanning_active){
            btstack->hci->le_scanning_active = 0;
            hci_send_cmd(btstack, &hci_le_set_scan_enable, 0, 0);
        } else {
            int scan_type = (int) btstack->hci->le_scan_type;
            btstack->hci->le_scan_type = 0xff;
            hci_send_cmd(btstack, &hci_le_set_scan_parameters, scan_type, btstack->hci->le_scan_interval, btstack->hci->le_scan_window, btstack->hci->le_own_addr_type, 0);
        }
        return true;
    }
    // finally, we can enable/disable le scan
    if ((btstack->hci->le_scanning_enabled != btstack->hci->le_scanning_active)){
        btstack->hci->le_scanning_active = btstack->hci->le_scanning_enabled;
        hci_send_cmd(btstack, &hci_le_set_scan_enable, btstack->hci->le_scanning_enabled, 0);
        return true;
    }
#endif
#ifdef ENABLE_LE_PERIPHERAL
    // le advertisement control
    if (btstack->hci->le_advertisements_todo){
        log_info("hci_run: gap_le: adv todo: %x", btstack->hci->le_advertisements_todo );
    }
    if (btstack->hci->le_advertisements_todo & LE_ADVERTISEMENT_TASKS_DISABLE){
        btstack->hci->le_advertisements_todo &= ~LE_ADVERTISEMENT_TASKS_DISABLE;
        hci_send_cmd(&hci_le_set_advertise_enable, 0);
        return true;
    }
    if (btstack->hci->le_advertisements_todo & LE_ADVERTISEMENT_TASKS_SET_PARAMS){
        btstack->hci->le_advertisements_todo &= ~LE_ADVERTISEMENT_TASKS_SET_PARAMS;
        hci_send_cmd(&hci_le_set_advertising_parameters,
                     btstack->hci->le_advertisements_interval_min,
                     btstack->hci->le_advertisements_interval_max,
                     btstack->hci->le_advertisements_type,
                     btstack->hci->le_own_addr_type,
                     btstack->hci->le_advertisements_direct_address_type,
                     btstack->hci->le_advertisements_direct_address,
                     btstack->hci->le_advertisements_channel_map,
                     btstack->hci->le_advertisements_filter_policy);
        return true;
    }
    if (btstack->hci->le_advertisements_todo & LE_ADVERTISEMENT_TASKS_SET_ADV_DATA){
        btstack->hci->le_advertisements_todo &= ~LE_ADVERTISEMENT_TASKS_SET_ADV_DATA;
        uint8_t adv_data_clean[31];
        memset(adv_data_clean, 0, sizeof(adv_data_clean));
        (void)memcpy(adv_data_clean, btstack->hci->le_advertisements_data,
                     btstack->hci->le_advertisements_data_len);
        btstack_replace_bd_addr_placeholder(adv_data_clean, btstack->hci->le_advertisements_data_len, btstack->hci->local_bd_addr);
        hci_send_cmd(&hci_le_set_advertising_data, btstack->hci->le_advertisements_data_len, adv_data_clean);
        return true;
    }
    if (btstack->hci->le_advertisements_todo & LE_ADVERTISEMENT_TASKS_SET_SCAN_DATA){
        btstack->hci->le_advertisements_todo &= ~LE_ADVERTISEMENT_TASKS_SET_SCAN_DATA;
        uint8_t scan_data_clean[31];
        memset(scan_data_clean, 0, sizeof(scan_data_clean));
        (void)memcpy(scan_data_clean, btstack->hci->le_scan_response_data,
                     btstack->hci->le_scan_response_data_len);
        btstack_replace_bd_addr_placeholder(scan_data_clean, btstack->hci->le_scan_response_data_len, btstack->hci->local_bd_addr);
        hci_send_cmd(&hci_le_set_scan_response_data, btstack->hci->le_scan_response_data_len, scan_data_clean);
        return true;
    }
    if (btstack->hci->le_advertisements_todo & LE_ADVERTISEMENT_TASKS_ENABLE){
        btstack->hci->le_advertisements_todo &= ~LE_ADVERTISEMENT_TASKS_ENABLE;
        hci_send_cmd(&hci_le_set_advertise_enable, 1);
        return true;
    }
#endif

#ifdef ENABLE_LE_CENTRAL
    //
    // LE Whitelist Management
    //

    // check if whitelist needs modification
    btstack_linked_list_iterator_t lit;
    int modification_pending = 0;
    btstack_linked_list_iterator_init(&lit, &btstack->hci->le_whitelist);
    while (btstack_linked_list_iterator_has_next(&lit)){
        whitelist_entry_t * entry = (whitelist_entry_t*) btstack_linked_list_iterator_next(&lit);
        if (entry->state & (LE_WHITELIST_REMOVE_FROM_CONTROLLER | LE_WHITELIST_ADD_TO_CONTROLLER)){
            modification_pending = 1;
            break;
        }
    }

    if (modification_pending){
        // stop connnecting if modification pending
        if (btstack->hci->le_connecting_state != LE_CONNECTING_IDLE){
            hci_send_cmd(btstack, &hci_le_create_connection_cancel);
            return true;
        }

        // add/remove entries
        btstack_linked_list_iterator_init(&lit, &btstack->hci->le_whitelist);
        while (btstack_linked_list_iterator_has_next(&lit)){
            whitelist_entry_t * entry = (whitelist_entry_t*) btstack_linked_list_iterator_next(&lit);
            if (entry->state & LE_WHITELIST_ADD_TO_CONTROLLER){
                entry->state = LE_WHITELIST_ON_CONTROLLER;
                hci_send_cmd(btstack, &hci_le_add_device_to_white_list, entry->address_type, entry->address);
                return true;
            }
            if (entry->state & LE_WHITELIST_REMOVE_FROM_CONTROLLER){
                bd_addr_t address;
                bd_addr_type_t address_type = entry->address_type;
                (void)memcpy(address, entry->address, 6);
                btstack_linked_list_remove(&btstack->hci->le_whitelist, (btstack_linked_item_t *) entry);
                btstack_memory_whitelist_entry_free(entry);
                hci_send_cmd(btstack, &hci_le_remove_device_from_white_list, address_type, address);
                return true;
            }
        }
    }

    // start connecting
    if ( (btstack->hci->le_connecting_state == LE_CONNECTING_IDLE) &&
         !btstack_linked_list_empty(&btstack->hci->le_whitelist)){
        bd_addr_t null_addr;
        memset(null_addr, 0, 6);
        hci_send_cmd(btstack, &hci_le_create_connection,
                     btstack->hci->le_connection_scan_interval,    // scan interval: 60 ms
                     btstack->hci->le_connection_scan_window,    // scan interval: 30 ms
                     1,         // use whitelist
                     0,         // peer address type
                     null_addr, // peer bd addr
                     btstack->hci->le_own_addr_type, // our addr type:
                     btstack->hci->le_connection_interval_min,    // conn interval min
                     btstack->hci->le_connection_interval_max,    // conn interval max
                     btstack->hci->le_connection_latency,         // conn latency
                     btstack->hci->le_supervision_timeout,        // conn latency
                     btstack->hci->le_minimum_ce_length,          // min ce length
                     btstack->hci->le_maximum_ce_length           // max ce length
        );
        return true;
    }
#endif
    return false;
}
#endif

static bool hci_run_general_pending_commmands(btstack_state_t *btstack){
    btstack_linked_item_t * it;
    for (it = (btstack_linked_item_t *) btstack->hci->connections; it != NULL; it = it->next){
        hci_connection_t * connection = (hci_connection_t *) it;

        switch(connection->state){
            case SEND_CREATE_CONNECTION:
                switch(connection->address_type){
#ifdef ENABLE_CLASSIC
                    case BD_ADDR_TYPE_ACL:
                        log_info("sending hci_create_connection");
                        hci_send_cmd(btstack, &hci_create_connection, connection->address, hci_usable_acl_packet_types(btstack), 0, 0, 0, btstack->hci->allow_role_switch);
                        break;
#endif
                    default:
#ifdef ENABLE_BLE
#ifdef ENABLE_LE_CENTRAL
                        // track outgoing connection
                        btstack->hci->outgoing_addr_type = connection->address_type;
                        (void)memcpy(btstack->hci->outgoing_addr,
                                     connection->address, 6);
                        log_info("sending hci_le_create_connection");
                        hci_send_cmd(btstack, &hci_le_create_connection,
                                     btstack->hci->le_connection_scan_interval,    // conn scan interval
                                     btstack->hci->le_connection_scan_window,      // conn scan windows
                                     0,         // don't use whitelist
                                     connection->address_type, // peer address type
                                     connection->address,      // peer bd addr
                                     btstack->hci->le_own_addr_type, // our addr type:
                                     btstack->hci->le_connection_interval_min,    // conn interval min
                                     btstack->hci->le_connection_interval_max,    // conn interval max
                                     btstack->hci->le_connection_latency,         // conn latency
                                     btstack->hci->le_supervision_timeout,        // conn latency
                                     btstack->hci->le_minimum_ce_length,          // min ce length
                                     btstack->hci->le_maximum_ce_length          // max ce length
                        );
                        connection->state = SENT_CREATE_CONNECTION;
#endif
#endif
                        break;
                }
                return true;

#ifdef ENABLE_CLASSIC
            case RECEIVED_CONNECTION_REQUEST:
                connection->role  = HCI_ROLE_SLAVE;
                if (connection->address_type == BD_ADDR_TYPE_ACL){
                    log_info("sending hci_accept_connection_request, remote eSCO %u", connection->remote_supported_feature_eSCO);
                    connection->state = ACCEPTED_CONNECTION_REQUEST;
                    hci_send_cmd(btstack, &hci_accept_connection_request, connection->address, btstack->hci->master_slave_policy);
                }
                return true;
#endif

#ifdef ENABLE_BLE
#ifdef ENABLE_LE_CENTRAL
            case SEND_CANCEL_CONNECTION:
                connection->state = SENT_CANCEL_CONNECTION;
                hci_send_cmd(btstack, &hci_le_create_connection_cancel);
                return true;
#endif
#endif
            case SEND_DISCONNECT:
                connection->state = SENT_DISCONNECT;
                hci_send_cmd(btstack, &hci_disconnect, connection->con_handle, 0x13); // remote closed connection
                return true;

            default:
                break;
        }

        // no further commands if connection is about to get shut down
        if (connection->state == SENT_DISCONNECT) continue;

        if (connection->authentication_flags & READ_RSSI){
            connectionClearAuthenticationFlags(connection, READ_RSSI);
            hci_send_cmd(btstack, &hci_read_rssi, connection->con_handle);
            return true;
        }

#ifdef ENABLE_CLASSIC

        if (connection->authentication_flags & WRITE_SUPERVISION_TIMEOUT){
            connectionClearAuthenticationFlags(connection, WRITE_SUPERVISION_TIMEOUT);
            hci_send_cmd(btstack, &hci_write_link_supervision_timeout, connection->con_handle, btstack->hci->link_supervision_timeout);
            return true;
        }

        if (connection->authentication_flags & HANDLE_LINK_KEY_REQUEST){
            log_info("responding to link key request");
            connectionClearAuthenticationFlags(connection, HANDLE_LINK_KEY_REQUEST);
            link_key_t link_key;
            link_key_type_t link_key_type;
            if ( btstack->hci->link_key_db
                 && btstack->hci->link_key_db->get_link_key(connection->address, link_key, &link_key_type)
                 && (gap_security_level_for_link_key_type(link_key_type) >= connection->requested_security_level)){
                connection->link_key_type = link_key_type;
                hci_send_cmd(btstack, &hci_link_key_request_reply, connection->address, &link_key);
            } else {
                hci_send_cmd(btstack, &hci_link_key_request_negative_reply, connection->address);
            }
            return true;
        }

        if (connection->authentication_flags & DENY_PIN_CODE_REQUEST){
            log_info("denying to pin request");
            connectionClearAuthenticationFlags(connection, DENY_PIN_CODE_REQUEST);
            hci_send_cmd(btstack, &hci_pin_code_request_negative_reply, connection->address);
            return true;
        }

        if (connection->authentication_flags & SEND_IO_CAPABILITIES_REPLY){
            connectionClearAuthenticationFlags(connection, SEND_IO_CAPABILITIES_REPLY);
            log_info("IO Capability Request received, stack bondable %u, io cap %u", btstack->hci->bondable, btstack->hci->ssp_io_capability);
            if (btstack->hci->bondable && (btstack->hci->ssp_io_capability != SSP_IO_CAPABILITY_UNKNOWN)){
                // tweak authentication requirements
                uint8_t authreq = btstack->hci->ssp_authentication_requirement;
                if (connection->bonding_flags & BONDING_DEDICATED){
                    authreq = SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_DEDICATED_BONDING;
                }
                if (gap_mitm_protection_required_for_security_level(connection->requested_security_level)){
                    authreq |= 1;
                }
                hci_send_cmd(btstack, &hci_io_capability_request_reply, &connection->address, btstack->hci->ssp_io_capability, NULL, authreq);
            } else {
                hci_send_cmd(btstack, &hci_io_capability_request_negative_reply, &connection->address, ERROR_CODE_PAIRING_NOT_ALLOWED);
            }
            return true;
        }

        if (connection->authentication_flags & SEND_USER_CONFIRM_REPLY){
            connectionClearAuthenticationFlags(connection, SEND_USER_CONFIRM_REPLY);
            hci_send_cmd(btstack, &hci_user_confirmation_request_reply, &connection->address);
            return true;
        }

        if (connection->authentication_flags & SEND_USER_PASSKEY_REPLY){
            connectionClearAuthenticationFlags(connection, SEND_USER_PASSKEY_REPLY);
            hci_send_cmd(btstack, &hci_user_passkey_request_reply, &connection->address, 000000);
            return true;
        }

        if (connection->bonding_flags & BONDING_REQUEST_REMOTE_FEATURES){
            connection->bonding_flags &= ~BONDING_REQUEST_REMOTE_FEATURES;
            hci_send_cmd(btstack, &hci_read_remote_supported_features_command, connection->con_handle);
            return true;
        }

        if (connection->bonding_flags & BONDING_DISCONNECT_DEDICATED_DONE){
            connection->bonding_flags &= ~BONDING_DISCONNECT_DEDICATED_DONE;
            connection->bonding_flags |= BONDING_EMIT_COMPLETE_ON_DISCONNECT;
            hci_send_cmd(btstack, &hci_disconnect, connection->con_handle, 0x13);  // authentication done
            return true;
        }

        if (connection->bonding_flags & BONDING_SEND_AUTHENTICATE_REQUEST){
            connection->bonding_flags &= ~BONDING_SEND_AUTHENTICATE_REQUEST;
            hci_send_cmd(btstack, &hci_authentication_requested, connection->con_handle);
            return true;
        }

        if (connection->bonding_flags & BONDING_SEND_ENCRYPTION_REQUEST){
            connection->bonding_flags &= ~BONDING_SEND_ENCRYPTION_REQUEST;
            hci_send_cmd(btstack, &hci_set_connection_encryption, connection->con_handle, 1);
            return true;
        }
        if (connection->bonding_flags & BONDING_SEND_READ_ENCRYPTION_KEY_SIZE){
            connection->bonding_flags &= ~BONDING_SEND_READ_ENCRYPTION_KEY_SIZE;
            hci_send_cmd(btstack, &hci_read_encryption_key_size, connection->con_handle, 1);
            return true;
        }
#endif

        if (connection->bonding_flags & BONDING_DISCONNECT_SECURITY_BLOCK){
            connection->bonding_flags &= ~BONDING_DISCONNECT_SECURITY_BLOCK;
            hci_send_cmd(btstack, &hci_disconnect, connection->con_handle, 0x0005);  // authentication failure
            return true;
        }

#ifdef ENABLE_CLASSIC
        uint16_t sniff_min_interval;
        switch (connection->sniff_min_interval){
            case 0:
                break;
            case 0xffff:
                connection->sniff_min_interval = 0;
                hci_send_cmd(btstack, &hci_exit_sniff_mode, connection->con_handle);
                return true;
            default:
                sniff_min_interval = connection->sniff_min_interval;
                connection->sniff_min_interval = 0;
                hci_send_cmd(btstack, &hci_sniff_mode, connection->con_handle, connection->sniff_max_interval, sniff_min_interval, connection->sniff_attempt, connection->sniff_timeout);
                return true;
        }
#endif

#ifdef ENABLE_BLE
        switch (connection->le_con_parameter_update_state){
            // response to L2CAP CON PARAMETER UPDATE REQUEST
            case CON_PARAMETER_UPDATE_CHANGE_HCI_CON_PARAMETERS:
                connection->le_con_parameter_update_state = CON_PARAMETER_UPDATE_NONE;
                hci_send_cmd(btstack, &hci_le_connection_update, connection->con_handle, connection->le_conn_interval_min,
                             connection->le_conn_interval_max, connection->le_conn_latency, connection->le_supervision_timeout,
                             0x0000, 0xffff);
                return true;
            case CON_PARAMETER_UPDATE_REPLY:
                connection->le_con_parameter_update_state = CON_PARAMETER_UPDATE_NONE;
                hci_send_cmd(btstack, &hci_le_remote_connection_parameter_request_reply, connection->con_handle, connection->le_conn_interval_min,
                             connection->le_conn_interval_max, connection->le_conn_latency, connection->le_supervision_timeout,
                             0x0000, 0xffff);
                return true;
            case CON_PARAMETER_UPDATE_NEGATIVE_REPLY:
                connection->le_con_parameter_update_state = CON_PARAMETER_UPDATE_NONE;
                hci_send_cmd(btstack, &hci_le_remote_connection_parameter_request_negative_reply, ERROR_CODE_UNSUPPORTED_LMP_PARAMETER_VALUE_UNSUPPORTED_LL_PARAMETER_VALUE);
                return true;
            default:
                break;
        }
        if (connection->le_phy_update_all_phys != 0xff){
            uint8_t all_phys = connection->le_phy_update_all_phys;
            connection->le_phy_update_all_phys = 0xff;
            hci_send_cmd(btstack, &hci_le_set_phy, connection->con_handle, all_phys, connection->le_phy_update_tx_phys, connection->le_phy_update_rx_phys, connection->le_phy_update_phy_options);
            return true;
        }
#endif
    }
    return false;
}

static void hci_run(btstack_state_t *btstack){

    bool done;

    // send continuation fragments first, as they block the prepared packet buffer
    done = hci_run_acl_fragments(btstack);
    if (done) return;

#ifdef ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
    // send host num completed packets next as they don't require num_cmd_packets > 0
    if (!hci_can_send_comand_packet_transport()) return;
    if (btstack->hci->host_completed_packets){
        hci_host_num_completed_packets();
        return;
    }
#endif

    if (!hci_can_send_command_packet_now(btstack)) return;

    // global/non-connection oriented commands


#ifdef ENABLE_CLASSIC
    // general gap classic
    done = hci_run_general_gap_classic(btstack);
    if (done) return;
#endif

#ifdef ENABLE_BLE
    // general gap le
    done = hci_run_general_gap_le(btstack);
    if (done) return;
#endif

    // send pending HCI commands
    done = hci_run_general_pending_commmands(btstack);
    if (done) return;

    // stack state sub statemachines
    hci_connection_t * connection;
    switch (btstack->hci->state){
        case HCI_STATE_INITIALIZING:
            hci_initializing_run(btstack);
            break;

        case HCI_STATE_HALTING:

            log_info("HCI_STATE_HALTING, substate %x\n", btstack->hci->substate);
            switch (btstack->hci->substate){
                case HCI_HALTING_DISCONNECT_ALL_NO_TIMER:
                case HCI_HALTING_DISCONNECT_ALL_TIMER:

#ifdef ENABLE_BLE
#ifdef ENABLE_LE_CENTRAL
                    // free whitelist entries
                    {
                        btstack_linked_list_iterator_t lit;
                        btstack_linked_list_iterator_init(&lit, &btstack->hci->le_whitelist);
                        while (btstack_linked_list_iterator_has_next(&lit)){
                            whitelist_entry_t * entry = (whitelist_entry_t*) btstack_linked_list_iterator_next(&lit);
                            btstack_linked_list_remove(&btstack->hci->le_whitelist, (btstack_linked_item_t *) entry);
                            btstack_memory_whitelist_entry_free(entry);
                        }
                    }
#endif
#endif
                    // close all open connections
                    connection =  (hci_connection_t *) btstack->hci->connections;
                    if (connection){
                        hci_con_handle_t con_handle = (uint16_t) connection->con_handle;
                        if (!hci_can_send_command_packet_now(btstack)) return;

                        // check state
                        if (connection->state == SENT_DISCONNECT) return;
                        connection->state = SENT_DISCONNECT;

                        log_info("HCI_STATE_HALTING, connection %p, handle %u", connection, con_handle);

                        // cancel all l2cap connections right away instead of waiting for disconnection complete event ...
                        hci_emit_disconnection_complete(btstack, con_handle, 0x16); // terminated by local host

                        // ... which would be ignored anyway as we shutdown (free) the connection now
                        hci_shutdown_connection(btstack, connection);

                        // finally, send the disconnect command
                        hci_send_cmd(btstack, &hci_disconnect, con_handle, 0x13);  // remote closed connection
                        return;
                    }

                    if (btstack->hci->substate == HCI_HALTING_DISCONNECT_ALL_TIMER){
                        // no connections left, wait a bit to assert that btstack_cyrpto isn't waiting for an HCI event
                        log_info("HCI_STATE_HALTING: wait 50 ms");
                        btstack->hci->substate = HCI_HALTING_W4_TIMER;
                        btstack_run_loop_set_timer(btstack, &btstack->hci->timeout, 50);
                        btstack_run_loop_set_timer_handler(&btstack->hci->timeout, hci_halting_timeout_handler);
                        btstack_run_loop_add_timer(btstack, &btstack->hci->timeout);
                        break;
                    }

                    /* fall through */

                case HCI_HALTING_CLOSE:
                    log_info("HCI_STATE_HALTING, calling off");

                    // switch mode
                    hci_power_control_off(btstack);

                    log_info("HCI_STATE_HALTING, emitting state");
                    hci_emit_state(btstack);
                    log_info("HCI_STATE_HALTING, done");
                    break;

                case HCI_HALTING_W4_TIMER:
                    // keep waiting

                    break;
                default:
                    break;
            }

            break;

        case HCI_STATE_FALLING_ASLEEP:
            switch(btstack->hci->substate) {
                case HCI_FALLING_ASLEEP_DISCONNECT:
                    log_info("HCI_STATE_FALLING_ASLEEP");
                    // close all open connections
                    connection =  (hci_connection_t *) btstack->hci->connections;

#ifdef HAVE_PLATFORM_IPHONE_OS
                    // don't close connections, if H4 supports power management
                    if (btstack_control_iphone_power_management_enabled()){
                        connection = NULL;
                    }
#endif
                    if (connection){

                        // send disconnect
                        if (!hci_can_send_command_packet_now(btstack)) return;

                        log_info("HCI_STATE_FALLING_ASLEEP, connection %p, handle %u", connection, (uint16_t)connection->con_handle);
                        hci_send_cmd(btstack, &hci_disconnect, connection->con_handle, 0x13);  // remote closed connection

                        // send disconnected event right away - causes higher layer connections to get closed, too.
                        hci_shutdown_connection(btstack, connection);
                        return;
                    }

                    if (hci_classic_supported(btstack)){
                        // disable page and inquiry scan
                        if (!hci_can_send_command_packet_now(btstack)) return;

                        log_info("HCI_STATE_HALTING, disabling inq scans");
                        hci_send_cmd(btstack, &hci_write_scan_enable, btstack->hci->connectable << 1); // drop inquiry scan but keep page scan

                        // continue in next sub state
                        btstack->hci->substate = HCI_FALLING_ASLEEP_W4_WRITE_SCAN_ENABLE;
                        break;
                    }

                    /* fall through */

                case HCI_FALLING_ASLEEP_COMPLETE:
                    log_info("HCI_STATE_HALTING, calling sleep");
#ifdef HAVE_PLATFORM_IPHONE_OS
                    // don't actually go to sleep, if H4 supports power management
                    if (btstack_control_iphone_power_management_enabled()){
                        // SLEEP MODE reached
                        btstack->hci->state = HCI_STATE_SLEEPING;
                        hci_emit_state(btstack);
                        break;
                    }
#endif
                    // switch mode
                    hci_power_control_sleep(btstack);  // changes btstack->hci->state to SLEEP
                    hci_emit_state(btstack);
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

int hci_send_cmd_packet(btstack_state_t *btstack, uint8_t *packet, int size){
    // house-keeping

    if (IS_COMMAND(packet, hci_write_loopback_mode)){
        btstack->hci->loopback_mode = packet[3];
    }

#ifdef ENABLE_CLASSIC
    bd_addr_t addr;
    hci_connection_t * conn;

    // create_connection?
    if (IS_COMMAND(packet, hci_create_connection)){
        reverse_bd_addr(&packet[3], addr);
        log_info("Create_connection to %s", bd_addr_to_str(addr));

        conn = hci_connection_for_bd_addr_and_type(btstack, addr, BD_ADDR_TYPE_ACL);
        if (!conn){
            conn = create_connection_for_bd_addr_and_type(btstack, addr, BD_ADDR_TYPE_ACL);
            if (!conn){
                // notify client that alloc failed
                hci_emit_connection_complete(btstack, addr, 0, BTSTACK_MEMORY_ALLOC_FAILED);
                return -1; // packet not sent to controller
            }
            conn->state = SEND_CREATE_CONNECTION;
        }
        log_info("conn state %u", conn->state);
        switch (conn->state){
            // if connection active exists
            case OPEN:
                // and OPEN, emit connection complete command
                hci_emit_connection_complete(btstack, addr, conn->con_handle, 0);
                return -1; // packet not sent to controller
            case RECEIVED_DISCONNECTION_COMPLETE:
                // create connection triggered in disconnect complete event, let's do it now
                break;
            case SEND_CREATE_CONNECTION:
                // connection created by hci, e.g. dedicated bonding, but not executed yet, let's do it now
                break;
            default:
                // otherwise, just ignore as it is already in the open process
                return -1; // packet not sent to controller
        }
        conn->state = SENT_CREATE_CONNECTION;

        // track outgoing connection
        btstack->hci->outgoing_addr_type = BD_ADDR_TYPE_ACL;
        (void)memcpy(btstack->hci->outgoing_addr, addr, 6);
    }

    else if (IS_COMMAND(packet, hci_link_key_request_reply)){
        hci_add_connection_flags_for_flipped_bd_addr(btstack, &packet[3], SENT_LINK_KEY_REPLY);
    }
    else if (IS_COMMAND(packet, hci_link_key_request_negative_reply)){
        hci_add_connection_flags_for_flipped_bd_addr(btstack, &packet[3], SENT_LINK_KEY_NEGATIVE_REQUEST);
    }

    else if (IS_COMMAND(packet, hci_delete_stored_link_key)){
        if (btstack->hci->link_key_db){
            reverse_bd_addr(&packet[3], addr);
            btstack->hci->link_key_db->delete_link_key(addr);
        }
    }

    else if (IS_COMMAND(packet, hci_pin_code_request_negative_reply)
    ||  IS_COMMAND(packet, hci_pin_code_request_reply)){
        reverse_bd_addr(&packet[3], addr);
        conn = hci_connection_for_bd_addr_and_type(btstack, addr, BD_ADDR_TYPE_ACL);
        if (conn){
            connectionClearAuthenticationFlags(conn, LEGACY_PAIRING_ACTIVE);
        }
    }

    else if (IS_COMMAND(packet, hci_user_confirmation_request_negative_reply)
    ||  IS_COMMAND(packet, hci_user_confirmation_request_reply)
    ||  IS_COMMAND(packet, hci_user_passkey_request_negative_reply)
    ||  IS_COMMAND(packet, hci_user_passkey_request_reply)) {
        reverse_bd_addr(&packet[3], addr);
        conn = hci_connection_for_bd_addr_and_type(btstack, addr, BD_ADDR_TYPE_ACL);
        if (conn){
            connectionClearAuthenticationFlags(conn, SSP_PAIRING_ACTIVE);
        }
    }

#ifdef ENABLE_SCO_OVER_HCI
    // setup_synchronous_connection? Voice setting at offset 22
    else if (IS_COMMAND(packet, hci_setup_synchronous_connection)){
        // TODO: compare to current setting if sco connection already active
        btstack->hci->sco_voice_setting_active = little_endian_read_16(packet, 15);
    }
    // accept_synchronus_connection? Voice setting at offset 18
    else if (IS_COMMAND(packet, hci_accept_synchronous_connection)){
        // TODO: compare to current setting if sco connection already active
        btstack->hci->sco_voice_setting_active = little_endian_read_16(packet, 19);
    }
#endif
#endif

#ifdef ENABLE_BLE
    else if (IS_COMMAND(packet, hci_le_set_random_address)){
        btstack->hci->le_random_address_set = 1;
        reverse_bd_addr(&packet[3], btstack->hci->le_random_address);
    }
#ifdef ENABLE_LE_PERIPHERAL
    else if (IS_COMMAND(packet, hci_le_set_advertise_enable)){
        btstack->hci->le_advertisements_active = packet[3];
    }
#endif
#ifdef ENABLE_LE_CENTRAL
    else if (IS_COMMAND(packet, hci_le_create_connection)){
        // white list used?
        uint8_t initiator_filter_policy = packet[7];
        switch (initiator_filter_policy){
            case 0:
                // whitelist not used
                btstack->hci->le_connecting_state = LE_CONNECTING_DIRECT;
                break;
            case 1:
                btstack->hci->le_connecting_state = LE_CONNECTING_WHITELIST;
                break;
            default:
                log_error("Invalid initiator_filter_policy in LE Create Connection %u", initiator_filter_policy);
                break;
        }
    }
    else if (IS_COMMAND(packet, hci_le_create_connection_cancel)){
        btstack->hci->le_connecting_state = LE_CONNECTING_IDLE;
    }
#endif
#endif

    btstack->hci->num_cmd_packets--;

    hci_dump_packet(HCI_COMMAND_DATA_PACKET, 0, packet, size);
    return btstack->hci->hci_transport->send_packet(btstack, HCI_COMMAND_DATA_PACKET, packet, size);
}

// disconnect because of security block
void hci_disconnect_security_block(btstack_state_t *btstack, hci_con_handle_t con_handle){
    hci_connection_t * connection = hci_connection_for_handle(btstack, con_handle);
    if (!connection) return;
    connection->bonding_flags |= BONDING_DISCONNECT_SECURITY_BLOCK;
}


// Configure Secure Simple Pairing

#ifdef ENABLE_CLASSIC

// enable will enable SSP during init
void gap_ssp_set_enable(btstack_state_t *btstack, int enable){
    btstack->hci->ssp_enable = enable;
}

static int hci_local_ssp_activated(btstack_state_t *btstack){
    return gap_ssp_supported(btstack) && btstack->hci->ssp_enable;
}

// if set, BTstack will respond to io capability request using authentication requirement
void gap_ssp_set_io_capability(btstack_state_t *btstack, int io_capability){
    btstack->hci->ssp_io_capability = io_capability;
}
void gap_ssp_set_authentication_requirement(btstack_state_t *btstack, int authentication_requirement){
    btstack->hci->ssp_authentication_requirement = authentication_requirement;
}

// if set, BTstack will confirm a numberic comparion and enter '000000' if requested
void gap_ssp_set_auto_accept(btstack_state_t *btstack, int auto_accept){
    btstack->hci->ssp_auto_accept = auto_accept;
}

void gap_secure_connections_enable(btstack_state_t *btstack, bool enable){
    btstack->hci->secure_connections_enable = enable;
}

#endif

// va_list part of hci_send_cmd
int hci_send_cmd_va_arg(btstack_state_t *btstack, const hci_cmd_t *cmd, va_list argptr){
    if (!hci_can_send_command_packet_now(btstack)){
        log_error("hci_send_cmd called but cannot send packet now");
        return 0;
    }
    hci_reserve_packet_buffer(btstack);
    btstack->hci->last_cmd_opcode = cmd->opcode;

    uint8_t * packet = btstack->hci->hci_packet_buffer;
    uint16_t size = hci_cmd_create_from_template(packet, cmd, argptr);
    einstein_log(32, __func__, __LINE__, "%04x %d", cmd->opcode, size);
    int err = hci_send_cmd_packet(btstack, packet, size);

    // release packet buffer on error or for synchronous transport implementations
    if ((err < 0) || hci_transport_synchronous(btstack)){
        hci_release_packet_buffer(btstack);
        hci_emit_transport_packet_sent(btstack);
    }

    return err;
}

/**
 * pre: numcmds >= 0 - it's allowed to send a command to the controller
 */
int hci_send_cmd(btstack_state_t *btstack, const hci_cmd_t *cmd, ...){
    va_list argptr;
    va_start(argptr, cmd);
    int res = hci_send_cmd_va_arg(btstack, cmd, argptr);
    va_end(argptr);
    return res;
}

// Create various non-HCI events.
// TODO: generalize, use table similar to hci_create_command

static void hci_emit_event(btstack_state_t *btstack, uint8_t * event, uint16_t size, int dump){
    // dump packet
    if (dump) {
        hci_dump_packet( HCI_EVENT_PACKET, 0, event, size);
    }

    // dispatch to all event handlers
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, &btstack->hci->event_handlers);
    while (btstack_linked_list_iterator_has_next(&it)){
        btstack_packet_callback_registration_t * entry = (btstack_packet_callback_registration_t*) btstack_linked_list_iterator_next(&it);
        entry->callback(btstack, HCI_EVENT_PACKET, 0, event, size);
    }
}

static void hci_emit_acl_packet(btstack_state_t *btstack, uint8_t * packet, uint16_t size){
    if (!btstack->hci->acl_packet_handler) return;
    btstack->hci->acl_packet_handler(btstack, HCI_ACL_DATA_PACKET, 0, packet, size);
}

#ifdef ENABLE_CLASSIC
static void hci_notify_if_sco_can_send_now(btstack_state_t *btstack){
    // notify SCO sender if waiting
    if (!btstack->hci->sco_waiting_for_can_send_now) return;
    if (hci_can_send_sco_packet_now(btstack)){
        btstack->hci->sco_waiting_for_can_send_now = 0;
        uint8_t event[2] = { HCI_EVENT_SCO_CAN_SEND_NOW, 0 };
        hci_dump_packet(HCI_EVENT_PACKET, 1, event, sizeof(event));
        btstack->hci->sco_packet_handler(btstack, HCI_EVENT_PACKET, 0, event, sizeof(event));
    }
}

// parsing end emitting has been merged to reduce code size
static void gap_inquiry_explode(btstack_state_t *btstack, uint8_t *packet, uint16_t size) {
    uint8_t event[19+GAP_INQUIRY_MAX_NAME_LEN];

    uint8_t * eir_data;
    ad_context_t context;
    const uint8_t * name;
    uint8_t         name_len;

    if (size < 3) return;

    int event_type = hci_event_packet_get_type(packet);
    int num_reserved_fields = (event_type == HCI_EVENT_INQUIRY_RESULT) ? 2 : 1;    // 2 for old event, 1 otherwise
    int num_responses       = hci_event_inquiry_result_get_num_responses(packet);

    switch (event_type){
        case HCI_EVENT_INQUIRY_RESULT:
        case HCI_EVENT_INQUIRY_RESULT_WITH_RSSI:
            if (size != (3 + (num_responses * 14))) return;
            break;
        case HCI_EVENT_EXTENDED_INQUIRY_RESPONSE:
            if (size != 257) return;
            if (num_responses != 1) return;
            break;
        default:
            return;
    }

    // event[1] is set at the end
    int i;
    for (i=0; i<num_responses;i++){
        memset(event, 0, sizeof(event));
        event[0] = GAP_EVENT_INQUIRY_RESULT;
        uint8_t event_size = 18;    // if name is not set by EIR

        (void)memcpy(&event[2], &packet[3 + (i * 6)], 6); // bd_addr
        event[8] =          packet[3 + (num_responses*(6))                         + (i*1)];     // page_scan_repetition_mode
        (void)memcpy(&event[9],
                     &packet[3 + (num_responses * (6 + 1 + num_reserved_fields)) + (i * 3)],
                     3); // class of device
        (void)memcpy(&event[12],
                     &packet[3 + (num_responses * (6 + 1 + num_reserved_fields + 3)) + (i * 2)],
                     2); // clock offset

        switch (event_type){
            case HCI_EVENT_INQUIRY_RESULT:
                // 14,15,16,17 = 0, size 18
                break;
            case HCI_EVENT_INQUIRY_RESULT_WITH_RSSI:
                event[14] = 1;
                event[15] = packet [3 + (num_responses*(6+1+num_reserved_fields+3+2)) + (i*1)]; // rssi
                // 16,17 = 0, size 18
                break;
            case HCI_EVENT_EXTENDED_INQUIRY_RESPONSE:
                event[14] = 1;
                event[15] = packet [3 + (num_responses*(6+1+num_reserved_fields+3+2)) + (i*1)]; // rssi
                // EIR packets only contain a single inquiry response
                eir_data = &packet[3 + (6+1+num_reserved_fields+3+2+1)];
                name = NULL;
                // Iterate over EIR data
                for (ad_iterator_init(&context, EXTENDED_INQUIRY_RESPONSE_DATA_LEN, eir_data) ; ad_iterator_has_more(&context) ; ad_iterator_next(&context)){
                    uint8_t data_type    = ad_iterator_get_data_type(&context);
                    uint8_t data_size    = ad_iterator_get_data_len(&context);
                    const uint8_t * data = ad_iterator_get_data(&context);
                    // Prefer Complete Local Name over Shortend Local Name
                    switch (data_type){
                        case BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME:
                            if (name) continue;
                            /* fall through */
                        case BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME:
                            name = data;
                            name_len = data_size;
                            break;
                        default:
                            break;
                    }
                }
                if (name){
                    event[16] = 1;
                    // truncate name if needed
                    int len = btstack_min(name_len, GAP_INQUIRY_MAX_NAME_LEN);
                    event[17] = len;
                    (void)memcpy(&event[18], name, len);
                    event_size += len;
                }
                break;
        }
        event[1] = event_size - 2;
        hci_emit_event(btstack, event, event_size, 1);
    }
}
#endif

void hci_emit_state(btstack_state_t *btstack){
    log_info("BTSTACK_EVENT_STATE %u", btstack->hci->state);
    uint8_t event[3];
    event[0] = BTSTACK_EVENT_STATE;
    event[1] = sizeof(event) - 2;
    event[2] = btstack->hci->state;
    hci_emit_event(btstack, event, sizeof(event), 1);
}

#ifdef ENABLE_CLASSIC
static void hci_emit_connection_complete(btstack_state_t *btstack, bd_addr_t address, hci_con_handle_t con_handle, uint8_t status){
    uint8_t event[13];
    event[0] = HCI_EVENT_CONNECTION_COMPLETE;
    event[1] = sizeof(event) - 2;
    event[2] = status;
    little_endian_store_16(event, 3, con_handle);
    reverse_bd_addr(address, &event[5]);
    event[11] = 1; // ACL connection
    event[12] = 0; // encryption disabled
    hci_emit_event(btstack, event, sizeof(event), 1);
}
static void hci_emit_l2cap_check_timeout(btstack_state_t *btstack, hci_connection_t *conn){
    if (btstack->hci->disable_l2cap_timeouts) return;
    log_info("L2CAP_EVENT_TIMEOUT_CHECK");
    uint8_t event[4];
    event[0] = L2CAP_EVENT_TIMEOUT_CHECK;
    event[1] = sizeof(event) - 2;
    little_endian_store_16(event, 2, conn->con_handle);
    hci_emit_event(btstack, event, sizeof(event), 1);
}
#endif

#ifdef ENABLE_BLE
#ifdef ENABLE_LE_CENTRAL
static void hci_emit_le_connection_complete(btstack_state_t *btstack, uint8_t address_type, bd_addr_t address, hci_con_handle_t con_handle, uint8_t status){
    uint8_t event[21];
    event[0] = HCI_EVENT_LE_META;
    event[1] = sizeof(event) - 2;
    event[2] = HCI_SUBEVENT_LE_CONNECTION_COMPLETE;
    event[3] = status;
    little_endian_store_16(event, 4, con_handle);
    event[6] = 0; // TODO: role
    event[7] = address_type;
    reverse_bd_addr(address, &event[8]);
    little_endian_store_16(event, 14, 0); // interval
    little_endian_store_16(event, 16, 0); // latency
    little_endian_store_16(event, 18, 0); // supervision timeout
    event[20] = 0; // master clock accuracy
    hci_emit_event(btstack, event, sizeof(event), 1);
}
#endif
#endif

static void hci_emit_transport_packet_sent(btstack_state_t *btstack){
    // notify upper stack that it might be possible to send again
    uint8_t event[] = { HCI_EVENT_TRANSPORT_PACKET_SENT, 0};
    hci_emit_event(btstack, &event[0], sizeof(event), 0);  // don't dump
}

static void hci_emit_disconnection_complete(btstack_state_t *btstack, hci_con_handle_t con_handle, uint8_t reason){
    uint8_t event[6];
    event[0] = HCI_EVENT_DISCONNECTION_COMPLETE;
    event[1] = sizeof(event) - 2;
    event[2] = 0; // status = OK
    little_endian_store_16(event, 3, con_handle);
    event[5] = reason;
    hci_emit_event(btstack, event, sizeof(event), 1);
}

static void hci_emit_nr_connections_changed(btstack_state_t *btstack){
    log_info("BTSTACK_EVENT_NR_CONNECTIONS_CHANGED %u", nr_hci_connections(btstack));
    uint8_t event[3];
    event[0] = BTSTACK_EVENT_NR_CONNECTIONS_CHANGED;
    event[1] = sizeof(event) - 2;
    event[2] = nr_hci_connections(btstack);
    hci_emit_event(btstack, event, sizeof(event), 1);
}

static void hci_emit_hci_open_failed(btstack_state_t *btstack){
    log_info("BTSTACK_EVENT_POWERON_FAILED");
    uint8_t event[2];
    event[0] = BTSTACK_EVENT_POWERON_FAILED;
    event[1] = sizeof(event) - 2;
    hci_emit_event(btstack, event, sizeof(event), 1);
}

static void hci_emit_dedicated_bonding_result(btstack_state_t *btstack, bd_addr_t address, uint8_t status){
    log_info("hci_emit_dedicated_bonding_result %u ", status);
    uint8_t event[9];
    int pos = 0;
    event[pos++] = GAP_EVENT_DEDICATED_BONDING_COMPLETED;
    event[pos++] = sizeof(event) - 2;
    event[pos++] = status;
    reverse_bd_addr(address, &event[pos]);
    hci_emit_event(btstack, event, sizeof(event), 1);
}


#ifdef ENABLE_CLASSIC

static void hci_emit_security_level(btstack_state_t *btstack, hci_con_handle_t con_handle, gap_security_level_t level){
    log_info("hci_emit_security_level %u for handle %x", level, con_handle);
    uint8_t event[5];
    int pos = 0;
    event[pos++] = GAP_EVENT_SECURITY_LEVEL;
    event[pos++] = sizeof(event) - 2;
    little_endian_store_16(event, 2, con_handle);
    pos += 2;
    event[pos++] = level;
    hci_emit_event(btstack, event, sizeof(event), 1);
}

static gap_security_level_t gap_security_level_for_connection(btstack_state_t *btstack, hci_connection_t * connection){
    if (!connection) return LEVEL_0;
    if ((connection->authentication_flags & CONNECTION_ENCRYPTED) == 0) return LEVEL_0;
    if (connection->encryption_key_size < btstack->hci->gap_required_encyrption_key_size) return LEVEL_0;
    gap_security_level_t security_level = gap_security_level_for_link_key_type(connection->link_key_type);
    // LEVEL 4 always requires 128 bit encrytion key size
    if ((security_level == LEVEL_4) && (connection->encryption_key_size < 16)){
        security_level = LEVEL_3;
    }
    return security_level;
}

static void hci_emit_discoverable_enabled(btstack_state_t *btstack, uint8_t enabled){
    log_info("BTSTACK_EVENT_DISCOVERABLE_ENABLED %u", enabled);
    uint8_t event[3];
    event[0] = BTSTACK_EVENT_DISCOVERABLE_ENABLED;
    event[1] = sizeof(event) - 2;
    event[2] = enabled;
    hci_emit_event(btstack, event, sizeof(event), 1);
}

// query if remote side supports eSCO
int hci_remote_esco_supported(btstack_state_t *btstack, hci_con_handle_t con_handle){
    hci_connection_t * connection = hci_connection_for_handle(btstack, con_handle);
    if (!connection) return 0;
    return connection->remote_supported_feature_eSCO;
}

// query if remote side supports SSP
int hci_remote_ssp_supported(btstack_state_t *btstack, hci_con_handle_t con_handle){
    hci_connection_t * connection = hci_connection_for_handle(btstack, con_handle);
    if (!connection) return 0;
    return (connection->bonding_flags & BONDING_REMOTE_SUPPORTS_SSP) ? 1 : 0;
}

int gap_ssp_supported_on_both_sides(btstack_state_t *btstack, hci_con_handle_t handle){
    return hci_local_ssp_activated(btstack) && hci_remote_ssp_supported(btstack, handle);
}

// GAP API
/**
 * @bbrief enable/disable bonding. default is enabled
 * @praram enabled
 */
void gap_set_bondable_mode(btstack_state_t *btstack, int enable){
    btstack->hci->bondable = enable ? 1 : 0;
}
/**
 * @brief Get bondable mode.
 * @return 1 if bondable
 */
int gap_get_bondable_mode(btstack_state_t *btstack){
    return btstack->hci->bondable;
}

/**
 * @brief map link keys to security levels
 */
gap_security_level_t gap_security_level_for_link_key_type(link_key_type_t link_key_type){
    switch (link_key_type){
        case AUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P256:
            return LEVEL_4;
        case COMBINATION_KEY:
        case AUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P192:
            return LEVEL_3;
        default:
            return LEVEL_2;
    }
}

/**
 * @brief map link keys to secure connection yes/no
 */
int gap_secure_connection_for_link_key_type(link_key_type_t link_key_type){
    switch (link_key_type){
        case AUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P256:
        case UNAUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P256:
            return 1;
        default:
            return 0;
    }
}

/**
 * @brief map link keys to authenticated
 */
int gap_authenticated_for_link_key_type(link_key_type_t link_key_type){
    switch (link_key_type){
        case AUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P256:
        case AUTHENTICATED_COMBINATION_KEY_GENERATED_FROM_P192:
            return 1;
        default:
            return 0;
    }
}

int gap_mitm_protection_required_for_security_level(gap_security_level_t level){
    log_info("gap_mitm_protection_required_for_security_level %u", level);
    return level > LEVEL_2;
}

/**
 * @brief get current security level
 */
gap_security_level_t gap_security_level(btstack_state_t *btstack, hci_con_handle_t con_handle){
    hci_connection_t * connection = hci_connection_for_handle(btstack, con_handle);
    if (!connection) return LEVEL_0;
    return gap_security_level_for_connection(btstack, connection);
}

/**
 * @brief request connection to device to
 * @result GAP_AUTHENTICATION_RESULT
 */
void gap_request_security_level(btstack_state_t *btstack, hci_con_handle_t con_handle, gap_security_level_t requested_level){
    hci_connection_t * connection = hci_connection_for_handle(btstack, con_handle);
    if (!connection){
        hci_emit_security_level(btstack, con_handle, LEVEL_0);
        return;
    }
    gap_security_level_t current_level = gap_security_level(btstack, con_handle);
    log_info("gap_request_security_level requested level %u, planned level %u, current level %u",
        requested_level, connection->requested_security_level, current_level);

    // assumption: earlier requested security higher than current level => security request is active
    if (current_level < connection->requested_security_level){
        if (connection->requested_security_level < requested_level){
            // increase requested level as new level is higher

            // TODO: handle re-authentication when done

            connection->requested_security_level = requested_level;
        }
        return;
    }

    // no request active, notify if security sufficient
    if (requested_level <= current_level){
        hci_emit_security_level(btstack, con_handle, current_level);
        return;
    }

    // start pairing to increase security level
    connection->requested_security_level = requested_level;

#if 0
    // sending encryption request without a link key results in an error.
    // TODO: figure out how to use it properly

    // would enabling ecnryption suffice (>= LEVEL_2)?
    if (btstack->hci->link_key_db){
        link_key_type_t link_key_type;
        link_key_t      link_key;
        if (btstack->hci->link_key_db->get_link_key( &connection->address, &link_key, &link_key_type)){
            if (gap_security_level_for_link_key_type(link_key_type) >= requested_level){
                connection->bonding_flags |= BONDING_SEND_ENCRYPTION_REQUEST;
                return;
            }
        }
    }
#endif

    // start to authenticate connection
    connection->bonding_flags |= BONDING_SEND_AUTHENTICATE_REQUEST;
    hci_run(btstack);
}

/**
 * @brief start dedicated bonding with device. disconnect after bonding
 * @param device
 * @param request MITM protection
 * @result GAP_DEDICATED_BONDING_COMPLETE
 */
int gap_dedicated_bonding(btstack_state_t *btstack, bd_addr_t device, int mitm_protection_required){

    // create connection state machine
    hci_connection_t * connection = create_connection_for_bd_addr_and_type(btstack, device, BD_ADDR_TYPE_ACL);

    if (!connection){
        return BTSTACK_MEMORY_ALLOC_FAILED;
    }

    // delete linkn key
    gap_drop_link_key_for_bd_addr(btstack, device);

    // configure LEVEL_2/3, dedicated bonding
    connection->state = SEND_CREATE_CONNECTION;
    connection->requested_security_level = mitm_protection_required ? LEVEL_3 : LEVEL_2;
    log_info("gap_dedicated_bonding, mitm %d -> level %u", mitm_protection_required, connection->requested_security_level);
    connection->bonding_flags = BONDING_DEDICATED;

    // wait for GAP Security Result and send GAP Dedicated Bonding complete

    // handle: connnection failure (connection complete != ok)
    // handle: authentication failure
    // handle: disconnect on done

    hci_run(btstack);

    return 0;
}
#endif

void gap_set_local_name(btstack_state_t *btstack, const char * local_name){
    btstack->hci->local_name = local_name;
}


#ifdef ENABLE_BLE

#ifdef ENABLE_LE_CENTRAL
void gap_start_scan(btstack_state_t *btstack){
    btstack->hci->le_scanning_enabled = 1;
    hci_run(btstack);
}

void gap_stop_scan(btstack_state_t *btstack){
    btstack->hci->le_scanning_enabled = 0;
    hci_run(btstack);
}

void gap_set_scan_parameters(btstack_state_t *btstack, uint8_t scan_type, uint16_t scan_interval, uint16_t scan_window){
    btstack->hci->le_scan_type     = scan_type;
    btstack->hci->le_scan_interval = scan_interval;
    btstack->hci->le_scan_window   = scan_window;
    hci_run(btstack);
}

uint8_t gap_connect(btstack_state_t *btstack, bd_addr_t addr, bd_addr_type_t addr_type){
    hci_connection_t * conn = hci_connection_for_bd_addr_and_type(btstack, addr, addr_type);
    if (!conn){
        log_info("gap_connect: no connection exists yet, creating context");
        conn = create_connection_for_bd_addr_and_type(btstack, addr, addr_type);
        if (!conn){
            // notify client that alloc failed
            hci_emit_le_connection_complete(btstack, addr_type, addr, 0, BTSTACK_MEMORY_ALLOC_FAILED);
            log_info("gap_connect: failed to alloc hci_connection_t");
            return GATT_CLIENT_NOT_CONNECTED; // don't sent packet to controller
        }
        conn->state = SEND_CREATE_CONNECTION;
        log_info("gap_connect: send create connection next");
        hci_run(btstack);
        return ERROR_CODE_SUCCESS;
    }

    if (!hci_is_le_connection(conn) ||
        (conn->state == SEND_CREATE_CONNECTION) ||
        (conn->state == SENT_CREATE_CONNECTION)) {
        hci_emit_le_connection_complete(btstack, conn->address_type, conn->address, 0, ERROR_CODE_COMMAND_DISALLOWED);
        log_error("gap_connect: classic connection or connect is already being created");
        return GATT_CLIENT_IN_WRONG_STATE;
    }

    // check if connection was just disconnected
    if (conn->state == RECEIVED_DISCONNECTION_COMPLETE){
        log_info("gap_connect: send create connection (again)");
        conn->state = SEND_CREATE_CONNECTION;
        hci_run(btstack);
        return ERROR_CODE_SUCCESS;
    }

    log_info("gap_connect: context exists with state %u", conn->state);
    hci_emit_le_connection_complete(btstack, conn->address_type, conn->address, conn->con_handle, 0);
    hci_run(btstack);
    return ERROR_CODE_SUCCESS;
}

// @assumption: only a single outgoing LE Connection exists
static hci_connection_t * gap_get_outgoing_connection(btstack_state_t *btstack){
    btstack_linked_item_t *it;
    for (it = (btstack_linked_item_t *) btstack->hci->connections; it != NULL; it = it->next){
        hci_connection_t * conn = (hci_connection_t *) it;
        if (!hci_is_le_connection(conn)) continue;
        switch (conn->state){
            case SEND_CREATE_CONNECTION:
            case SENT_CREATE_CONNECTION:
            case SENT_CANCEL_CONNECTION:
                return conn;
            default:
                break;
        };
    }
    return NULL;
}

uint8_t gap_connect_cancel(btstack_state_t *btstack){
    hci_connection_t * conn = gap_get_outgoing_connection(btstack);
    if (!conn) return 0;
    switch (conn->state){
        case SEND_CREATE_CONNECTION:
            // skip sending create connection and emit event instead
            hci_emit_le_connection_complete(btstack, conn->address_type, conn->address, 0, ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER);
            btstack_linked_list_remove(&btstack->hci->connections, (btstack_linked_item_t *) conn);
            btstack_memory_hci_connection_free( conn );
            break;
        case SENT_CREATE_CONNECTION:
            // request to send cancel connection
            conn->state = SEND_CANCEL_CONNECTION;
            hci_run(btstack);
            break;
        default:
            break;
    }
    return 0;
}
#endif

#ifdef ENABLE_LE_CENTRAL
/**
 * @brief Set connection parameters for outgoing connections
 * @param conn_scan_interval (unit: 0.625 msec), default: 60 ms
 * @param conn_scan_window (unit: 0.625 msec), default: 30 ms
 * @param conn_interval_min (unit: 1.25ms), default: 10 ms
 * @param conn_interval_max (unit: 1.25ms), default: 30 ms
 * @param conn_latency, default: 4
 * @param supervision_timeout (unit: 10ms), default: 720 ms
 * @param min_ce_length (unit: 0.625ms), default: 10 ms
 * @param max_ce_length (unit: 0.625ms), default: 30 ms
 */

void gap_set_connection_parameters(btstack_state_t *btstack, uint16_t conn_scan_interval, uint16_t conn_scan_window,
    uint16_t conn_interval_min, uint16_t conn_interval_max, uint16_t conn_latency,
    uint16_t supervision_timeout, uint16_t min_ce_length, uint16_t max_ce_length){
    btstack->hci->le_connection_scan_interval = conn_scan_interval;
    btstack->hci->le_connection_scan_window = conn_scan_window;
    btstack->hci->le_connection_interval_min = conn_interval_min;
    btstack->hci->le_connection_interval_max = conn_interval_max;
    btstack->hci->le_connection_latency = conn_latency;
    btstack->hci->le_supervision_timeout = supervision_timeout;
    btstack->hci->le_minimum_ce_length = min_ce_length;
    btstack->hci->le_maximum_ce_length = max_ce_length;
}
#endif

/**
 * @brief Updates the connection parameters for a given LE connection
 * @param handle
 * @param conn_interval_min (unit: 1.25ms)
 * @param conn_interval_max (unit: 1.25ms)
 * @param conn_latency
 * @param supervision_timeout (unit: 10ms)
 * @returns 0 if ok
 */
int gap_update_connection_parameters(btstack_state_t *btstack, hci_con_handle_t con_handle, uint16_t conn_interval_min,
    uint16_t conn_interval_max, uint16_t conn_latency, uint16_t supervision_timeout){
    hci_connection_t * connection = hci_connection_for_handle(btstack, con_handle);
    if (!connection) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    connection->le_conn_interval_min = conn_interval_min;
    connection->le_conn_interval_max = conn_interval_max;
    connection->le_conn_latency = conn_latency;
    connection->le_supervision_timeout = supervision_timeout;
    connection->le_con_parameter_update_state = CON_PARAMETER_UPDATE_CHANGE_HCI_CON_PARAMETERS;
    hci_run(btstack);
    return 0;
}

/**
 * @brief Request an update of the connection parameter for a given LE connection
 * @param handle
 * @param conn_interval_min (unit: 1.25ms)
 * @param conn_interval_max (unit: 1.25ms)
 * @param conn_latency
 * @param supervision_timeout (unit: 10ms)
 * @returns 0 if ok
 */
int gap_request_connection_parameter_update(btstack_state_t *btstack, hci_con_handle_t con_handle, uint16_t conn_interval_min,
    uint16_t conn_interval_max, uint16_t conn_latency, uint16_t supervision_timeout){
    hci_connection_t * connection = hci_connection_for_handle(btstack, con_handle);
    if (!connection) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;
    connection->le_conn_interval_min = conn_interval_min;
    connection->le_conn_interval_max = conn_interval_max;
    connection->le_conn_latency = conn_latency;
    connection->le_supervision_timeout = supervision_timeout;
    connection->le_con_parameter_update_state = CON_PARAMETER_UPDATE_SEND_REQUEST;
    uint8_t l2cap_trigger_run_event[2] = { L2CAP_EVENT_TRIGGER_RUN, 0};
    hci_emit_event(btstack, l2cap_trigger_run_event, sizeof(l2cap_trigger_run_event), 0);
    return 0;
}

#ifdef ENABLE_LE_PERIPHERAL

static void gap_advertisments_changed(void){
    // disable advertisements before updating adv, scan data, or adv params
    if (btstack->hci->le_advertisements_active){
        btstack->hci->le_advertisements_todo |= LE_ADVERTISEMENT_TASKS_DISABLE | LE_ADVERTISEMENT_TASKS_ENABLE;
    }
    hci_run(btstack);
}

/**
 * @brief Set Advertisement Data
 * @param advertising_data_length
 * @param advertising_data (max 31 octets)
 * @note data is not copied, pointer has to stay valid
 */
void gap_advertisements_set_data(uint8_t advertising_data_length, uint8_t * advertising_data){
    btstack->hci->le_advertisements_data_len = advertising_data_length;
    btstack->hci->le_advertisements_data = advertising_data;
    btstack->hci->le_advertisements_todo |= LE_ADVERTISEMENT_TASKS_SET_ADV_DATA;
    gap_advertisments_changed();
}

/**
 * @brief Set Scan Response Data
 * @param advertising_data_length
 * @param advertising_data (max 31 octets)
 * @note data is not copied, pointer has to stay valid
 */
void gap_scan_response_set_data(uint8_t scan_response_data_length, uint8_t * scan_response_data){
    btstack->hci->le_scan_response_data_len = scan_response_data_length;
    btstack->hci->le_scan_response_data = scan_response_data;
    btstack->hci->le_advertisements_todo |= LE_ADVERTISEMENT_TASKS_SET_SCAN_DATA;
    gap_advertisments_changed();
}

/**
 * @brief Set Advertisement Parameters
 * @param adv_int_min
 * @param adv_int_max
 * @param adv_type
 * @param direct_address_type
 * @param direct_address
 * @param channel_map
 * @param filter_policy
 *
 * @note internal use. use gap_advertisements_set_params from gap_le.h instead.
 */
 void hci_le_advertisements_set_params(uint16_t adv_int_min, uint16_t adv_int_max, uint8_t adv_type,
    uint8_t direct_address_typ, bd_addr_t direct_address,
    uint8_t channel_map, uint8_t filter_policy) {

    btstack->hci->le_advertisements_interval_min = adv_int_min;
    btstack->hci->le_advertisements_interval_max = adv_int_max;
    btstack->hci->le_advertisements_type = adv_type;
    btstack->hci->le_advertisements_direct_address_type = direct_address_typ;
    btstack->hci->le_advertisements_channel_map = channel_map;
    btstack->hci->le_advertisements_filter_policy = filter_policy;
    (void)memcpy(btstack->hci->le_advertisements_direct_address, direct_address,
                 6);

    btstack->hci->le_advertisements_todo |= LE_ADVERTISEMENT_TASKS_SET_PARAMS;
    gap_advertisments_changed();
 }

/**
 * @brief Enable/Disable Advertisements
 * @param enabled
 */
void gap_advertisements_enable(int enabled){
    btstack->hci->le_advertisements_enabled = enabled;
    if (enabled && !btstack->hci->le_advertisements_active){
        btstack->hci->le_advertisements_todo |= LE_ADVERTISEMENT_TASKS_ENABLE;
    }
    if (!enabled && btstack->hci->le_advertisements_active){
        btstack->hci->le_advertisements_todo |= LE_ADVERTISEMENT_TASKS_DISABLE;
    }
    hci_run(btstack);
}

#endif

void hci_le_set_own_address_type(btstack_state_t *btstack, uint8_t own_address_type){
    log_info("hci_le_set_own_address_type: old %u, new %u", btstack->hci->le_own_addr_type, own_address_type);
    if (own_address_type == btstack->hci->le_own_addr_type) return;
    btstack->hci->le_own_addr_type = own_address_type;

#ifdef ENABLE_LE_PERIPHERAL
    // update advertisement parameters, too
    btstack->hci->le_advertisements_todo |= LE_ADVERTISEMENT_TASKS_SET_PARAMS;
    gap_advertisments_changed();
#endif
#ifdef ENABLE_LE_CENTRAL
    // note: we don't update scan parameters or modify ongoing connection attempts
#endif
}

#endif

uint8_t gap_disconnect(btstack_state_t *btstack, hci_con_handle_t handle){
    hci_connection_t * conn = hci_connection_for_handle(btstack, handle);
    if (!conn){
        hci_emit_disconnection_complete(btstack, handle, 0);
        return 0;
    }
    // ignore if already disconnected
    if (conn->state == RECEIVED_DISCONNECTION_COMPLETE){
        return 0;
    }
    conn->state = SEND_DISCONNECT;
    hci_run(btstack);
    return 0;
}

int gap_read_rssi(btstack_state_t *btstack, hci_con_handle_t con_handle){
    hci_connection_t * hci_connection = hci_connection_for_handle(btstack, con_handle);
    if (hci_connection == NULL) return 0;
    connectionSetAuthenticationFlags(hci_connection, READ_RSSI);
    hci_run(btstack);
    return 1;
}

/**
 * @brief Get connection type
 * @param con_handle
 * @result connection_type
 */
gap_connection_type_t gap_get_connection_type(btstack_state_t *btstack, hci_con_handle_t connection_handle){
    hci_connection_t * conn = hci_connection_for_handle(btstack, connection_handle);
    if (!conn) return GAP_CONNECTION_INVALID;
    switch (conn->address_type){
        case BD_ADDR_TYPE_LE_PUBLIC:
        case BD_ADDR_TYPE_LE_RANDOM:
            return GAP_CONNECTION_LE;
        case BD_ADDR_TYPE_SCO:
            return GAP_CONNECTION_SCO;
        case BD_ADDR_TYPE_ACL:
            return GAP_CONNECTION_ACL;
        default:
            return GAP_CONNECTION_INVALID;
    }
}

#ifdef ENABLE_BLE

uint8_t gap_le_set_phy(btstack_state_t *btstack, hci_con_handle_t connection_handle, uint8_t all_phys, uint8_t tx_phys, uint8_t rx_phys, uint8_t phy_options){
    hci_connection_t * conn = hci_connection_for_handle(btstack, connection_handle);
    if (!conn) return ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER;

    conn->le_phy_update_all_phys    = all_phys;
    conn->le_phy_update_tx_phys     = tx_phys;
    conn->le_phy_update_rx_phys     = rx_phys;
    conn->le_phy_update_phy_options = phy_options;

    hci_run(btstack);

    return 0;
}

#ifdef ENABLE_LE_CENTRAL
/**
 * @brief Auto Connection Establishment - Start Connecting to device
 * @param address_typ
 * @param address
 * @returns 0 if ok
 */
int gap_auto_connection_start(btstack_state_t *btstack, bd_addr_type_t address_type, bd_addr_t address){
    // check capacity
    int num_entries = btstack_linked_list_count(&btstack->hci->le_whitelist);
    if (num_entries >= btstack->hci->le_whitelist_capacity) return ERROR_CODE_MEMORY_CAPACITY_EXCEEDED;
    whitelist_entry_t * entry = btstack_memory_whitelist_entry_get();
    if (!entry) return BTSTACK_MEMORY_ALLOC_FAILED;
    entry->address_type = address_type;
    (void)memcpy(entry->address, address, 6);
    entry->state = LE_WHITELIST_ADD_TO_CONTROLLER;
    btstack_linked_list_add(&btstack->hci->le_whitelist, (btstack_linked_item_t*) entry);
    hci_run(btstack);
    return 0;
}

static void hci_remove_from_whitelist(btstack_state_t *btstack, bd_addr_type_t address_type, bd_addr_t address){
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, &btstack->hci->le_whitelist);
    while (btstack_linked_list_iterator_has_next(&it)){
        whitelist_entry_t * entry = (whitelist_entry_t*) btstack_linked_list_iterator_next(&it);
        if (entry->address_type != address_type) continue;
        if (memcmp(entry->address, address, 6) != 0) continue;
        if (entry->state & LE_WHITELIST_ON_CONTROLLER){
            // remove from controller if already present
            entry->state |= LE_WHITELIST_REMOVE_FROM_CONTROLLER;
            continue;
        }
        // direclty remove entry from whitelist
        btstack_linked_list_iterator_remove(&it);
        btstack_memory_whitelist_entry_free(entry);
    }
}

/**
 * @brief Auto Connection Establishment - Stop Connecting to device
 * @param address_typ
 * @param address
 * @returns 0 if ok
 */
int gap_auto_connection_stop(btstack_state_t *btstack, bd_addr_type_t address_type, bd_addr_t address){
    hci_remove_from_whitelist(btstack, address_type, address);
    hci_run(btstack);
    return 0;
}

/**
 * @brief Auto Connection Establishment - Stop everything
 * @note  Convenience function to stop all active auto connection attempts
 */
void gap_auto_connection_stop_all(btstack_state_t *btstack){
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, &btstack->hci->le_whitelist);
    while (btstack_linked_list_iterator_has_next(&it)){
        whitelist_entry_t * entry = (whitelist_entry_t*) btstack_linked_list_iterator_next(&it);
        if (entry->state & LE_WHITELIST_ON_CONTROLLER){
            // remove from controller if already present
            entry->state |= LE_WHITELIST_REMOVE_FROM_CONTROLLER;
            continue;
        }
        // directly remove entry from whitelist
        btstack_linked_list_iterator_remove(&it);
        btstack_memory_whitelist_entry_free(entry);
    }
    hci_run(btstack);
}

uint16_t gap_le_connection_interval(btstack_state_t *btstack, hci_con_handle_t connection_handle){
    hci_connection_t * conn = hci_connection_for_handle(btstack, connection_handle);
    if (!conn) return 0;
    return conn->le_connection_interval;
}
#endif
#endif

#ifdef ENABLE_CLASSIC
/**
 * @brief Set Extended Inquiry Response data
 * @param eir_data size HCI_EXTENDED_INQUIRY_RESPONSE_DATA_LEN (240) bytes, is not copied make sure memory is accessible during stack startup
 * @note has to be done before stack starts up
 */
void gap_set_extended_inquiry_response(btstack_state_t *btstack, const uint8_t * data){
    btstack->hci->eir_data = data;
}

/**
 * @brief Start GAP Classic Inquiry
 * @param duration in 1.28s units
 * @return 0 if ok
 * @events: GAP_EVENT_INQUIRY_RESULT, GAP_EVENT_INQUIRY_COMPLETE
 */
int gap_inquiry_start(btstack_state_t *btstack, uint8_t duration_in_1280ms_units){
    if (btstack->hci->state != HCI_STATE_WORKING) return ERROR_CODE_COMMAND_DISALLOWED;
    if (btstack->hci->inquiry_state != GAP_INQUIRY_STATE_IDLE) return ERROR_CODE_COMMAND_DISALLOWED;
    if ((duration_in_1280ms_units < GAP_INQUIRY_DURATION_MIN) || (duration_in_1280ms_units > GAP_INQUIRY_DURATION_MAX)){
        return ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS;
    }
    btstack->hci->inquiry_state = duration_in_1280ms_units;
    hci_run(btstack);
    return 0;
}

/**
 * @brief Stop GAP Classic Inquiry
 * @returns 0 if ok
 */
int gap_inquiry_stop(btstack_state_t *btstack){
    if ((btstack->hci->inquiry_state >= GAP_INQUIRY_DURATION_MIN) && (btstack->hci->inquiry_state <= GAP_INQUIRY_DURATION_MAX)) {
        // emit inquiry complete event, before it even started
        uint8_t event[] = { GAP_EVENT_INQUIRY_COMPLETE, 1, 0};
        hci_emit_event(btstack, event, sizeof(event), 1);
        return 0;
    }
    if (btstack->hci->inquiry_state != GAP_INQUIRY_STATE_ACTIVE) return ERROR_CODE_COMMAND_DISALLOWED;
    btstack->hci->inquiry_state = GAP_INQUIRY_STATE_W2_CANCEL;
    hci_run(btstack);
    return 0;
}


/**
 * @brief Remote Name Request
 * @param addr
 * @param page_scan_repetition_mode
 * @param clock_offset only used when bit 15 is set
 * @events: HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE
 */
int gap_remote_name_request(btstack_state_t *btstack, bd_addr_t addr, uint8_t page_scan_repetition_mode, uint16_t clock_offset){
    if (btstack->hci->remote_name_state != GAP_REMOTE_NAME_STATE_IDLE) return ERROR_CODE_COMMAND_DISALLOWED;
    (void)memcpy(btstack->hci->remote_name_addr, addr, 6);
    btstack->hci->remote_name_page_scan_repetition_mode = page_scan_repetition_mode;
    btstack->hci->remote_name_clock_offset = clock_offset;
    btstack->hci->remote_name_state = GAP_REMOTE_NAME_STATE_W2_SEND;
    hci_run(btstack);
    return 0;
}

static int gap_pairing_set_state_and_run(btstack_state_t *btstack, bd_addr_t addr, uint8_t state){
    btstack->hci->gap_pairing_state = state;
    (void)memcpy(btstack->hci->gap_pairing_addr, addr, 6);
    hci_run(btstack);
    return 0;
}

/**
 * @brief Legacy Pairing Pin Code Response
 * @param addr
 * @param pin
 * @return 0 if ok
 */
int gap_pin_code_response(btstack_state_t *btstack, bd_addr_t addr, const char * pin){
    if (btstack->hci->gap_pairing_state != GAP_PAIRING_STATE_IDLE) return ERROR_CODE_COMMAND_DISALLOWED;
    btstack->hci->gap_pairing_input.gap_pairing_pin = pin;
    return gap_pairing_set_state_and_run(btstack, addr, GAP_PAIRING_STATE_SEND_PIN);
}

/**
 * @brief Abort Legacy Pairing
 * @param addr
 * @param pin
 * @return 0 if ok
 */
int gap_pin_code_negative(btstack_state_t *btstack, bd_addr_t addr){
    if (btstack->hci->gap_pairing_state != GAP_PAIRING_STATE_IDLE) return ERROR_CODE_COMMAND_DISALLOWED;
    return gap_pairing_set_state_and_run(btstack, addr, GAP_PAIRING_STATE_SEND_PIN_NEGATIVE);
}

/**
 * @brief SSP Passkey Response
 * @param addr
 * @param passkey
 * @return 0 if ok
 */
int gap_ssp_passkey_response(btstack_state_t *btstack, bd_addr_t addr, uint32_t passkey){
    if (btstack->hci->gap_pairing_state != GAP_PAIRING_STATE_IDLE) return ERROR_CODE_COMMAND_DISALLOWED;
    btstack->hci->gap_pairing_input.gap_pairing_passkey = passkey;
    return gap_pairing_set_state_and_run(btstack, addr, GAP_PAIRING_STATE_SEND_PASSKEY);
}

/**
 * @brief Abort SSP Passkey Entry/Pairing
 * @param addr
 * @param pin
 * @return 0 if ok
 */
int gap_ssp_passkey_negative(btstack_state_t *btstack, bd_addr_t addr){
    if (btstack->hci->gap_pairing_state != GAP_PAIRING_STATE_IDLE) return ERROR_CODE_COMMAND_DISALLOWED;
    return gap_pairing_set_state_and_run(btstack, addr, GAP_PAIRING_STATE_SEND_PASSKEY_NEGATIVE);
}

/**
 * @brief Accept SSP Numeric Comparison
 * @param addr
 * @param passkey
 * @return 0 if ok
 */
int gap_ssp_confirmation_response(btstack_state_t *btstack, bd_addr_t addr){
    if (btstack->hci->gap_pairing_state != GAP_PAIRING_STATE_IDLE) return ERROR_CODE_COMMAND_DISALLOWED;
    return gap_pairing_set_state_and_run(btstack, addr, GAP_PAIRING_STATE_SEND_CONFIRMATION);
}

/**
 * @brief Abort SSP Numeric Comparison/Pairing
 * @param addr
 * @param pin
 * @return 0 if ok
 */
int gap_ssp_confirmation_negative(btstack_state_t *btstack, bd_addr_t addr){
    if (btstack->hci->gap_pairing_state != GAP_PAIRING_STATE_IDLE) return ERROR_CODE_COMMAND_DISALLOWED;
    return gap_pairing_set_state_and_run(btstack, addr, GAP_PAIRING_STATE_SEND_CONFIRMATION_NEGATIVE);
}

/**
 * @brief Set inquiry mode: standard, with RSSI, with RSSI + Extended Inquiry Results. Has to be called before power on.
 * @param inquiry_mode see bluetooth_defines.h
 */
void hci_set_inquiry_mode(btstack_state_t *btstack, inquiry_mode_t mode){
    btstack->hci->inquiry_mode = mode;
}

/**
 * @brief Configure Voice Setting for use with SCO data in HSP/HFP
 */
void hci_set_sco_voice_setting(btstack_state_t *btstack, uint16_t voice_setting){
    btstack->hci->sco_voice_setting = voice_setting;
}

/**
 * @brief Get SCO Voice Setting
 * @return current voice setting
 */
uint16_t hci_get_sco_voice_setting(btstack_state_t *btstack){
    return btstack->hci->sco_voice_setting;
}

static int hci_have_usb_transport(btstack_state_t *btstack){
    if (!btstack->hci->hci_transport) return 0;
    const char * transport_name = btstack->hci->hci_transport->name;
    if (!transport_name) return 0;
    return (transport_name[0] == 'H') && (transport_name[1] == '2');
}

/** @brief Get SCO packet length for current SCO Voice setting
 *  @note  Using SCO packets of the exact length is required for USB transfer
 *  @return Length of SCO packets in bytes (not audio frames)
 */
int hci_get_sco_packet_length(void){
    int sco_packet_length = 0;

#ifdef ENABLE_SCO_OVER_HCI

    // Transparent = mSBC => 1, CVSD with 16-bit samples requires twice as much bytes
    int multiplier = ((btstack->hci->sco_voice_setting_active & 0x03) == 0x03) ? 1 : 2;

    if (hci_have_usb_transport()){
        // see Core Spec for H2 USB Transfer.
        // 3 byte SCO header + 24 bytes per connection
        int num_sco_connections = btstack_max(1, hci_number_sco_connections());
        sco_packet_length = 3 + 24 * num_sco_connections * multiplier;
    } else {
        // 3 byte SCO header + SCO packet size over the air (60 bytes)
        sco_packet_length = 3 + 60 * multiplier;
        // assert that it still fits inside an SCO buffer
        if (sco_packet_length > btstack->hci->sco_data_packet_length){
            sco_packet_length = 3 + 60;
        }
    }
#endif
    return sco_packet_length;
}

/**
* @brief Sets the master/slave policy
* @param policy (0: attempt to become master, 1: let connecting device decide)
*/
void hci_set_master_slave_policy(btstack_state_t *btstack, uint8_t policy){
    btstack->hci->master_slave_policy = policy;
}

#endif

HCI_STATE hci_get_state(btstack_state_t *btstack){
    return btstack->hci->state;
}

#ifdef ENABLE_CLASSIC
void gap_register_classic_connection_filter(btstack_state_t *btstack, int (*accept_callback)(bd_addr_t addr)){
    btstack->hci->gap_classic_accept_callback = accept_callback;
}
#endif

/**
 * @brief Set callback for Bluetooth Hardware Error
 */
void hci_set_hardware_error_callback(btstack_state_t *btstack, void (*fn)(uint8_t error)){
    btstack->hci->hardware_error_callback = fn;
}

void hci_disconnect_all(btstack_state_t *btstack){
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, &btstack->hci->connections);
    while (btstack_linked_list_iterator_has_next(&it)){
        hci_connection_t * con = (hci_connection_t*) btstack_linked_list_iterator_next(&it);
        if (con->state == SENT_DISCONNECT) continue;
        con->state = SEND_DISCONNECT;
    }
    hci_run(btstack);
}

uint16_t hci_get_manufacturer(btstack_state_t *btstack){
    return btstack->hci->manufacturer;
}

#ifdef ENABLE_BLE

static sm_connection_t * sm_get_connection_for_handle(btstack_state_t *btstack, hci_con_handle_t con_handle){
    hci_connection_t * hci_con = hci_connection_for_handle(btstack, con_handle);
    if (!hci_con) return NULL;
    return &hci_con->sm_connection;
}

// extracted from sm.c to allow enabling of l2cap le data channels without adding sm.c to the build
// without sm.c default values from create_connection_for_bd_addr_and_type() resulg in non-encrypted, not-authenticated

int gap_encryption_key_size(btstack_state_t *btstack, hci_con_handle_t con_handle){
    hci_connection_t * hci_connection = hci_connection_for_handle(btstack, con_handle);
    if (hci_connection == NULL) return 0;
    if (hci_is_le_connection(hci_connection)){
        sm_connection_t * sm_conn = &hci_connection->sm_connection;
        if (sm_conn->sm_connection_encrypted) {
            return sm_conn->sm_actual_encryption_key_size;
        }
    }
#ifdef ENABLE_CLASSIC
    else {
        if ((hci_connection->authentication_flags & CONNECTION_ENCRYPTED)){
            return hci_connection->encryption_key_size;
        }
    }
#endif
    return 0;
}

int gap_authenticated(btstack_state_t *btstack, hci_con_handle_t con_handle){
    hci_connection_t * hci_connection = hci_connection_for_handle(btstack, con_handle);
    if (hci_connection == NULL) return 0;

    switch (hci_connection->address_type){
        case BD_ADDR_TYPE_LE_PUBLIC:
        case BD_ADDR_TYPE_LE_RANDOM:
            if (hci_connection->sm_connection.sm_connection_encrypted == 0) return 0; // unencrypted connection cannot be authenticated
            return hci_connection->sm_connection.sm_connection_authenticated;
#ifdef ENABLE_CLASSIC
        case BD_ADDR_TYPE_SCO:
        case BD_ADDR_TYPE_ACL:
            return gap_authenticated_for_link_key_type(hci_connection->link_key_type);
#endif
        default:
            return 0;
    }
}

int gap_secure_connection(btstack_state_t *btstack, hci_con_handle_t con_handle){
    hci_connection_t * hci_connection = hci_connection_for_handle(btstack, con_handle);
    if (hci_connection == NULL) return 0;

    switch (hci_connection->address_type){
        case BD_ADDR_TYPE_LE_PUBLIC:
        case BD_ADDR_TYPE_LE_RANDOM:
            if (hci_connection->sm_connection.sm_connection_encrypted == 0) return 0; // unencrypted connection cannot be authenticated
            return hci_connection->sm_connection.sm_connection_sc;
#ifdef ENABLE_CLASSIC
        case BD_ADDR_TYPE_SCO:
        case BD_ADDR_TYPE_ACL:
            return gap_secure_connection_for_link_key_type(hci_connection->link_key_type);
#endif
        default:
            return 0;
    }
}

authorization_state_t gap_authorization_state(btstack_state_t *btstack, hci_con_handle_t con_handle){
    sm_connection_t * sm_conn = sm_get_connection_for_handle(btstack, con_handle);
    if (!sm_conn) return AUTHORIZATION_UNKNOWN;     // wrong connection
    if (!sm_conn->sm_connection_encrypted)               return AUTHORIZATION_UNKNOWN; // unencrypted connection cannot be authorized
    if (!sm_conn->sm_connection_authenticated)           return AUTHORIZATION_UNKNOWN; // unauthenticatd connection cannot be authorized
    return sm_conn->sm_connection_authorization_state;
}
#endif

#ifdef ENABLE_CLASSIC
uint8_t gap_sniff_mode_enter(btstack_state_t *btstack, hci_con_handle_t con_handle, uint16_t sniff_min_interval, uint16_t sniff_max_interval, uint16_t sniff_attempt, uint16_t sniff_timeout){
    hci_connection_t * conn = hci_connection_for_handle(btstack, con_handle);
    if (!conn) return GAP_CONNECTION_INVALID;
    conn->sniff_min_interval = sniff_min_interval;
    conn->sniff_max_interval = sniff_max_interval;
    conn->sniff_attempt = sniff_attempt;
    conn->sniff_timeout = sniff_timeout;
    hci_run(btstack);
    return 0;
}

/**
 * @brief Exit Sniff mode
 * @param con_handle
 @ @return 0 if ok
 */
uint8_t gap_sniff_mode_exit(btstack_state_t *btstack, hci_con_handle_t con_handle){
    hci_connection_t * conn = hci_connection_for_handle(btstack, con_handle);
    if (!conn) return GAP_CONNECTION_INVALID;
    conn->sniff_min_interval = 0xffff;
    hci_run(btstack);
    return 0;
}
#endif

void hci_halting_defer(btstack_state_t *btstack){
    if (btstack->hci->state != HCI_STATE_HALTING) return;
    switch (btstack->hci->substate){
        case HCI_HALTING_DISCONNECT_ALL_NO_TIMER:
        case HCI_HALTING_CLOSE:
            btstack->hci->substate = HCI_HALTING_DISCONNECT_ALL_TIMER;
            break;
        default:
            break;
    }
}

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
void hci_setup_test_connections_fuzz(void){
    hci_connection_t * conn;

    // default address: 66:55:44:33:00:01
    bd_addr_t addr = { 0x66, 0x55, 0x44, 0x33, 0x00, 0x00};

    // setup Controller info
    btstack->hci->num_cmd_packets = 255;
    btstack->hci->acl_packets_total_num = 255;

    // setup incoming Classic ACL connection with con handle 0x0001, 66:55:44:33:22:01
    addr[5] = 0x01;
    conn = create_connection_for_bd_addr_and_type(addr, BD_ADDR_TYPE_ACL);
    conn->con_handle = addr[5];
    conn->role  = HCI_ROLE_SLAVE;
    conn->state = RECEIVED_CONNECTION_REQUEST;
    conn->sm_connection.sm_role = HCI_ROLE_SLAVE;

    // setup incoming Classic SCO connection with con handle 0x0002
    addr[5] = 0x02;
    conn = create_connection_for_bd_addr_and_type(addr, BD_ADDR_TYPE_SCO);
    conn->con_handle = addr[5];
    conn->role  = HCI_ROLE_SLAVE;
    conn->state = RECEIVED_CONNECTION_REQUEST;
    conn->sm_connection.sm_role = HCI_ROLE_SLAVE;

    // setup ready Classic ACL connection with con handle 0x0003
    addr[5] = 0x03;
    conn = create_connection_for_bd_addr_and_type(addr, BD_ADDR_TYPE_ACL);
    conn->con_handle = addr[5];
    conn->role  = HCI_ROLE_SLAVE;
    conn->state = OPEN;
    conn->sm_connection.sm_role = HCI_ROLE_SLAVE;

    // setup ready Classic SCO connection with con handle 0x0004
    addr[5] = 0x04;
    conn = create_connection_for_bd_addr_and_type(addr, BD_ADDR_TYPE_SCO);
    conn->con_handle = addr[5];
    conn->role  = HCI_ROLE_SLAVE;
    conn->state = OPEN;
    conn->sm_connection.sm_role = HCI_ROLE_SLAVE;

    // setup ready LE ACL connection with con handle 0x005 and public address
    addr[5] = 0x05;
    conn = create_connection_for_bd_addr_and_type(addr, BD_ADDR_TYPE_LE_PUBLIC);
    conn->con_handle = addr[5];
    conn->role  = HCI_ROLE_SLAVE;
    conn->state = OPEN;
    conn->sm_connection.sm_role = HCI_ROLE_SLAVE;
}

void hci_free_connections_fuzz(void){
    btstack_linked_list_iterator_t it;
    btstack_linked_list_iterator_init(&it, btstack->hci->connections);
    while (btstack_linked_list_iterator_has_next(&it)){
        hci_connection_t * con = (hci_connection_t*) btstack_linked_list_iterator_next(&it);
        btstack_linked_list_iterator_remove(&it);
        btstack_memory_hci_connection_free(con);
    }
}
void hci_simulate_working_fuzz(void){
    hci_init_done();
    btstack->hci->num_cmd_packets = 255;
}
#endif
