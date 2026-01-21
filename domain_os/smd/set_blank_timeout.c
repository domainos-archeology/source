/*
 * smd/set_blank_timeout.c - SMD_$SET_BLANK_TIMEOUT implementation
 *
 * Sets the display blank timeout value.
 *
 * Original address: 0x00E6F192
 *
 * Assembly:
 *   link.w A6,0x0
 *   pea (A5)
 *   lea (0xe82b8c).l,A5      ; SMD_GLOBALS base
 *   movea.l (0xc,A6),A0      ; param2 (timeout ptr)
 *   move.l (A0),(0xd8,A5)    ; SMD_GLOBALS.blank_timeout = *timeout
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
 * SMD_$SET_BLANK_TIMEOUT - Set display blank timeout
 *
 * Sets the timeout value after which the display will automatically blank
 * to save power or prevent burn-in.
 *
 * Parameters:
 *   param_1    - Unused (reserved for future use or API compatibility)
 *   timeout    - Pointer to timeout value (in system clock units)
 *   status_ret - Output: status return
 *
 * Returns:
 *   status_$ok always
 */
void SMD_$SET_BLANK_TIMEOUT(uint32_t param_1, uint32_t *timeout, status_$t *status_ret)
{
    (void)param_1;  /* Unused */

    SMD_GLOBALS.blank_timeout = *timeout;
    *status_ret = status_$ok;
}
