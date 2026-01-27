/*
 * KBD_$RCV - Keyboard receive handler
 *
 * Processes incoming keyboard data through a state machine.
 * Handles normal keys, touchpad data, and special keys (crash key).
 *
 * Parameters:
 *   state - Pointer to keyboard state structure
 *   key   - Received key byte
 *
 * Original address: 0x00e1ccc0
 */

#include "kbd/kbd_internal.h"

/* Touchpad event buffer constants */
#define TPAD_EVENT_SIZE  sizeof(suma_sample_t)
#define TPAD_MAX_EVENTS  SUMA_TPAD_BUFFER_SIZE

void KBD_$RCV(kbd_state_t *state, uint8_t key)
{
    void *state_entry;
    uint8_t state_byte;
    uint16_t new_state;
    uint16_t write_idx;
    clock_t current_time;
    status_$t status[2];
    kbd_state_t *callback_param;
    uint8_t local_key;
    int16_t local_mode[2];

    /* Look up state machine entry */
    state_entry = kbd_$state_lookup(state->state, key);
    state_byte = *((uint8_t *)state_entry + 1);

    /* Process based on high nibble of state byte */
    switch (state_byte >> 4) {
    case 2:
        /* Check for crash key sequence */
        if (MMU_$NORMAL_MODE() >= 0) {
            CRASH_SYSTEM(&Term_Manual_Stop_err);
            KBD_$CRASH_INIT();
            /* Set state to 0xF */
            *((uint16_t *)state_entry) |= 0x0F;
        }
        /* Fall through to normal key processing */
        /* FALLTHROUGH */
    case 1:
    case 10:
    case 11:
    case 12:
        kbd_$process_key(key, state);
        break;

    case 3:
        /* Start of touchpad sequence - store X coordinate */
        state->tpad_x = key;
        state->tpad_ptr = &state->tpad_y;
        break;

    case 4:
        /* Touchpad Y byte 1 */
        *state->tpad_ptr = key;
        break;

    case 5:
        /* Touchpad Y byte 2 */
        *(state->tpad_ptr + 1) = key;
        break;

    case 6:
        /* Touchpad sequence complete */
        *(state->tpad_ptr + 2) = key;

        /* Only process if no handler installed */
        if (state->handler == NULL) {
            /* Calculate next write index */
            write_idx = TERM_$TPAD_BUFFER.head + 1;
            if (write_idx == TPAD_MAX_EVENTS) {
                write_idx = 0;
            }

            /* Get current time */
            TIME_$CLOCK(&current_time);

            /* Check if buffer has space */
            if ((int16_t)TERM_$TPAD_BUFFER.tail != (int)write_idx) {
                /* Store timing data */
                state->clock_high = current_time.high;
                state->clock_low = (uint16_t)current_time.low;

                /* Calculate delta time */
                state->delta_time = *(uint32_t *)((uint8_t *)state + 0x22) - state->last_time;

                /* Copy event data to buffer */
                {
                    int16_t idx = (int16_t)TERM_$TPAD_BUFFER.head;
                    uint8_t *src = (uint8_t *)&state->delta_time;
                    uint8_t *dst = (uint8_t *)&TERM_$TPAD_BUFFER.samples[idx];
                    int i;
                    for (i = 0; i < TPAD_EVENT_SIZE; i++) {
                        dst[i] = src[i];
                    }
                }

                /* Update write index */
                TERM_$TPAD_BUFFER.head = write_idx;

                /* Queue callback */
                callback_param = (kbd_state_t *)((uint8_t *)state + 0x30);
                DXM_$ADD_CALLBACK(&DXM_$UNWIRED_Q, &PTR_TERM_$ENQUEUE_TPAD_00e1ce90,
                                  &callback_param, 0x04FFA6, status);
            }

            /* Save current time as last time */
            state->last_time = (current_time.high << 16) | current_time.low;
        }
        break;

    default:
        /* Unknown state - ignore */
        break;
    }

    /* Update state based on low nibble */
    new_state = state_byte & 0x0F;
    if (new_state == 0x0F) {
        /* Look up next state from table */
        state->state = DAT_00e2ddec[state->kbd_type_idx];
    } else {
        state->state = new_state;
    }

    /* If handler installed, process buffered keys */
    if (state->handler != NULL) {
        while (kbd_$fetch_key(state, &local_key, local_mode) < 0) {
            if (local_mode[0] == 0) {
                local_key = kbd_$translate_key(local_key);
                ((void (*)(uint32_t))state->handler)(state->user_data);
            }
        }
    }
}
