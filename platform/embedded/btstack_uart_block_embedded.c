/*
 * Copyright (C) 2016 BlueKitchen GmbH
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

#define BTSTACK_FILE__ "btstack_uart_block_embedded.c"

/*
 *  btstack_uart_block_embedded.c
 *
 *  Common code to access UART via asynchronous block read/write commands on top of hal_uart_dma.h
 *
 */

#include <stdlib.h>
#include <string.h>
#include "btstack_debug.h"
#include "btstack_uart_block.h"
#include "btstack_run_loop_embedded.h"
#include "hal_uart_dma.h"
#include "log.h"

struct btstack_uart_state {
    // uart config
    const btstack_uart_config_t * uart_config;

    // data source for integration with BTstack Runloop
    btstack_data_source_t transport_data_source;

    int send_complete;
    int receive_complete;
    int wakeup_event;

    // callbacks
    void (*block_sent)(btstack_state_t *btstack);
    void (*block_received)(btstack_state_t *btstack);
    void (*wakeup_handler)(btstack_state_t *btstack);
};

static void btstack_uart_block_received(btstack_state_t *btstack){
    btstack->uart->receive_complete = 1;
    btstack_run_loop_embedded_trigger(btstack);
}

static void btstack_uart_block_sent(btstack_state_t *btstack){
    btstack->uart->send_complete = 1;
    btstack_run_loop_embedded_trigger(btstack);
}

static void btstack_uart_cts_pulse(btstack_state_t *btstack){
    btstack->uart->wakeup_event = 1;
    btstack_run_loop_embedded_trigger(btstack);
}

static int btstack_uart_embedded_init(btstack_state_t *btstack, const btstack_uart_config_t * config){
    btstack->uart = malloc(sizeof(struct btstack_uart_state));
    memset(btstack->uart, 0, sizeof(struct btstack_uart_state));
    btstack->uart->uart_config = config;
    hal_uart_dma_init();
    hal_uart_dma_set_block_received(&btstack_uart_block_received);
    hal_uart_dma_set_block_sent(&btstack_uart_block_sent);
    return 0;
}

static void btstack_uart_embedded_process(btstack_state_t *btstack, btstack_data_source_t *ds, btstack_data_source_callback_type_t callback_type) {
    switch (callback_type){
        case DATA_SOURCE_CALLBACK_POLL:
            if (btstack->uart->send_complete){
                btstack->uart->send_complete = 0;
                if (btstack->uart->block_sent){
                    btstack->uart->block_sent(btstack);
                }
            }
            if (btstack->uart->receive_complete){
                btstack->uart->receive_complete = 0;
                if (btstack->uart->block_received){
                    btstack->uart->block_received(btstack);
                }
            }
            if (btstack->uart->wakeup_event){
                btstack->uart->wakeup_event = 0;
                if (btstack->uart->wakeup_handler){
                    btstack->uart->wakeup_handler(btstack);
                }
            }
            break;
        default:
            break;
    }
}

static int btstack_uart_embedded_open(btstack_state_t *btstack){
    hal_uart_dma_init();
    hal_uart_dma_set_baud(btstack, btstack->uart->uart_config->baudrate);

    // set up polling data_source
    btstack_run_loop_set_data_source_handler(&btstack->uart->transport_data_source, &btstack_uart_embedded_process);
    btstack_run_loop_enable_data_source_callbacks(btstack, &btstack->uart->transport_data_source, DATA_SOURCE_CALLBACK_POLL);
    btstack_run_loop_add_data_source(btstack, &btstack->uart->transport_data_source);
    return 0;
}

static int btstack_uart_embedded_close(btstack_state_t *btstack){

    // remove data source
    btstack_run_loop_disable_data_source_callbacks(btstack, &btstack->uart->transport_data_source, DATA_SOURCE_CALLBACK_POLL);
    btstack_run_loop_remove_data_source(btstack, &btstack->uart->transport_data_source);

    // close device
    // ...
    return 0;
}

static void btstack_uart_embedded_set_block_received(btstack_state_t *btstack, void (*block_handler)(btstack_state_t *btstack)){
    btstack->uart->block_received = block_handler;
}

static void btstack_uart_embedded_set_block_sent(btstack_state_t *btstack, void (*block_handler)(btstack_state_t *btstack)){
    btstack->uart->block_sent = block_handler;
}

static void btstack_uart_embedded_set_wakeup_handler(btstack_state_t *btstack, void (*the_wakeup_handler)(btstack_state_t *btstack)){
    btstack->uart->wakeup_handler = the_wakeup_handler;
}

static int btstack_uart_embedded_set_parity(btstack_state_t *btstack, int parity){
    return 0;
}

static void btstack_uart_embedded_send_block(btstack_state_t *btstack, const uint8_t *data, uint16_t size){
    hal_uart_dma_send_block(btstack, data, size);
}

static void btstack_uart_embedded_receive_block(btstack_state_t *btstack, uint8_t *buffer, uint16_t len){
    hal_uart_dma_receive_block(btstack, buffer, len);
}

static int btstack_uart_embedded_get_supported_sleep_modes(void){
#ifdef HAVE_HAL_UART_DMA_SLEEP_MODES
	return hal_uart_dma_get_supported_sleep_modes();
#else
	return BTSTACK_UART_SLEEP_MASK_RTS_HIGH_WAKE_ON_CTS_PULSE;
#endif
}

static void btstack_uart_embedded_set_sleep(btstack_uart_sleep_mode_t sleep_mode){
	log_info("set sleep %u", sleep_mode);
	if (sleep_mode == BTSTACK_UART_SLEEP_RTS_HIGH_WAKE_ON_CTS_PULSE){
		hal_uart_dma_set_csr_irq_handler(&btstack_uart_cts_pulse);
	} else {
		hal_uart_dma_set_csr_irq_handler(NULL);
	}

#ifdef HAVE_HAL_UART_DMA_SLEEP_MODES
	hal_uart_dma_set_sleep_mode(sleep_mode);
#else
	hal_uart_dma_set_sleep(sleep_mode != BTSTACK_UART_SLEEP_OFF);
#endif
	log_info("done");
}

static const btstack_uart_block_t btstack_uart_embedded = {
    /* int  (*init)(hci_transport_config_uart_t * config); */         &btstack_uart_embedded_init,
    /* int  (*open)(void); */                                         &btstack_uart_embedded_open,
    /* int  (*close)(void); */                                        &btstack_uart_embedded_close,
    /* void (*set_block_received)(void (*handler)(void)); */          &btstack_uart_embedded_set_block_received,
    /* void (*set_block_sent)(void (*handler)(void)); */              &btstack_uart_embedded_set_block_sent,
    /* int  (*set_baudrate)(uint32_t baudrate); */                    &hal_uart_dma_set_baud,
    /* int  (*set_parity)(int parity); */                             &btstack_uart_embedded_set_parity,
#ifdef HAVE_UART_DMA_SET_FLOWCONTROL
    /* int  (*set_flowcontrol)(int flowcontrol); */                   &hal_uart_dma_set_flowcontrol,
#else
    /* int  (*set_flowcontrol)(int flowcontrol); */                   NULL,
#endif
    /* void (*receive_block)(uint8_t *buffer, uint16_t len); */       &btstack_uart_embedded_receive_block,
    /* void (*send_block)(const uint8_t *buffer, uint16_t length); */ &btstack_uart_embedded_send_block,
    /* int (*get_supported_sleep_modes); */                           &btstack_uart_embedded_get_supported_sleep_modes,
    /* void (*set_sleep)(btstack_uart_sleep_mode_t sleep_mode); */    &btstack_uart_embedded_set_sleep,
    /* void (*set_wakeup_handler)(void (*handler)(void)); */          &btstack_uart_embedded_set_wakeup_handler,
};

const btstack_uart_block_t * btstack_uart_block_embedded_instance(void){
	return &btstack_uart_embedded;
}
