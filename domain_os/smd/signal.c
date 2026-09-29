/*
 * smd/signal.c - SMD_$SIGNAL implementation
 *
 * Sends a signal (request) to the display manager via the request queue.
 *
 * Original address: 0x00E6F1DA
 *
 * The request queue is a circular buffer with 40 entries (indices 1-40).
 * Each entry can hold up to 16 parameters. When the queue is full, callers
 * block waiting for the request event count.
 *
 * Queue structure:
 *   - Base at SMD_GLOBALS + 0x17D0
 *   - Each entry is 0x24 (36) bytes
 *   - Entry format: [requester_asid:2, param_count:2, params[16]:32]
 *   - Tail at SMD_GLOBALS + 0x17F0 (read index)
 *   - Head at SMD_GLOBALS + 0x17F2 (write index)
 */

#include "smd/smd_internal.h"
#include "ml/ml.h"

/* Status code for invalid buffer size */

/* Request queue event counts SMD_$WIRED_DATA.ec_1 / SMD_$WIRED_DATA.ec_2 are
 * declared in smd/smd_internal.h */

/*
 * SMD_$SIGNAL - Send signal to display manager
 *
 * Queues a request for the display manager to process. The request includes
 * the calling process's ASID and a variable number of parameters.
 *
 * Parameters:
 *   unit_ptr       - Pointer to display unit number
 *   params         - Pointer to parameter array (16-bit words)
 *   param_count    - Pointer to parameter count (1-16)
 *   status_ret     - Status return pointer
 *
 * Returns:
 *   status_$ok on success
 *   status_$display_invalid_unit_number if unit is invalid
 *   status_$display_invalid_buffer_size if param_count is 0 or > 16
 *
 * Note: This function blocks if the request queue is full.
 */
void SMD_$SIGNAL(uint16_t *unit_ptr, uint16_t *params, uint16_t *param_count,
                 status_$t *status_ret)
{
    int8_t valid;
    int32_t ec_value;
    int16_t queue_head, queue_tail;
    int16_t count;
    int16_t i;
    smd_request_entry_t *entry;

    /* Validate display unit */
    valid = smd_$validate_unit(*unit_ptr);
    if (valid >= 0) {
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    /* Validate parameter count (must be 1-16) */
    if (*param_count == 0 || *param_count > 16) {
        *status_ret = status_$display_invalid_buffer_size;
        return;
    }

    /*
     * Wait for space in the request queue.
     * 00e6f232 lea (A3),A4 / move.l (A4),D2 : re-read the eventcount value at
     * the top of every attempt, before taking the lock.
     */
    for (;;) {
        ec_value = SMD_$WIRED_DATA.ec_1.value;
        ML_$LOCK(SMD_REQUEST_LOCK);

        queue_head = SMD_GLOBALS.request_queue_head;
        queue_tail = SMD_GLOBALS.request_queue_tail;

        /* 00e6f24c cmp.w D1w,D0w / beq -> have space */
        if (queue_head == queue_tail) {
            break;
        }

        /* 00e6f250 cmp.w D1w,D0w / ble -> 0x00e6f262 */
        if (queue_head > queue_tail) {
            /* 00e6f254 ext.l/ext.l/sub.l -> D0 = head - tail
             * 00e6f25a moveq #0x27,D1 / sub.l D0,D1 / tst.l D1 / bgt */
            if ((int32_t)(SMD_REQUEST_QUEUE_MAX - 1) -
                    ((int32_t)queue_head - (int32_t)queue_tail) > 0) {
                break;
            }
        }
        /*
         * 00e6f262: reached both when head <= tail and when the test above
         * failed - the original falls through, it does not `else`.
         */
        if ((int32_t)queue_tail - (int32_t)queue_head - 1 > 0) {
            break;
        }

        /*
         * Queue full: drop the lock and block on SMD_$WIRED_DATA.ec_1.
         * 00e6f284-00e6f29a: EC_$WAIT with two 3-element arrays by value,
         * only slot 0 in use.
         */
        ML_$UNLOCK(SMD_REQUEST_LOCK);
        (void)EC_$WAIT(
            (ec_$wait_ecs_t){ { &SMD_$WIRED_DATA.ec_1, NULL, NULL } },
            (ec_$wait_vals_t){ { ec_value + 1, 0, 0 } });
    }

    /*
     * 00e6f2a0 move.w (0x17f2,A5),D1w / lsl.w #2 / lsl.w #3 / add
     *          lea (0,A5,D1w*1),A0 ... (0x17d0,A0)
     * i.e. the entry lives at SMD_GLOBALS + 0x17D0 + head*36: the Pascal
     * [1..40] table declared at its 0x17D0 bias slot, indexed with head.
     */
    entry = &SMD_GLOBALS.request_queue[SMD_GLOBALS.request_queue_head];

    /* 00e6f2b0 move.w (0x00e20608).l,(0x17d0,A0) */
    entry->request_type = PROC1_$CURRENT;

    /* 00e6f2b8 movea.l D3,A1 / move.w (A1),(0x17d2,A0) */
    entry->param_count = *param_count;

    /*
     * 00e6f2be move.w (A1),D0w / addq.w #1,D0w / cmpi.w #2,D0w / bcs -> skip
     * With param_count validated to 1..16 above, (count + 1) is never below 2,
     * so the copy always runs; the loop is `dbf` on count-1, i.e. `count`
     * words from the caller's array into entry->params[0..count-1].
     */
    count = (int16_t)*param_count;
    if ((uint16_t)(count + 1) >= 2) {
        for (i = 0; i < count; i++) {
            entry->params[i] = params[i];
        }
    }

    /* 00e6f2ea cmpi.w #0x28,(0x17f2,A5) / blt / move.w #1 / addq.w #1 */
    if (SMD_GLOBALS.request_queue_head >= SMD_REQUEST_QUEUE_MAX) {
        SMD_GLOBALS.request_queue_head = 1;
    } else {
        SMD_GLOBALS.request_queue_head++;
    }

    /* 00e6f2fe */
    ML_$UNLOCK(SMD_REQUEST_LOCK);

    /* 00e6f30c pea (0xe2e408).l / jsr EC_$ADVANCE */
    EC_$ADVANCE(&SMD_$WIRED_DATA.ec_2);

    *status_ret = status_$ok;
}
