/*
 * flp/shake.c - SHAKE (0x00E3E49E, 128 bytes)
 *
 * The FDC data-register handshake.  For each of *count_ptr words it waits
 * (up to 2000 polls) for RQM, checks that the FDC's DIO bit matches the
 * requested direction, and moves one byte: a read stores the byte as a
 * word, a write sends the word's low byte.
 *
 * Frame (link.w A6,-0x10; A5 A3 A2 D2 saved):
 *   A3/A2       data      argument 1, A2 walks it two bytes at a time
 *   (0xc,A6)    count_ptr argument 2
 *   A0          dir_ptr   argument 3
 *   A1          the register base
 *   D1          dbf counter (*count_ptr - 1)
 *   D0          the 2000-poll budget, reloaded for every word
 *   D2          the byte read, zero-extended
 */

#include "flp/flp_internal.h"

status_$t SHAKE(uint16_t *data, int16_t *count_ptr, int16_t *dir_ptr)
{
    volatile flp_regs_t *regs;          /* A1 */
    int16_t remaining;                  /* D1 */
    int16_t polls;                      /* D0 */
    uint16_t byte;                      /* D2 */

    /* 0x00E3E4AC-0x00E3E4BC: a count of zero (or less) is success at once. */
    regs = FLP_REGS();
    remaining = (int16_t)(*count_ptr - 1);
    if (remaining < 0) {
        return status_$ok;                          /* 0x00E3E512 */
    }

    /* 0x00E3E4C6-0x00E3E50E: `dbf D1w` - *count_ptr iterations. */
    do {
        polls = 0x7D0;                              /* 0x00E3E4C6 */

        /* 0x00E3E4DA-0x00E3E4DE / 0x00E3E4CC-0x00E3E4D8: spin on RQM (the
         * sign bit of the status byte); the budget is counted down first
         * and tested with `bgt`, so 2000 polls are allowed. */
        while ((int8_t)regs->status >= 0) {
            polls--;
            if (polls <= 0) {
                return status_$disk_controller_timeout;     /* 0x00E3E4D2 */
            }
        }

        if ((regs->status & FLP_STATUS_DIO) != 0) {
            /* 0x00E3E4E8-0x00E3E4F4: the FDC has a byte for us; only a read
             * (direction 0) may take it. */
            if (*dir_ptr != 0) {
                return status_$disk_controller_error;       /* 0x00E3E504 */
            }
            byte = regs->data;
            *data = byte;
        } else {
            /* 0x00E3E4F6-0x00E3E502: the FDC wants a byte; only a write
             * (direction 1) may give it - the low byte of the word. */
            if (*dir_ptr != 1) {
                return status_$disk_controller_error;       /* 0x00E3E504 */
            }
            regs->data = (uint8_t)(*data & 0xFF);
        }

        data++;                                     /* 0x00E3E50C */
        remaining--;
    } while (remaining >= 0);

    return status_$ok;                              /* 0x00E3E512 */
}
