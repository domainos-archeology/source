/*
 * smd/add_trk_rect.c - SMD_$ADD_TRK_RECT implementation
 *
 * Adds tracking rectangles to the existing list without clearing.
 *
 * Original address: 0x00E6E5D8
 */

#include "smd/smd_internal.h"

/*
 * SMD_$ADD_TRK_RECT - Add tracking rectangles
 *
 * Adds one or more tracking rectangles to the current tracking list.
 * Does not clear existing rectangles - appends to the list.
 *
 * Parameters:
 *   rects      - Pointer to array of tracking rectangles
 *   count      - Pointer to number of rectangles to add
 *   status_ret - Pointer to status return
 *
 * Original address: 0x00E6E5D8
 *
 * Assembly:
 *   00e6e5d8    link.w A6,0x0
 *   00e6e5dc    movem.l {  A5 A2},-(SP)
 *   00e6e5e0    lea (0xe82b8c).l,A5
 *   00e6e5e6    movea.l (0x10,A6),A2          ; A2 = status_ret
 *   00e6e5ea    movea.l (0xc,A6),A0           ; A0 = count ptr
 *   00e6e5ee    move.w (A0),-(SP)             ; push *count
 *   00e6e5f0    move.l (0x8,A6),-(SP)         ; push rects
 *   00e6e5f4    clr.w -(SP)                   ; push clear_flag = 0 (don't clear)
 *   00e6e5f6    bsr.w 0x00e6e4d4              ; smd_$add_trk_rects_internal
 *   00e6e5fa    addq.w #0x8,SP
 *   00e6e5fc    tst.b D0b                     ; check result
 *   00e6e5fe    bpl.b 0x00e6e604              ; if >= 0, error
 *   00e6e600    clr.l (A2)                    ; *status_ret = 0 (success)
 *   00e6e602    bra.b 0x00e6e60a
 *   00e6e604    move.l #0x130031,(A2)         ; tracking_list_full error
 *   00e6e60a    movem.l (-0x8,A6),{  A2 A5}
 *   00e6e610    unlk A6
 *   00e6e612    rts
 */
void SMD_$ADD_TRK_RECT(smd_track_rect_t *rects, uint16_t *count, status_$t *status_ret)
{
    int8_t result;

    /* Add rectangles without clearing existing ones */
    result = smd_$add_trk_rects_internal(0, rects, *count);

    if (result < 0) {
        *status_ret = status_$ok;
    } else {
        *status_ret = status_$display_tracking_list_full;
    }
}
