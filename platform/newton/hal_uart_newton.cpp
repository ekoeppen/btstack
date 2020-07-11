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

/**
 * @file  hal_bt.c
 ***************************************************************************/
#include <stdint.h>

#include "btstack_state.h"
#include "btstack_uart_block_newton.h"
#include "hal_uart_newton.h"
#include "log.h"
#include "stdio.h"

#include "SerialChipRegistry.h"

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

void *hal_uart_newton_init(btstack_state_t *bstack)
{
    PSerialChipRegistry *registry = GetSerialChipRegistry();
    SerialChipID id = registry->FindByLocation('hsr1');
    TSerialChip *chip = registry->GetChipPtr(id);
    if (chip == NULL) {
        chip = TSerialChip::New("TSerialChipEinstein");
        if (chip != NULL) {
            THMOSerialEinsteinHardware option;
            option.fLocationID = 'hsr1';
            option.fType = 5;
            NewtonErr r = registry->Register(chip, 'hsr1');
            chip->ProcessOption(&option);
        }
    }
    LH(__func__, __LINE__, (int) chip);
    return chip;
}

int hal_uart_newton_set_baud(btstack_state_t *btstack, uint32_t baud){
    int result = 0;
    LH(__func__, __LINE__, baud);
    return result;
}

void hal_uart_newton_shutdown(void) {
}

void hal_uart_newton_send_block(btstack_state_t *btstack, const uint8_t * data, uint16_t len){
    TSerialChip *chip = static_cast<TSerialChip*>(btstack->uart->serial_chip);
    for (size_t i = 0; i < len; i++) {
        chip->PutByte(*data++);
    }
    btstack->uart->block_sent(btstack);
    LH(__func__, __LINE__, len);
}

void hal_uart_newton_receive_block(btstack_state_t *btstack, uint8_t *data, uint16_t len){
    LH(__func__, __LINE__, len);
}
