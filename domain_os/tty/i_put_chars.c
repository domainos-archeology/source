/*
 * tty_$i_put_chars - Put characters into TTY output buffer
 *
 * Processes a string of characters for TTY output, handling special
 * characters with appropriate output transformations:
 *   - BS  (0x08): Backspace - output BS, decrement column
 *   - CR  (0x0D): Carriage return - may convert to LF based on flags
 *   - LF  (0x0A): Line feed - may prepend CR based on flags
 *   - TAB (0x09): Tab - may expand to spaces based on flags
 *   - VT  (0x0B): Vertical tab - output VT with optional delay
 *   - FF  (0x0C): Form feed - output FF with optional delay
 *   - 0xFE: Escape marker - doubled in output buffer
 *   - Normal chars (>= 0x20): Stored directly, increment column
 *
 * The output buffer is a circular buffer starting at &tty->output_head
 * (offset 0x3D2), with 1-based indexing (1..0x100) and 256 entries.
 *
 * After processing characters (or as many as fit), the transmit
 * callback at offset 0x2B4 is invoked to start actual I/O.
 *
 * Contains one Pascal nested subprocedure (buf_put_delay) that has
 * been flattened to a static C function with explicit parent-frame
 * parameters.
 *
 * Parameters:
 *   tty   - TTY descriptor
 *   buf   - Character buffer to output
 *   flags - Packed: high 16 bits = max chars to process,
 *           low 16 bits = buffer space already reserved
 *
 * Returns:
 *   Number of characters actually processed from buf
 *
 * Original address: 0x00e1b00a
 * Size: 906 bytes
 */

#include "tty/tty_internal.h"

/*
 * Output flag bits (from 32-bit word at tty descriptor offset 0x0C).
 * Assembly: move.l (0xC,A0),D1 then tests individual bits.
 */
#define TTY_OFLAG_CR_TO_LF    0x01  /* Bit 0: Convert CR to LF */
#define TTY_OFLAG_LF_TO_CRLF  0x02  /* Bit 1: Prepend CR before LF */
#define TTY_OFLAG_DISCARD      0x08  /* Bit 3: Discard CR when column == 0 */
#define TTY_OFLAG_EXPAND_TABS  0x10  /* Bit 4: Expand TABs to spaces */

/*
 * The field column at offset 0x58 in tty_desc_t is the display column
 * position. The assembly increments it for printable chars, decrements
 * for BS, resets for CR, and uses (column & 7) for TAB expansion.
 */
#define TTY_COLUMN(tty) ((tty)->column)

/*
 * Delay type indices in tty->delay[] array:
 *   delay[0] at offset 0x40: LF delay
 *   delay[1] at offset 0x42: CR delay
 *   delay[2] at offset 0x44: TAB delay
 *   delay[3] at offset 0x46: VT delay
 *   delay[4] at offset 0x48: FF delay
 */
#define TTY_DELAY_LF  0
#define TTY_DELAY_CR  1
#define TTY_DELAY_TAB 2
#define TTY_DELAY_VT  3
#define TTY_DELAY_FF  4

/*
 * Minimum delay value to trigger delay sequence insertion.
 * Delays <= 3 are treated as "no delay".
 */
#define TTY_DELAY_THRESHOLD 3

/*
 * Buffer fullness threshold for setting the output wait flag.
 * When used bytes >= 0xBF (191 of 256), TTY_STATUS_OUTPUT_WAIT is set.
 */
#define TTY_BUFFER_NEARLY_FULL 0xBF

/*
 * buf_put_delay - Insert delay escape sequence into output buffer
 *
 * Pascal nested procedure from tty_$i_put_chars. In the original M68K
 * code, this accesses the parent frame's tty pointer (A6+0x08 via
 * saved A6) and local_max (A6-0x14 via saved A6). In this C version
 * these are passed explicitly.
 *
 * Inserts a 4-byte delay sequence: 0xFE, 0x00, delay_high, delay_low.
 * Decrements *local_max by 4 to account for the extra buffer usage.
 *
 * Original address: 0x00e1afa2
 * Size: 104 bytes
 */
static void buf_put_delay(tty_desc_t *tty, uint16_t delay_val, int16_t *local_max)
{
    *local_max -= 4;
    tty_$i_buf_put(0xFE, &tty->output_head);
    tty_$i_buf_put(0x00, &tty->output_head);
    tty_$i_buf_put((uint8_t)(delay_val >> 8), &tty->output_head);
    tty_$i_buf_put((uint8_t)(delay_val & 0xFF), &tty->output_head);
}

uint16_t tty_$i_put_chars(tty_desc_t *tty, const uint8_t *buf, uint32_t flags)
{
    uint16_t count = (uint16_t)(flags >> 16);
    uint16_t avail_hint = (uint16_t)(flags & 0xFFFF);

    /* If output flush in progress (state_flags bit 5), pretend all consumed */
    if (tty->state_flags & TTY_STATUS_OUTPUT_FLUSH) {
        return count;
    }

    /* 0xE1B032: output stopped by XOFF (state_flags bit 2) - consume nothing */
    if (tty->state_flags & TTY_STATUS_OUTPUT_STOPPED) {
        return 0;
    }

    /*
     * Calculate free buffer space.
     * Free = (head - tail - 1) mod 256, then subtract reserved space.
     *
     * output_head (0x3D2) is the consumer read position.
     * output_read (0x3D4) is the producer write position (tail).
     */
    int16_t free_space = (int16_t)(tty->output_head - tty->output_read) - 1;
    if (free_space < 0) {
        free_space += TTY_BUFFER_SIZE;
    }
    free_space -= (int16_t)avail_hint;

    /*
     * Determine how many chars to process: min(count, free_space),
     * clamped to 0 when free_space is negative.
     * Uses signed comparison matching the assembly:
     *   clr.l D2; move.w D3w,D2w (zero-extend count)
     *   ext.l D4 (sign-extend free_space)
     *   cmp.l D4,D2
     */
    int16_t local_max;
    if ((int32_t)(uint32_t)count <= (int32_t)free_space) {
        local_max = (int16_t)count;
    } else if (free_space >= 0) {
        local_max = free_space;
    } else {
        local_max = 0;
    }

    uint16_t chars_processed = 0;

    /* Main character processing loop.
     * Assembly: initial bra.w to loop_check, then loop body at 0xe1b07a.
     * Condition: zero-extend chars_processed, sign-extend local_max,
     * signed compare.
     */
    while ((int32_t)(uint32_t)chars_processed < (int32_t)local_max) {
        uint8_t ch = *buf++;
        chars_processed++;

        uint32_t oflags;

        if (ch == 0x08) {
            /* BS (Backspace): output BS, decrement column if > 0 */
            tty_$i_buf_put(0x08, &tty->output_head);
            if (TTY_COLUMN(tty) != 0) {
                TTY_COLUMN(tty)--;
            }

        } else if (ch == 0x0D) {
            /* CR (Carriage Return) */
            oflags = tty->output_flags;

            /*
             * If discard mode (bit 3) and column == 0, skip output
             * but still reset column.
             */
            if ((oflags & TTY_OFLAG_DISCARD) != 0 &&
                TTY_COLUMN(tty) == 0) {
                /* Discard: just reset column below */
            } else {
                oflags = tty->output_flags;

                if (oflags & TTY_OFLAG_CR_TO_LF) {
                    /* Convert CR to LF */
                    local_max--;
                    tty_$i_buf_put(0x0A, &tty->output_head);
                    if (tty->delay[TTY_DELAY_LF] > TTY_DELAY_THRESHOLD) {
                        buf_put_delay(tty, tty->delay[TTY_DELAY_LF],
                                      &local_max);
                    }
                } else {
                    /* Normal CR */
                    tty_$i_buf_put(0x0D, &tty->output_head);
                    if (tty->delay[TTY_DELAY_CR] > TTY_DELAY_THRESHOLD) {
                        buf_put_delay(tty, tty->delay[TTY_DELAY_CR],
                                      &local_max);
                    }
                }
            }
            /* Reset column to 0 */
            TTY_COLUMN(tty) = 0;

        } else if (ch == 0x0A) {
            /* LF (Line Feed) */
            oflags = tty->output_flags;

            if (oflags & TTY_OFLAG_LF_TO_CRLF) {
                /* Prepend CR before LF */
                local_max--;
                tty_$i_buf_put(0x0D, &tty->output_head);
                if (tty->delay[TTY_DELAY_CR] > TTY_DELAY_THRESHOLD) {
                    buf_put_delay(tty, tty->delay[TTY_DELAY_CR],
                                  &local_max);
                }
                /* Reset column after CR */
                TTY_COLUMN(tty) = 0;
            }
            /* Output LF */
            tty_$i_buf_put(0x0A, &tty->output_head);
            if (tty->delay[TTY_DELAY_LF] > TTY_DELAY_THRESHOLD) {
                buf_put_delay(tty, tty->delay[TTY_DELAY_LF], &local_max);
            }

        } else if (ch == 0x09) {
            /* TAB */
            int16_t col_mod = TTY_COLUMN(tty) & 7;
            int16_t spaces = 8 - col_mod;

            oflags = tty->output_flags;

            if (oflags & TTY_OFLAG_EXPAND_TABS) {
                /* Expand TAB to spaces */
                int16_t extra = spaces - 1;
                local_max -= extra;

                if (spaces != 0) {
                    /*
                     * dbf loop: D5 starts at (spaces-1), body executes,
                     * D5--, loop while D5 != -1. Total: spaces iterations.
                     */
                    int16_t i = extra;
                    do {
                        tty_$i_buf_put(0x20, &tty->output_head);
                    } while (i-- != 0);
                }
            } else {
                /* Output literal TAB with optional delay */
                tty_$i_buf_put(0x09, &tty->output_head);
                if (tty->delay[TTY_DELAY_TAB] > TTY_DELAY_THRESHOLD) {
                    buf_put_delay(tty, tty->delay[TTY_DELAY_TAB],
                                  &local_max);
                }
            }
            /* Advance column by tab spaces */
            TTY_COLUMN(tty) += spaces;

        } else if (ch == 0x0B) {
            /* VT (Vertical Tab) */
            tty_$i_buf_put(0x0B, &tty->output_head);
            if (tty->delay[TTY_DELAY_VT] > TTY_DELAY_THRESHOLD) {
                buf_put_delay(tty, tty->delay[TTY_DELAY_VT], &local_max);
            }

        } else if (ch == 0x0C) {
            /* FF (Form Feed) */
            tty_$i_buf_put(0x0C, &tty->output_head);
            if (tty->delay[TTY_DELAY_FF] > TTY_DELAY_THRESHOLD) {
                buf_put_delay(tty, tty->delay[TTY_DELAY_FF], &local_max);
            }

        } else if (ch == 0xFE) {
            /* Escape marker: double it in output buffer */
            tty_$i_buf_put(0xFE, &tty->output_head);
            tty_$i_buf_put(0xFE, &tty->output_head);

        } else {
            /*
             * Normal character: inline buffer insert with spin lock.
             * This path is inlined (not calling tty_$i_buf_put) for
             * performance, since normal chars are the common case.
             */
            uint16_t token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);

            /* Store char at output_buffer[output_read - 1]
             * Assembly: lea (0x0,A0,D1*1),A1; move.b D5b,(0x3D7,A1)
             * = tty + output_read + 0x3D7 = &output_buffer[output_read - 1]
             */
            tty->output_buffer[tty->output_read - 1] = ch;

            /* Advance tail, wrapping 0x100 -> 1 */
            if (tty->output_read == TTY_BUFFER_SIZE) {
                tty->output_read = 1;
            } else {
                tty->output_read++;
            }

            /* Printable chars (>= 0x20) advance column */
            if (ch >= 0x20) {
                TTY_COLUMN(tty)++;
            }

            ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);
        }
    }

    /*
     * Post-loop: acquire lock, optionally set output wait flag,
     * call transmit callback, release lock.
     */
    uint16_t token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);

    /* If we didn't process all requested chars, check buffer fullness */
    if (chars_processed < count) {
        int16_t used = (int16_t)(tty->output_read - tty->output_head);
        if (used < 0) {
            used += TTY_BUFFER_SIZE;
        }
        if (used >= TTY_BUFFER_NEARLY_FULL) {
            /* Set output wait flag (bit 0 of state_flags low byte) */
            tty->state_flags |= TTY_STATUS_OUTPUT_WAIT;
        }
    }

    /*
     * Call transmit callback to start actual I/O.
     * Assembly: move.l (A0),-(SP); movea.l (0x2B4,A0),A1; jsr (A1)
     *
     * xmit_callback is the transmit callback function pointer.
     * line_id (first 4 bytes of tty) is passed as the argument.
     */
    ((void (*)(uint32_t))(uintptr_t)tty->xmit_callback)(tty->line_id);

    ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);

    return chars_processed;
}
