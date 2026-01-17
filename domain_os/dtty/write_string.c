/*
 * dtty/write_string.c - DTTY_$WRITE_STRING implementation
 *
 * Writes a counted string to the display terminal.
 *
 * Original address: 0x00E1D690
 */

#include "dtty/dtty_internal.h"

/*
 * DTTY_$WRITE_STRING - Write a string to the display
 *
 * Outputs a counted string to the display by calling DTTY_$PUTC
 * for each character. Uses Pascal-style 1-indexed string access.
 *
 * Parameters:
 *   str - Pointer to string data (characters start at str[0])
 *   len - Pointer to string length
 *
 * Note: The original code uses A5 to point to the DTTY data block,
 * and calls dtty_$get_disp_type() though its return value is unused.
 * This may be a side effect of Pascal calling conventions or setting
 * up A5 for potential use by nested functions.
 *
 * Assembly (0x00E1D690):
 *   link.w  A6,#-0x4
 *   movem.l {A5 A2 D3 D2},-(SP)
 *   lea     (0xe2e00c).l,A5      ; A5 = DTTY data block base
 *   movea.l (0x8,A6),A2          ; A2 = str
 *   bsr.w   dtty_$get_disp_type  ; (return value unused)
 *   movea.l (0xc,A6),A0          ; A0 = len
 *   move.w  (A0),D0w             ; D0 = *len
 *   subq.w  #1,D0w               ; D0 = length - 1 (loop counter)
 *   bmi.b   done                 ; Skip if length was 0
 *   move.w  D0w,D3w              ; D3 = loop counter
 *   moveq   #1,D2                ; D2 = 1 (1-based index)
 * loop:
 *   pea     (-0x1,A2,D2w*1)      ; Push &str[D2-1] = &str[0], &str[1], ...
 *   jsr     DTTY_$PUTC
 *   addq.w  #4,SP
 *   addq.w  #1,D2w
 *   dbf     D3w,loop
 * done:
 *   movem.l (-0x14,A6),{D2 D3 A2 A5}
 *   unlk    A6
 *   rts
 */
void DTTY_$WRITE_STRING(char *str, int16_t *len)
{
    int16_t count;
    int16_t i;

    /* Call get_disp_type for side effects (sets up A5 in original) */
    dtty_$get_disp_type();

    /* Get string length and check for empty string */
    count = *len - 1;
    if (count < 0) {
        return;  /* Empty string, nothing to output */
    }

    /* Output each character using 1-based indexing
     * i goes from 1 to *len, accessing str[0] to str[*len-1] */
    for (i = 1; count >= 0; i++, count--) {
        /* Address calculation: str + i - 1 = str + (i-1) = &str[i-1] */
        DTTY_$PUTC(&str[i - 1]);
    }
}
