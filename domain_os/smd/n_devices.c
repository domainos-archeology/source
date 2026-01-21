/*
 * smd/n_devices.c - SMD_$N_DEVICES implementation
 *
 * Returns the number of display devices present.
 *
 * Original address: 0x00E70024
 *
 * Assembly:
 *   link.w A6,-0x8
 *   movem.l {A5 D5 D4 D3 D2},-(SP)
 *   lea (0xe82b8c).l,A5      ; SMD_GLOBALS base
 *   clr.w D2w                ; result = 0
 *   moveq #0x1,D4            ; unit = 1
 *   move.w D2w,D3w           ; counter = 0 (loop counter)
 * loop:
 *   move.w D4w,(-0x4,A6)     ; local_unit = unit
 *   pea (-0x4,A6)            ; push &local_unit
 *   bsr.w SMD_$INQ_DISP_TYPE ; call SMD_$INQ_DISP_TYPE
 *   addq.w #0x4,SP
 *   tst.w D0w                ; test result
 *   sne D5b                  ; D5 = (result != 0) ? 0xFF : 0
 *   tst.b D5b
 *   bpl.b skip               ; if D5 >= 0 (i.e., == 0), skip
 *   move.w D4w,D2w           ; result = unit (found valid display)
 * skip:
 *   addq.w #0x1,D4w          ; unit++
 *   dbf D3w,loop             ; loop once (D3 starts at 0, decrements to -1)
 *   move.w D2w,D0w           ; return result
 *   movem.l (-0x1c,A6),{D2 D3 D4 D5 A5}
 *   unlk A6
 *   rts
 *
 * Note: This loops exactly once (D3 starts at 0, dbf decrements and loops while >= 0).
 * So it checks units 1 and 2, returning the highest valid unit number found.
 */

#include "smd/smd_internal.h"

/*
 * SMD_$N_DEVICES - Get number of display devices
 *
 * Returns the number of display devices present in the system.
 * The function iterates through display units and returns the
 * highest valid unit number found.
 *
 * Returns:
 *   Number of display devices (highest valid unit number)
 */
uint16_t SMD_$N_DEVICES(void)
{
    uint16_t result = 0;
    uint16_t unit = 1;
    int16_t counter = 0;
    uint16_t local_unit;
    uint16_t disp_type;

    /* Loop through units (loops once: counter goes 0 -> -1) */
    do {
        local_unit = unit;
        disp_type = SMD_$INQ_DISP_TYPE(&local_unit);

        if (disp_type != 0) {
            /* Found a valid display unit */
            result = unit;
        }

        unit++;
        counter--;
    } while (counter >= 0);

    return result;
}
