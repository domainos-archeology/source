/*
 * smd/clr_and_load_trk_rect.c - SMD_$CLR_AND_LOAD_TRK_RECT implementation
 *
 * Clears existing tracking rectangles and loads new ones.
 *
 * Original address: 0x00E6E59C
 */

#include "smd/smd_internal.h"

/*
 * SMD_$CLR_AND_LOAD_TRK_RECT - Clear and load tracking rectangles
 *
 * Clears all existing tracking rectangles and loads a new set.
 * This is typically used when the tracking region needs to be
 * completely redefined.
 *
 * Parameters:
 *   rects      - Pointer to array of tracking rectangles
 *   count      - Pointer to number of rectangles to load
 *   status_ret - Pointer to status return
 *
 * Original address: 0x00E6E59C
 *
 * Assembly:
 *   00e6e59c    link.w A6,0x0
 *   00e6e5a0    movem.l {  A5 A2},-(SP)
 *   00e6e5a4    lea (0xe82b8c).l,A5
 *   00e6e5aa    movea.l (0x10,A6),A2          ; A2 = status_ret
 *   00e6e5ae    movea.l (0xc,A6),A0           ; A0 = count ptr
 *   00e6e5b2    move.w (A0),-(SP)             ; push *count
 *   00e6e5b4    move.l (0x8,A6),-(SP)         ; push rects
 *   00e6e5b8    st -(SP)                      ; push clear_flag = 0xFF (clear)
 *   00e6e5ba    bsr.w 0x00e6e4d4              ; smd_$add_trk_rects_internal
 *   00e6e5be    addq.w #0x8,SP
 *   00e6e5c0    tst.b D0b                     ; check result
 *   00e6e5c2    bpl.b 0x00e6e5c8              ; if >= 0, error
 *   00e6e5c4    clr.l (A2)                    ; *status_ret = 0 (success)
 *   00e6e5c6    bra.b 0x00e6e5ce
 *   00e6e5c8    move.l #0x130031,(A2)         ; tracking_list_full error
 *   00e6e5ce    movem.l (-0x8,A6),{  A2 A5}
 *   00e6e5d4    unlk A6
 *   00e6e5d6    rts
 */
void SMD_$CLR_AND_LOAD_TRK_RECT(smd_track_rect_t *rects, uint16_t *count, status_$t *status_ret)
{
    int8_t result;

    /* Clear existing and add new rectangles */
    result = smd_$add_trk_rects_internal((int8_t)0xFF, rects, *count);

    if (result < 0) {
        *status_ret = status_$ok;
    } else {
        *status_ret = status_$display_tracking_list_full;
    }
}
