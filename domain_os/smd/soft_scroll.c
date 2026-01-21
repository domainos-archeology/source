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
    uint16_t unit;
    uint16_t as_id;
    smd_display_hw_t *hw;
    smd_display_unit_t *unit_ptr;

    /* Get current address space ID */
    as_id = PROC1_$AS_ID;

    /* Look up display unit for this ASID */
    unit = SMD_GLOBALS.asid_to_unit[as_id];

    if (unit == 0) {
        /* No display unit associated with this ASID */
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* Acquire display lock for exclusive access */
    /* Lock data is at a fixed offset within the SMD module */
    SMD_$ACQ_DISPLAY(&SMD_ACQ_LOCK_DATA);

    /* Get pointer to display unit data */
    /* Unit number is used as index into display unit array */
    unit_ptr = &SMD_DISPLAY_UNITS[unit];

    /* Get hardware info pointer from display unit */
    hw = unit_ptr->hw;

    /* Copy scroll region coordinates to hardware structure */
    /* Scroll region is stored as two 32-bit values (x1/y1 and x2/y2) */
    hw->scroll_x1 = scroll_rect->x1;
    hw->scroll_y1 = scroll_rect->y1;
    hw->scroll_x2 = scroll_rect->x2;
    hw->scroll_y2 = scroll_rect->y2;

    /* Copy scroll direction/amount */
    hw->scroll_dx = *scroll_dx;
    hw->scroll_dy = *scroll_dy;

    /* Start the scroll operation */
    /* Pass hardware structure and event count for completion signaling */
    SMD_$START_SCROLL(hw, &unit_ptr->event_count_1);

    /* Record which ASID initiated this scroll operation */
    /* This is stored at offset -0xEC from end of unit structure */
    /* which corresponds to unit_ptr->asid */
    unit_ptr->asid = as_id;

    *status_ret = status_$ok;
}
