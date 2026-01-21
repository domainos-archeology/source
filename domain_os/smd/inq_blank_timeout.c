/*
 * smd/inq_blank_timeout.c - SMD_$INQ_BLANK_TIMEOUT implementation
 *
 * Inquires the display blank timeout value.
 *
 * Original address: 0x00E6F1B4
 *
 * Assembly:
 *   link.w A6,0x0
 *   pea (A5)
 *   lea (0xe82b8c).l,A5      ; SMD_GLOBALS base
 *   movea.l (0xc,A6),A0      ; param2 (output buffer ptr)
 *   move.l (0xd8,A5),(A0)    ; buffer[0] = SMD_GLOBALS.blank_timeout
 *   clr.w (0x4,A0)           ; buffer[2] = 0 (extra word cleared)
 *   movea.l (0x10,A6),A1     ; status_ret
 *   clr.l (A1)               ; *status_ret = 0 (success)
 *   movea.l (-0x4,A6),A5
 *   unlk A6
 *   rts
 *
 * Note: param_1 is unused in this implementation.
 */

#include "smd/smd_internal.h"

/*
 * SMD_$INQ_BLANK_TIMEOUT - Inquire display blank timeout
 *
 * Returns the current timeout value after which the display will
 * automatically blank.
 *
 * Parameters:
 *   param_1    - Unused (reserved for future use or API compatibility)
 *   timeout    - Output: receives timeout value (6 bytes: 4-byte value + 2-byte reserved)
 *   status_ret - Output: status return
 *
 * Returns:
 *   status_$ok always
 */
void SMD_$INQ_BLANK_TIMEOUT(uint32_t param_1, uint32_t *timeout, status_$t *status_ret)
{
    (void)param_1;  /* Unused */

    /* Return timeout value */
    timeout[0] = SMD_GLOBALS.blank_timeout;

    /* Clear extra word at offset +4 */
    *(uint16_t *)(timeout + 1) = 0;

    *status_ret = status_$ok;
}
