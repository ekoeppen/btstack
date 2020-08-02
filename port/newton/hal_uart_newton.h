/*
 * Copyright (C) 2020 Eckhart Koeppen
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
 */

#pragma once

#include <stdint.h>
#include "btstack_state.h"

#if defined __cplusplus
extern "C" {
#endif

/**
 * @brief Init and open device
 */
void *hal_uart_newton_init(btstack_state_t *btstack);

/**
 * @brief Set baud rate
 * @note During baud change, TX line should stay high and no data should be received on RX accidentally
 * @param baudrate
 */
int  hal_uart_newton_set_baud(btstack_state_t *btstack, uint32_t baud);

#ifdef HAVE_UART_DMA_SET_FLOWCONTROL
/**
 * @brief Set flowcontrol
 * @param flowcontrol enabled
 */
int  hal_uart_newton_set_flowcontrol(int flowcontrol);
#endif

/**
 * @brief Send block. When done, callback set by hal_uart_set_block_sent must be called
 * @param buffer
 * @param lengh
 */
void hal_uart_newton_send_block(btstack_state_t *btstack, const uint8_t *buffer, uint16_t length);

/**
 * @brief Receive block. When done, callback set by hal_uart_newton_set_block_received must be called
 * @param buffer
 * @param lengh
 */
void hal_uart_newton_receive_block(btstack_state_t *btstack, uint8_t *buffer, uint16_t len);

/**
 * @brief Copy received data for further processing
 */
void hal_uart_newton_process_received_data(btstack_state_t *btstack);
#if defined __cplusplus
}
#endif
