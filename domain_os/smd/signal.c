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
#define status_$display_invalid_buffer_size     0x0013000C

/* Request queue event counts */
extern ec_$eventcount_t SMD_REQUEST_EC_WAIT;  /* At 0x00E2E3FC - wait for space */
extern ec_$eventcount_t SMD_REQUEST_EC_SIGNAL; /* At 0x00E2E408 - signal new request */

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
    int16_t entry_offset;
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

    /* Wait for space in the request queue */
    for (;;) {
        ec_value = SMD_REQUEST_EC_WAIT.value;
        ML_$LOCK(SMD_REQUEST_LOCK);

        queue_head = SMD_GLOBALS.request_queue_head;
        queue_tail = SMD_GLOBALS.request_queue_tail;

        /* Check if queue has space */
        if (queue_head == queue_tail) {
            /* Queue is empty - has space */
            break;
        }

        if (queue_head > queue_tail) {
            /* Head is ahead of tail: space = max - (head - tail) */
            if (-(queue_head - queue_tail) != -(SMD_REQUEST_QUEUE_SIZE - 1) &&
                -(queue_head - queue_tail) + (SMD_REQUEST_QUEUE_SIZE - 1) >= 0) {
                /* Queue has space */
                break;
            }
        } else {
            /* Tail is ahead of head: space = tail - head - 1 */
            if ((queue_tail - queue_head) - 1 > 0) {
                /* Queue has space */
                break;
            }
        }

        /* Queue is full - wait for space */
        ML_$UNLOCK(SMD_REQUEST_LOCK);
        EC_$WAIT(&SMD_REQUEST_EC_WAIT, ec_value + 1);
    }

    /* Calculate entry offset: head * 0x24 */
    entry_offset = SMD_GLOBALS.request_queue_head * sizeof(smd_request_entry_t);
    entry = (smd_request_entry_t *)((uint8_t *)SMD_GLOBALS.request_queue + entry_offset);

    /* Store requester's process ID (ASID from PROC1) */
    entry->request_type = PROC1_$CURRENT;

    /* Store parameter count */
    entry->param_count = *param_count;

    /* Copy parameters */
    count = *param_count;
    if (count >= 2) {
        for (i = 0; i < count; i++) {
            entry->params[i] = params[i];
        }
    }

    /* Advance queue head (circular, 1-based indices 1-40) */
    if (SMD_GLOBALS.request_queue_head >= SMD_REQUEST_QUEUE_MAX) {
        SMD_GLOBALS.request_queue_head = 1;
    } else {
        SMD_GLOBALS.request_queue_head++;
    }

    ML_$UNLOCK(SMD_REQUEST_LOCK);

    /* Signal that a new request is available */
    EC_$ADVANCE(&SMD_REQUEST_EC_SIGNAL);

    *status_ret = status_$ok;
}
