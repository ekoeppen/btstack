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

// *****************************************************************************
//
// main.c for msp430f5529-cc256x
//
// *****************************************************************************

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "btstack_config.h"
#include "btstack_event.h"
#include "btstack_memory.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_embedded.h"
#include "bluetooth_company_id.h"
#include "classic/btstack_link_key_db_static.h"
#include "hci.h"
#include "hci_dump.h"
#include "log.h"

void hal_cpu_disable_irqs()
{
}

void hal_cpu_enable_irqs()
{
}

void hal_cpu_enable_irqs_and_sleep()
{
}

unsigned int hal_time_ms()
{
    struct timeval te;
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // calculate milliseconds
    // printf("milliseconds: %lld\n", milliseconds);
    return milliseconds;
}

static hci_transport_config_uart_t config = {
    HCI_TRANSPORT_CONFIG_UART,
    115200,
    1000000,  // main baudrate
    1,        // flow control
    NULL,
};

static void packet_handler (btstack_state_t *btstack, uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    if (packet_type != HCI_EVENT_PACKET) return;
    switch(hci_event_packet_get_type(packet)){
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) return;
            puts("BTstack up and running.\n");
            break;
        case HCI_EVENT_COMMAND_COMPLETE:
            if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_local_version_information)){
                uint16_t manufacturer   = little_endian_read_16(packet, 10);
                uint16_t lmp_subversion = little_endian_read_16(packet, 12);
            }
            break;
        default:
            break;
    }
}

#define HEARTBEAT_PERIOD_MS 1000

void heartbeat_handler(btstack_state_t *btstack, btstack_timer_source_t *ts){
    nwt_log("[####] Heartbeat");
    /*
    btstack_run_loop_set_timer(btstack, ts, HEARTBEAT_PERIOD_MS);
    btstack_run_loop_add_timer(btstack, ts);
    */
    btstack->run_loop->exit = 1;
}
/* LISTING_END */

int main(void){
    btstack_state_t btstack;
    const char * pklg_path = "/tmp/hci_dump.pklg";
    hci_dump_open(pklg_path, HCI_DUMP_PACKETLOGGER);
    printf("Packet Log: %s\n", pklg_path);
    nwt_log ("[####] Starting");
    memset(&btstack, 0, sizeof(btstack));
    btstack_timer_source_t heartbeat;
    btstack_packet_callback_registration_t hci_event_callback_registration;

    /// GET STARTED with BTstack ///
    btstack_memory_init();
    btstack_run_loop_init(&btstack, btstack_run_loop_embedded_get_instance());

    // init HCI
    hci_init(&btstack, hci_transport_h4_instance(&btstack, btstack_uart_block_embedded_instance()), &config);
    nwt_log("[####] HCI init done");
    hci_set_link_key_db(&btstack, btstack_link_key_db_static_instance());

    // inform about BTstack state
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&btstack, &hci_event_callback_registration);

    heartbeat.process = &heartbeat_handler;
    btstack_run_loop_set_timer(&btstack, &heartbeat, HEARTBEAT_PERIOD_MS);
    btstack_run_loop_add_timer(&btstack, &heartbeat);
    hci_power_control(&btstack, HCI_POWER_ON);

    btstack_run_loop_execute(&btstack);
    nwt_log ("[####] Done");
    return 0;
}

