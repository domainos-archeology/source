/*
 * win/spin_down.c - WIN_$SPIN_DOWN (0x00E19BC0, 58 bytes)
 *
 * Jump-table entry +0x04 (shutdown), called with the address of the unit
 * word.  Issues the ANSI SPIN CONTROL command (0x55) and returns 0x14 when
 * it succeeded, else the status with its LOW word cleared (`clr.w D0w` at
 * 0x00E19BF0 leaves the module half of the status in the high word).
 *
 * Frame (link.w A6,-0x8; A5 saved), A5 = 0xE2B89C:
 *   A6-0x06  2  out     WIN_$ANSI_COMMAND's output byte cell
 *   A6-0x04  4  status
 */

#include "win/win_internal.h"

uint32_t WIN_$SPIN_DOWN(uint16_t *unit_ptr)
{
    char out[2];                        /* A6-0x06 */
    status_$t status;                   /* A6-0x04 */

    /* 0x00E19BCC-0x00E19BE6: the input byte is the shared cell
     * WIN_ANSI_IN_PARAM (pea (-0x7c8,PC): 0x00E19BD2 - 0x7C8 = 0x00E1940A). */
    status = WIN_$ANSI_COMMAND(*unit_ptr, ANSI_CMD_SPIN_CONTROL,
                               (char *)&WIN_ANSI_IN_PARAM, out);

    /* 0x00E19BEA-0x00E19BF0 */
    if (status == status_$ok) {
        return 0x14;
    }
    return (uint32_t)status & 0xFFFF0000u;
}
