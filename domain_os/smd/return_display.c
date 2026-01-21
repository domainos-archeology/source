/*
 * smd/return_display.c - SMD_$RETURN_DISPLAY implementation
 *
 * Return a borrowed display to the screen manager.
 *
 * Original address: 0x00E6F700
 *
 * This function returns a display that was borrowed via SMD_$BORROW_DISPLAY.
 * It restores the display state and notifies the original owner if present.
 *
 * The function performs the following steps:
 * 1. Validates the display unit number
 * 2. Verifies the display was borrowed
 * 3. Unmaps display memory if mapped by this process
 * 4. Acquires display lock
 * 5. Restores display state from saved state
 * 6. Resets display and tracking state
 * 7. Releases display lock
 * 8. Clears borrowed_asid
 * 9. Notifies original owner if display is owned
 *
 * Assembly (key parts):
 *   00e6f700    link.w A6,-0x10
 *   00e6f704    movem.l {  A5 A4 A3 A2 D2},-(SP)
 *   00e6f708    lea (0xe82b8c).l,A5
 *   00e6f70e    movea.l (0x8,A6),A4         ; A4 = unit param
 *   00e6f712    subq.l #0x2,SP
 *   00e6f714    movea.l (0xc,A6),A2         ; A2 = status_ret
 *   00e6f718    move.w (A4),-(SP)           ; push *unit
 *   00e6f71a    bsr.w 0x00e6d700            ; smd_$validate_unit
 *   00e6f71e    addq.w #0x4,SP
 *   00e6f720    tst.b D0b
 *   00e6f722    bmi.b 0x00e6f72e            ; if valid, continue
 *   00e6f724    move.l #0x130001,(A2)       ; invalid unit
 *   ; ... rest of implementation ...
 */

#include "smd/smd_internal.h"
#include "ml/ml.h"

/* Static lock data for acquire display during return */
static int16_t return_display_lock_data = 0;

/*
 * SMD_$RETURN_DISPLAY - Return borrowed display
 *
 * Returns a display that was previously borrowed to the screen manager.
 * Restores the display state and notifies the original owner.
 *
 * Parameters:
 *   unit       - Pointer to display unit number
 *   status_ret - Status return
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_unit_number - Invalid unit number
 *   status_$display_cant_return_not_borrowed - Display not borrowed
 */
void SMD_$RETURN_DISPLAY(int16_t *unit, status_$t *status_ret)
{
    int8_t valid;
    uint16_t unit_num;
    int16_t unit_offset;
    smd_unit_aux_t *unit_aux;
    smd_display_hw_t *hw;

    unit_num = *unit;

    /* Validate unit number */
    valid = smd_$validate_unit(unit_num);
    if (valid >= 0) {
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    /* Get unit auxiliary data */
    unit_offset = unit_num * SMD_DISPLAY_UNIT_SIZE;
    unit_aux = smd_get_unit_aux(unit_num);
    hw = unit_aux->hw;

    /* Check if display is actually borrowed (tracking_enabled used as flag) */
    if (hw->tracking_enabled >= 0) {
        /* Not borrowed */
        *status_ret = status_$display_cant_return_not_borrowed;
        return;
    }

    /* Map this display to current process's ASID for the return operation */
    SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID] = unit_num;

    /* If we (the borrower) have the display mapped, unmap it */
    if (unit_aux->borrowed_asid == PROC1_$AS_ID) {
        SMD_$UNMAP_DISPLAY_U(status_ret);
        /* Ignore "not mapped" error - it's expected if we didn't map */
        if (*status_ret == status_$display_memory_not_mapped) {
            *status_ret = status_$ok;
        }
    }

    /* Acquire display for the return operation */
    SMD_$ACQ_DISPLAY(&return_display_lock_data);

    /* Restore saved display state from offset 0x10 to 0x1C
     * Original: hw->field_1c = hw->field_10 */
    hw->field_1c = *((uint32_t *)((uint8_t *)hw + 0x10));

    /* Reset tracking state (no full reset) */
    smd_$reset_tracking_state(unit_num, 0);

    /* Reset display state (full reset) */
    smd_$reset_display_state(unit_num, 0xFF);

    /* Release the display */
    SMD_$REL_DISPLAY();

    /* Clear borrowed_asid */
    unit_aux->borrowed_asid = 0;

    /* Clear the borrowed flag */
    hw->tracking_enabled = 0;

    /* If display has an owner, signal them that borrow is complete */
    if (unit_aux->owner_asid != 0) {
        /* Set return-complete flag (bit 6) in cursor flags */
        *(uint8_t *)((uint8_t *)hw + 0x4c) |= 0x40;

        /* Advance borrow event count to wake owner */
        EC_$ADVANCE(&SMD_BORROW_EC);
    }
}
