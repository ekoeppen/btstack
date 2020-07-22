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

#define BTSTACK_FILE__ "btstack_run_loop_newton.c"

/*
 *  btstack_run_loop_embedded.c
 *
 *  For this run loop, we assume that there's no global way to wait for a list
 *  of data sources to get ready. Instead, each data source has to queried
 *  individually. Calling ds->isReady() before calling ds->process() doesn't
 *  make sense, so we just poll each data source round robin.
 *
 *  To support an idle state, where an MCU could go to sleep, the process function
 *  has to return if it has to called again as soon as possible
 *
 *  After calling process() on every data source and evaluating the pending timers,
 *  the idle hook gets called if no data source did indicate that it needs to be
 *  called right away.
 *
 */


#include "btstack_run_loop.h"
#include "btstack_run_loop_newton.h"
#include "btstack_linked_list.h"
#include "btstack_util.h"

#include "btstack_debug.h"
#include "log.h"

#include <stddef.h> // NULL

#include "hal_timer_newton.h"

/**
 * Add data_source to run_loop
 */
static void btstack_run_loop_embedded_add_data_source(btstack_state_t *btstack, btstack_data_source_t *ds){
    btstack_linked_list_add(&btstack->run_loop->data_sources, (btstack_linked_item_t *) ds);
}

/**
 * Remove data_source from run loop
 */
static bool btstack_run_loop_embedded_remove_data_source(btstack_state_t *btstack, btstack_data_source_t *ds){
    return btstack_linked_list_remove(&btstack->run_loop->data_sources, (btstack_linked_item_t *) ds);
}

// set timer
static void btstack_run_loop_embedded_set_timer(btstack_state_t *btstack, btstack_timer_source_t *ts, uint32_t timeout_in_ms){
    ts->timeout = timeout_in_ms + 1;
}

/**
 * Add timer to run_loop (keep list sorted)
 */
static void btstack_run_loop_embedded_add_timer(btstack_state_t *btstack, btstack_timer_source_t *ts){
    btstack_linked_item_t *it;
    for (it = (btstack_linked_item_t *) &btstack->run_loop->timers; it->next ; it = it->next){
        // don't add timer that's already in there
        btstack_timer_source_t * next = (btstack_timer_source_t *) it->next;
        if (next == ts){
            log_error( "btstack_run_loop_timer_add error: timer to add already in list!");
            return;
        }
        // exit if new timeout before list timeout
        int32_t delta = btstack_time_delta(ts->timeout, next->timeout);
        if (delta < 0) break;
    }

    ts->item.next = it->next;
    ts->context = btstack;
    it->next = (btstack_linked_item_t *) ts;
    hal_timer_newton_set_timer(btstack, ts->timeout);
    einstein_log(34, __func__, __LINE__, "%d", ts->timeout);
}

/**
 * Remove timer from run loop
 */
static bool btstack_run_loop_embedded_remove_timer(btstack_state_t *btstack, btstack_timer_source_t *ts){
    return btstack_linked_list_remove(&btstack->run_loop->timers, (btstack_linked_item_t *) ts);
}

static void btstack_run_loop_embedded_dump_timer(btstack_state_t *btstack){
#ifdef ENABLE_LOG_INFO
    btstack_linked_item_t *it;
    int i = 0;
    for (it = (btstack_linked_item_t *) btstack->run_loop->timers; it ; it = it->next){
        btstack_timer_source_t *ts = (btstack_timer_source_t*) it;
        log_info("timer %u, timeout %u\n", i, (unsigned int) ts->timeout);
    }
#endif
}

static void btstack_run_loop_embedded_enable_data_source_callbacks(btstack_data_source_t * ds, uint16_t callback_types){
    ds->flags |= callback_types;
}

static void btstack_run_loop_embedded_disable_data_source_callbacks(btstack_data_source_t * ds, uint16_t callback_types){
    ds->flags &= ~callback_types;
}

/**
 * Execute run_loop once
 */
void btstack_run_loop_embedded_execute_once(btstack_state_t *btstack) {
    btstack_data_source_t *ds;

    // process data sources
    btstack_data_source_t *next;
    for (ds = (btstack_data_source_t *) btstack->run_loop->data_sources; ds != NULL ; ds = next){
        next = (btstack_data_source_t *) ds->item.next; // cache pointer to next data_source to allow data source to remove itself
        if (ds->flags & DATA_SOURCE_CALLBACK_POLL){
            ds->process(btstack, ds, DATA_SOURCE_CALLBACK_POLL);
        }
    }

    uint32_t now = hal_time_ms();

    // process timers
    while (btstack->run_loop->timers) {
        btstack_timer_source_t * ts = (btstack_timer_source_t *) btstack->run_loop->timers;
        int32_t delta = btstack_time_delta(ts->timeout, now);
        if (delta > 0) break;

        btstack_run_loop_embedded_remove_timer(btstack, ts);
        ts->process(btstack, ts);
    }
}

/**
 * Execute run_loop
 */
static void btstack_run_loop_embedded_execute(btstack_state_t *btstack) {
    while (!btstack->run_loop->exit) {
        btstack_run_loop_embedded_execute_once(btstack);
    }
}

static uint32_t btstack_run_loop_embedded_get_time_ms(btstack_state_t *btstack){
    return hal_time_ms();
}


/**
 * trigger run loop iteration
 */
void btstack_run_loop_embedded_trigger(btstack_state_t *btstack){
    btstack->run_loop->trigger_event_received = 1;
}

static void btstack_run_loop_embedded_init(btstack_state_t *btstack){
    btstack->run_loop->data_sources = NULL;
    btstack->run_loop->timers = NULL;
}

/**
 * Provide btstack_run_loop_embedded instance
 */

const btstack_run_loop_t btstack_run_loop_embedded = {
    &btstack_run_loop_embedded_init,
    &btstack_run_loop_embedded_add_data_source,
    &btstack_run_loop_embedded_remove_data_source,
    &btstack_run_loop_embedded_enable_data_source_callbacks,
    &btstack_run_loop_embedded_disable_data_source_callbacks,
    &btstack_run_loop_embedded_set_timer,
    &btstack_run_loop_embedded_add_timer,
    &btstack_run_loop_embedded_remove_timer,
    &btstack_run_loop_embedded_execute,
    &btstack_run_loop_embedded_dump_timer,
    &btstack_run_loop_embedded_get_time_ms,
};

const btstack_run_loop_t * btstack_run_loop_embedded_get_instance(void){
    return &btstack_run_loop_embedded;
}

