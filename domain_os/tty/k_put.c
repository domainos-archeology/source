/*
 * TTY_$K_PUT - Write characters to TTY (kernel level)
 *
 * Writes characters to the TTY output buffer. Handles both blocking
 * and non-blocking writes, with flow control support.
 *
 * Parameters:
 *   line_ptr   - Pointer to terminal line number
 *   options    - Pointer to write options (2-byte flags)
 *                  byte 1 bit 0: non-blocking (return error if would block)
 *                  byte 1 bit 1: check space only (don't actually write)
 *   buffer     - Buffer containing characters to write
 *   count      - Pointer to count of characters (updated on return)
 *   status_ret - Pointer to receive status code
 *
 * Original address: 0x00e67bb2
 * Size: 490 bytes
 */

#include "tty/tty_internal.h"
#include "proc1/proc1.h"
#include "fim/fim.h"
#include "ec/ec.h"

/* Status codes */
/* status_$tty_would_block comes from base/base.h */

/* TTY_$I_PUT_OUTPUT declared in tty_internal.h */

void TTY_$K_PUT(short *line_ptr, void *options, void *buffer,
                ushort *count, status_$t *status_ret)
{
    tty_desc_t *tty;
    uint16_t chars_written = 0;
    int16_t written;
    int16_t avail_space;
    ec_$eventcount_t *output_ec;
    ec_$eventcount_t *quit_ec;
    int32_t output_wait_val;
    int32_t quit_wait_val;
    uint16_t wait_result;

    /* Validate count */
    if ((int16_t)*count < 0) {
        *status_ret = status_$tty_invalid_output_buffer_length;
        return;
    }

    /* Get TTY descriptor for this line */
    tty = TTY_$I_GET_DESC(*line_ptr, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* Lock the TTY */
    TTY_$I_LOCK(tty);

    /* Check if this is a "check space only" request.
     * Original: btst.b #1,(0x1,A0) - bit 1 of the low byte of the 2-byte
     * big-endian option word (likewise bit 0 below). */
    if ((*(uint16_t *)options & 0x0002) != 0) {
        /* Calculate available output buffer space */
        /* Space = head - tail - 1 (circular buffer) */
        avail_space = tty->output_head - tty->output_read - 1;
        if (avail_space < 0) {
            avail_space += 0x100;
        }
        avail_space -= 0x40;  /* Reserve some space */

        /* Unlock TTY before returning */
        TTY_$I_UNLOCK(tty);

        /* Check if request is non-blocking */
        if ((*(uint16_t *)options & 0x0001) == 0) {
            *status_ret = status_$tty_invalid_option;
            return;
        }

        /* Check if enough space available */
        if (avail_space < (int16_t)*count) {
            *status_ret = status_$tty_would_block;
            return;
        }

        *status_ret = status_$ok;
        return;
    }

    /* Write data in chunks */
    while (chars_written < *count) {
        /* Write up to 64 bytes at a time */
        written = TTY_$I_PUT_OUTPUT(tty, (char *)buffer + chars_written,
                               *count - chars_written, 0x40);
        chars_written += written;

        /* Check for pending signal */
        if (tty->pending_signal != 0) {
            if ((*(uint8_t *)((char *)tty + 0x0b) & TTY_ERR_CALLBACK) != 0) {
                /* Call error handler */
                *status_ret = ((status_$t (*)(short))tty->status_handler)(
                    (short)tty->line_id);
            } else if ((*(uint8_t *)((char *)tty + 0x0b) & TTY_ERR_OVERFLOW) != 0) {
                *status_ret = status_$tty_input_buffer_overrun;
            }
            tty->pending_signal = 0;
            if (*status_ret != status_$ok) {
                goto done;
            }
        }

        /* Check if done */
        if (chars_written == *count) {
            goto done;
        }

        /* Need to wait for buffer space - set up wait */
        output_ec = (ec_$eventcount_t *)(uintptr_t)tty->output_ec;
        output_wait_val = output_ec->value + 1;

        quit_ec = (ec_$eventcount_t *)&FIM_$QUIT_EC[PROC1_$AS_ID];
        quit_wait_val = FIM_$QUIT_VALUE[PROC1_$AS_ID] + 1;

        /* Check output state */
        if ((tty->state_flags & 0x07) == 0) {
            continue;  /* Output ready, try again */
        }

        /* Check if non-blocking */
        if ((*(uint16_t *)options & 0x0001) != 0) {
            *status_ret = status_$tty_would_block;
            goto done;
        }

        /* Wait for output buffer drain or quit signal */
        TTY_$I_UNLOCK(tty);

        {
            ec_$eventcount_t *ecs[2];
            int32_t vals[2];
            ecs[0] = output_ec;
            ecs[1] = quit_ec;
            vals[0] = output_wait_val;
            vals[1] = quit_wait_val;
            wait_result = EC_$WAITN(ecs, vals, 2);
        }

        TTY_$I_LOCK(tty);

        /* Check if quit signaled */
        if (wait_result == 2) {
            *status_ret = status_$tty_quit_while_waiting_for_input;
            /* Update quit value */
            FIM_$QUIT_VALUE[PROC1_$AS_ID] = FIM_$QUIT_EC[PROC1_$AS_ID].value;
            goto done;
        }
    }

done:
    *count = chars_written;
    TTY_$I_UNLOCK(tty);
}
