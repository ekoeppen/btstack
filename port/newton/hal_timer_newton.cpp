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

/*
 *  hal_uart_newton.h
 *
 *  Hardware abstraction layer that provides
 *  - block wise IRQ-driven read/write
 *  - baud control
 *  - wake-up on CTS pulse (BTSTACK_UART_SLEEP_RTS_HIGH_WAKE_ON_CTS_PULSE)
 *
 * If HAVE_HAL_TIMER_NEWTON_SLEEP_MODES is defined, different sleeps modes can be provided and used
 *
 */

#include <stdint.h>
#include <UserTasks.h>
#include "btstack_state.h"
#include "hal_newton.h"
#include "hal_timer_newton.h"
#include "log.h"
#include "EventsCommands.h"

#if defined __cplusplus
extern "C" {
#endif

unsigned int hal_time_ms()
{
    return GetGlobalTime().ConvertTo(kMilliseconds);
}

void hal_timer_newton_set_timer(btstack_state_t *btstack, uint32_t milliseconds)
{
    TUPort serverPort(btstack->hal->server_port);
    TUAsyncMessage message(btstack->hal->timer_message, 0);
    TTime futureTime = GetGlobalTime() + TTime(milliseconds, kMilliseconds);
    serverPort.Send (&message, NULL, 0, kNoTimeout, &futureTime, M_TIMER);
}

#if defined __cplusplus
}
#endif
