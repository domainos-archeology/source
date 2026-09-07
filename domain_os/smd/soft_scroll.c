/*
 * smd/soft_scroll.c - SMD_$SOFT_SCROLL implementation
 *
 * Initiates a software scroll operation on the display.
 *
 * Original address: 0x00E6F326
 */

#include "smd/smd_internal.h"

/*
 * SMD_$SOFT_SCROLL - Perform software scroll operation
 *
 * Scrolls a rectangular region of the display by the specified delta amounts.
 * This is the user-mode entry point for scroll operations.
 *
 * Parameters:
 *   scroll_rect - Pointer to rectangle defining the scroll region (8 bytes)
 *                 Contains: x1, y1, x2, y2 as packed uint16_t values
 *   scroll_dx   - Pointer to scroll direction (0-3, see SMD_SCROLL_DIR_*)
 *   scroll_dy   - Pointer to vertical scroll amount (number of pixels)
 *   status_ret  - Status return
 *
 * The function:
 * 1. Validates that the current ASID has an associated display unit
 * 2. Acquires the display lock for exclusive access
 * 3. Copies scroll parameters to the display hardware structure
 * 4. Initiates the scroll operation via SMD_$START_SCROLL
 * 5. Records which ASID initiated the scroll
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_use_of_driver_procedure - No display unit for ASID
 *
 * Original assembly at 0x00E6F326:
 *   link.w A6,-0x10
 *   movem.l {A5 A4 A3 A2 D3 D2},-(SP)
 *   lea (0xe82b8c).l,A5              ; SMD globals base
 *   move.w (0x00e2060a).l,D0w        ; PROC1_$AS_ID
 *   move.l (0x14,A6),D3              ; status_ret
 *   add.w D0w,D0w
 *   move.w (0x48,A5,D0w*0x1),D2w     ; asid_to_unit[PROC1_$AS_ID]
 *   bne.b valid_unit
 *   movea.l D3,A0
 *   move.l #0x130004,(A0)            ; status_$display_invalid_use_of_driver_procedure
 *   bra.b done
 * valid_unit:
 *   pea lock_data
 *   bsr.w SMD_$ACQ_DISPLAY
 *   ...
 */
void SMD_$SOFT_SCROLL(smd_scroll_rect_t *scroll_rect, int16_t *scroll_dx,
                      int16_t *scroll_dy, status_$t *status_ret)
{
    int16_t unit;
    uint16_t as_id;
    smd_display_hw_t *hw;
    smd_display_unit_t *rec;

    /* 0x00e6f334-0x00e6f340 */
    as_id = PROC1_$AS_ID;
    unit = (int16_t)SMD_GLOBALS.asid_to_unit[as_id];

    if (unit == 0) {
        /* 0x00e6f348 */
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* 0x00e6f350 pea (-0x1a26,PC) -> 0x00e6f352 - 0x1a26 = 0x00e6d92c */
    SMD_$ACQ_DISPLAY((int16_t *)&SMD_ACQ_LOCK_DATA);

    /* 0x00e6f35c-0x00e6f36e: A2 = 0xE2E3FC + unit*0x10C, hw at (-0xF4,A2) */
    rec = smd_$unit_rec(unit);
    hw = rec->hw;

    /* 0x00e6f374/0x00e6f378: the scroll rectangle is copied as two longwords
     * through a post-incrementing source pointer. */
    hw->scroll_x1 = scroll_rect->x1;
    hw->scroll_y1 = scroll_rect->y1;
    hw->scroll_x2 = scroll_rect->x2;
    hw->scroll_y2 = scroll_rect->y2;

    /* 0x00e6f380: the second argument goes to +0x30 (scroll_dx) ... */
    hw->scroll_dx = *scroll_dx;
    /* 0x00e6f388: ... and the third to +0x2E (scroll_dy) */
    hw->scroll_dy = *scroll_dy;

    /* 0x00e6f38c-0x00e6f392: the second argument is the record's controller
     * register base (+0xFC), pushed by value. */
    SMD_$START_SCROLL(hw, rec->ctrl_regs);

    /* 0x00e6f398: the ASID is re-read from the global here, and the store is
     * to (-0xec,A2), i.e. the record's +0x08 field. */
    rec->field_08 = PROC1_$AS_ID;

    /* 0x00e6f3a2 */
    *status_ret = status_$ok;
}
