/*
 * SUMA_$RCV - Receive and process a tablet data byte
 *
 * Called when a byte is received from the tablet device. Implements
 * a 5-state state machine to assemble 5-byte tablet packets.
 *
 * Tablet packet format (5 bytes):
 *   Byte 0 (sync): bit 6 = sync flag, bits 5-2 = device ID << 2
 *   Byte 1: X high (6 bits, masked with 0x3f)
 *   Byte 2: X low (6 bits, masked with 0x3f)
 *   Byte 3: Y high (6 bits, masked with 0x3f)
 *   Byte 4: Y low (6 bits, masked with 0x3f)
 *
 * From: 0x00e1ad18
 *
 * The function uses a state machine with states 0-4:
 *   State 0: Wait for sync byte (bit 6 set)
 *            Copy current sample to previous
 *            Extract ID from bits 5-2, store shifted left by 2
 *   State 1: Receive X high coordinate (6 bits)
 *   State 2: Receive X low coordinate (6 bits)
 *   State 3: Receive Y high coordinate (6 bits)
 *   State 4: Receive Y low coordinate (6 bits)
 *            Then process complete packet:
 *            - Compare ID with previous sample
 *            - Compare X/Y against threshold
 *            - If significantly different, enqueue event
 *            - Adjust threshold based on movement
 */

#include "suma/suma_internal.h"

/* Macro to compute absolute difference */
#define ABS_DIFF(a, b) ((int32_t)(a) - (int32_t)(b) >= 0 ? \
                        (uint32_t)((int32_t)(a) - (int32_t)(b)) : \
                        (uint32_t)((int32_t)(b) - (int32_t)(a)))

void SUMA_$RCV(uint32_t param_1, uint8_t data_byte)
{
    uint16_t state;
    uint8_t id_nibble;
    int is_duplicate;
    uint16_t next_head;
    clock_t current_time;
    void *callback_data[3];
    status_$t status;
    uint32_t x_diff, y_diff;
    uint8_t threshold_byte;
    int16_t sample_index;

    (void)param_1;  /* Unused parameter */

    state = SUMA_$STATE.rcv_state;

    /* State machine for 5-byte packet assembly */
    switch (state) {
    case 0:
        /* State 0: Wait for sync byte (bit 6 set) */
        if ((data_byte & 0x40) == 0) {
            /* Not a sync byte - ignore */
            return;
        }

        /* Copy current sample to previous sample (16 bytes at offset 0x14 -> 0x04) */
        SUMA_$STATE.prev_delta = SUMA_$STATE.cur_delta;
        SUMA_$STATE.prev_timestamp_high = SUMA_$STATE.cur_timestamp_high;
        SUMA_$STATE.prev_timestamp_low = SUMA_$STATE.cur_timestamp_low;
        SUMA_$STATE.prev_id_flags = SUMA_$STATE.cur_id_flags;
        SUMA_$STATE.prev_reserved = SUMA_$STATE.cur_reserved;
        SUMA_$STATE.prev_x_high = SUMA_$STATE.cur_x_high;
        SUMA_$STATE.prev_x_low = SUMA_$STATE.cur_x_low;
        SUMA_$STATE.prev_y_high = SUMA_$STATE.cur_y_high;
        SUMA_$STATE.prev_y_low = SUMA_$STATE.cur_y_low;

        /* Extract device ID from bits 5-2 and store shifted left by 2 */
        /* Original: andi.b #0xf,D0b; lsl.b #0x2,D1b; or.b D1b,(0x1f,A5) */
        id_nibble = (data_byte >> 2) & 0x0f;
        SUMA_$STATE.cur_id_flags = (SUMA_$STATE.cur_id_flags & 0xc3) | (id_nibble << 2);
        break;

    case 1:
        /* State 1: X high coordinate (6 bits) */
        SUMA_$STATE.cur_x_high = data_byte & 0x3f;
        break;

    case 2:
        /* State 2: X low coordinate (6 bits) */
        SUMA_$STATE.cur_x_low = data_byte & 0x3f;
        break;

    case 3:
        /* State 3: Y high coordinate (6 bits) */
        SUMA_$STATE.cur_y_high = data_byte & 0x3f;
        break;

    case 4:
        /* State 4: Y low coordinate (6 bits), then process packet */
        SUMA_$STATE.cur_y_low = data_byte & 0x3f;
        SUMA_$STATE.rcv_state = 0;  /* Reset state machine */

        /*
         * Compare current sample with previous to detect movement.
         * Only compare if device IDs match (bits 5-2).
         */
        is_duplicate = 0;
        if (((SUMA_$STATE.prev_id_flags & 0x3c) >> 2) ==
            ((SUMA_$STATE.cur_id_flags & 0x3c) >> 2)) {
            /*
             * Same device ID - check if movement exceeds threshold.
             * Compare X high and Y high bytes against threshold.
             */
            threshold_byte = (uint8_t)SUMA_$STATE.threshold;

            /* Check X high difference */
            x_diff = ABS_DIFF(SUMA_$STATE.prev_x_high, SUMA_$STATE.cur_x_high);
            if (x_diff > threshold_byte) {
                goto not_duplicate;
            }

            /* Check X low must match exactly */
            if (SUMA_$STATE.prev_x_low != SUMA_$STATE.cur_x_low) {
                goto not_duplicate;
            }

            /* Check Y high difference */
            y_diff = ABS_DIFF(SUMA_$STATE.prev_y_high, SUMA_$STATE.cur_y_high);
            if (y_diff > threshold_byte) {
                goto not_duplicate;
            }

            /* Check Y low must match exactly */
            if (SUMA_$STATE.prev_y_low != SUMA_$STATE.cur_y_low) {
                goto not_duplicate;
            }

            /* All checks passed - this is a duplicate/no significant movement */
            /* Fall through with is_duplicate = 0 (actually we don't flag it) */
        } else {
        not_duplicate:
            is_duplicate = -1;  /* Flag as new/significant movement */
        }

        /* Calculate next head position in circular buffer */
        next_head = TERM_$TPAD_BUFFER.head + 1;
        if (next_head == SUMA_TPAD_BUFFER_SIZE) {
            next_head = 0;
        }

        /* Get current time */
        TIME_$CLOCK(&current_time);

        /*
         * If this is a new position (not duplicate) and buffer not full,
         * enqueue the event.
         */
        if (is_duplicate < 0 && next_head != TERM_$TPAD_BUFFER.tail) {
            /* Update current timestamp */
            SUMA_$STATE.cur_timestamp_high = current_time.high;
            SUMA_$STATE.cur_timestamp_low = current_time.low;

            /* Calculate delta time from last sample */
            SUMA_$STATE.cur_delta = ((current_time.high & 0xffff) << 16 | current_time.low)
                                    - SUMA_$STATE.last_time;

            /* Copy current sample to buffer at head position */
            sample_index = TERM_$TPAD_BUFFER.head << 4;  /* * 16 bytes per sample */
            TERM_$TPAD_BUFFER.samples[TERM_$TPAD_BUFFER.head].delta_time = SUMA_$STATE.cur_delta;
            TERM_$TPAD_BUFFER.samples[TERM_$TPAD_BUFFER.head].timestamp_high = SUMA_$STATE.cur_timestamp_high;
            TERM_$TPAD_BUFFER.samples[TERM_$TPAD_BUFFER.head].timestamp_low = SUMA_$STATE.cur_timestamp_low;
            TERM_$TPAD_BUFFER.samples[TERM_$TPAD_BUFFER.head].id_flags = SUMA_$STATE.cur_id_flags;
            TERM_$TPAD_BUFFER.samples[TERM_$TPAD_BUFFER.head].reserved_0b = SUMA_$STATE.cur_reserved;
            TERM_$TPAD_BUFFER.samples[TERM_$TPAD_BUFFER.head].x_high = SUMA_$STATE.cur_x_high;
            TERM_$TPAD_BUFFER.samples[TERM_$TPAD_BUFFER.head].x_low = SUMA_$STATE.cur_x_low;
            TERM_$TPAD_BUFFER.samples[TERM_$TPAD_BUFFER.head].y_high = SUMA_$STATE.cur_y_high;
            TERM_$TPAD_BUFFER.samples[TERM_$TPAD_BUFFER.head].y_low = SUMA_$STATE.cur_y_low;

            /* Update head pointer */
            TERM_$TPAD_BUFFER.head = next_head;

            /* Queue callback to process the event */
            callback_data[0] = &SUMA_$STATE.tpad_buffer;
            DXM_$ADD_CALLBACK(&DXM_$UNWIRED_Q,
                              (void **)&PTR_TERM_$ENQUEUE_TPAD_00e1aecc,
                              (void **)callback_data,
                              (4 << 16) | (0xff << 8) | 0x3a,  /* flags: size=4, check_dup=0xff, type=0x3a */
                              &status);

            /* Reset threshold to initial value */
            SUMA_$STATE.threshold = SUMA_INITIAL_THRESHOLD;
        } else {
            /* Duplicate or buffer full - increase threshold */
            SUMA_$STATE.threshold += SUMA_THRESHOLD_INCREMENT;
            if (SUMA_$STATE.threshold > SUMA_MAX_THRESHOLD) {
                SUMA_$STATE.threshold = SUMA_MAX_THRESHOLD;
            }
        }

        /* Update last_time for next delta calculation */
        SUMA_$STATE.last_time = (current_time.high & 0xffff) << 16 | current_time.low;
        return;

    default:
        /* Invalid state - should not happen */
        return;
    }

    /* Advance state machine for states 0-3 */
    SUMA_$STATE.rcv_state = state + 1;
}
