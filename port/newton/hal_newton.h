#pragma once

#include <stdint.h>
#include "btstack_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct btstack_hal_state {
    uint8_t* bytes_to_read;
    uint16_t num_bytes_to_read;
    uint8_t read_buffer[2048];
    uint16_t read_buffer_head;
    uint16_t read_buffer_tail;
    uint32_t server_port;
    uint32_t int_message;
} btstack_hal_state_t;

void btstack_hal_init(btstack_state_t *btstack);

#ifdef __cplusplus
}
#endif
