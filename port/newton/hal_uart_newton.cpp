/*
 * Copyright (C) 202 Eckhart Koeppen
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

#include <stdint.h>

#include "btstack_state.h"
#include "btstack_uart_block_newton.h"
#include "hal_uart_newton.h"
#include "hal_newton.h"
#include "log.h"
#include "stdio.h"

#include "SerialChipRegistry.h"
#include "EventsCommands.h"

#define kCMOSerialEinsteinLoc 'eloc'

class THMOSerialEinsteinHardware : public TOption
{
    public:
        THMOSerialEinsteinHardware();
        ULong   fLocationID;    // synthetic hardware location
        ULong   fType;          // host side driver type
};

THMOSerialEinsteinHardware::THMOSerialEinsteinHardware()
{
    SetAsOption(kCMOSerialEinsteinLoc);
    SetLength(sizeof(THMOSerialEinsteinHardware));
}

void TxBEmptyIntHandler(void* context)
{
    btstack_state_t *btstack = static_cast<btstack_state_t*>(context);
    einstein_here(90, __func__, __LINE__);
}

void RxCAvailIntHandler(void* context)
{
    btstack_state_t *btstack = static_cast<btstack_state_t*>(context);
    einstein_log(90, __func__, __LINE__, "%d", btstack->hal->num_bytes_to_read);
    TSerialChip *chip = static_cast<TSerialChip*>(btstack->uart->serial_chip);
    while (chip->RxBufFull()) {
        uint8_t b = chip->GetByte();
        btstack->hal->read_buffer[btstack->hal->read_buffer_head] = b;
        if (btstack->hal->read_buffer_head < sizeof(btstack->hal->read_buffer)) {
            ++btstack->hal->read_buffer_head;
        } else {
            btstack->hal->read_buffer_head = 0;
        }
    }
    if (btstack->hal->num_bytes_to_read == 0) {
        return;
    }
    while (btstack->hal->num_bytes_to_read > 0) {
        uint8_t b = btstack->hal->read_buffer[btstack->hal->read_buffer_tail];
        *btstack->hal->bytes_to_read = b;
        ++btstack->hal->bytes_to_read;
        if (btstack->hal->read_buffer_tail < sizeof(btstack->hal->read_buffer)) {
            ++btstack->hal->read_buffer_tail;
        } else {
            btstack->hal->read_buffer_tail = 0;
        }
        --btstack->hal->num_bytes_to_read;
    }
    TUPort serverPort(btstack->hal->server_port);
    SendForInterrupt(serverPort, btstack->hal->int_message, 0, NULL, 0, M_DATA);
}

void RxCSpecialIntHandler(void* context)
{
    btstack_state_t *btstack = static_cast<btstack_state_t*>(context);
    einstein_here(90, __func__, __LINE__);
}

void ExtStsIntHandler(void* context)
{
    btstack_state_t *btstack = static_cast<btstack_state_t*>(context);
    einstein_here(90, __func__, __LINE__);
}

void *hal_uart_newton_init(btstack_state_t *btstack)
{
    PSerialChipRegistry *registry = GetSerialChipRegistry();
    SerialChipID id = registry->FindByLocation('hsr1');
    TSerialChip *chip = registry->GetChipPtr(id);
    if (chip == NULL) {
        chip = TSerialChip::New("TSerialChipEinstein");
        if (chip != NULL) {
            THMOSerialEinsteinHardware option;
            SCCChannelInts handlers;
            option.fLocationID = 'hsr1';
            option.fType = 5;
            registry->Register(chip, 'hsr1');
            chip->ProcessOption(&option);

            handlers.TxBEmptyIntHandler = ::TxBEmptyIntHandler;
            handlers.ExtStsIntHandler = ::ExtStsIntHandler;
            handlers.RxCAvailIntHandler = ::RxCAvailIntHandler;
            handlers.RxCSpecialIntHandler = ::RxCSpecialIntHandler;
            chip->InstallChipHandler(btstack, &handlers);
            chip->SetInterruptEnable(true);
        }
    }
    einstein_log(90, __func__, __LINE__, "%08x", (int) chip);
    return chip;
}

int hal_uart_newton_set_baud(btstack_state_t *btstack, uint32_t baud){
    int result = 0;
    return result;
}

void hal_uart_newton_shutdown(void) {
}

void hal_uart_newton_send_block(btstack_state_t *btstack, const uint8_t * data, uint16_t len){
    TSerialChip *chip = static_cast<TSerialChip*>(btstack->uart->serial_chip);
    for (size_t i = 0; i < len; i++) {
        einstein_log(34, __func__, __LINE__, "%d", *data);
        chip->PutByte(*data++);
    }
    btstack->uart->block_sent(btstack);
}

void hal_uart_newton_receive_block(btstack_state_t *btstack, uint8_t *data, uint16_t len){
    einstein_log(90, __func__, __LINE__, "%d", len);
    btstack->hal->bytes_to_read = data;
    btstack->hal->num_bytes_to_read = len;
    if (btstack->hal->num_bytes_to_read == 0) {
        return;
    }
    while (btstack->hal->num_bytes_to_read > 0
            && btstack->hal->read_buffer_tail != btstack->hal->read_buffer_head) {
        *btstack->hal->bytes_to_read = btstack->hal->read_buffer[btstack->hal->read_buffer_tail];
        einstein_log(35, __func__, __LINE__, "%d", *btstack->hal->bytes_to_read);
        ++btstack->hal->bytes_to_read;
        if (btstack->hal->read_buffer_tail < sizeof(btstack->hal->read_buffer)) {
            ++btstack->hal->read_buffer_tail;
        } else {
            btstack->hal->read_buffer_tail = 0;
        }
        --btstack->hal->num_bytes_to_read;
    }
    if (btstack->hal->num_bytes_to_read == 0 && btstack->uart->block_received) {
        einstein_here(90, __func__, __LINE__);
        btstack->uart->block_received(btstack);
        btstack_run_loop_embedded_execute_once(btstack);
    }
}
