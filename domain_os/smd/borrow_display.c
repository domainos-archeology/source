/*
 * smd/borrow_display.c - SMD_$BORROW_DISPLAY implementation
 *
 * Temporarily borrow a display from the screen manager.
 *
 * Original address: 0x00E6F584
 *
 * This function allows a process to temporarily take over a display unit
 * from the screen manager. If the display is currently owned by another
 * process, the function signals that process and waits for permission.
 *
 * The function performs the following steps:
 * 1. Validates the display unit number
 * 2. Acquires the respond lock for synchronization
 * 3. Checks if already borrowed by this process
 * 4. If owned by another process, requests borrow permission and waits
 * 5. Sets up the borrowed state
 * 6. Initializes display state for the borrower
 * 7. Clears keyboard cursor if options indicate
 * 8. Resets tracking state
 * 9. Clears window for certain display types
 *
 * Assembly (key parts):
 *   00e6f584    link.w A6,-0xc
 *   00e6f588    movem.l {  A5 A4 A3 A2 D4 D3 D2},-(SP)
 *   00e6f58c    lea (0xe82b8c).l,A5
 *   00e6f592    move.l (0x8,A6),D3           ; D3 = unit param
 *   00e6f596    move.l (0xc,A6),D4           ; D4 = options param
 *   00e6f59a    movea.l (0x10,A6),A4         ; A4 = status_ret
 *   ; ... validates unit and checks borrow state ...
 */

#include "smd/smd_internal.h"
#include "ml/ml.h"
#include "misc/crash_system.h"

/*
 * Status cell passed to CRASH_SYSTEM by `pea (0x16,PC)` at 0x00E6F6E4
 * (effective address 0x00E6F6E6 + 0x16 = 0x00E6F6FC), a constant longword in
 * this module's code region.  Read with `gsk read 00e6f6fc`.
 *
 * Module 0x13 code 0x0E is "error borrowing display from screen manager" in
 * the SR10.4 status database (OS / display driver), which is exactly the
 * failure this cell reports: SMD_$CLEAR_WINDOW returned an error while
 * SMD_$BORROW_DISPLAY was taking the display over.
 */
static const status_$t smd_$borrow_display_err =
    status_$display_error_borrowing_from_screen_manager;

/*
 * SMD_$BORROW_DISPLAY - Temporarily borrow display
 *
 * Allows a process to temporarily take control of a display unit.
 * Used by programs that need direct display access (e.g., debuggers,
 * screen savers, games).
 *
 * Parameters:
 *   unit       - Pointer to display unit number
 *   options    - Pointer to borrow options byte:
 *                Negative: Enable video, clear window
 *                Positive: Just borrow, don't clear
 *   status_ret - Status return
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_unit_number - Invalid unit number
 *   status_$display_already_borrowed_by_this_process - Already borrowed
 *   status_$display_borrow_request_denied_by_screen_manager - Request denied
 */
void SMD_$BORROW_DISPLAY(int16_t *unit, int8_t *options, status_$t *status_ret)
{
    int8_t valid;
    int16_t disp_type;
    uint16_t unit_num;
    int16_t unit_offset;
    smd_display_unit_t *unit_aux;
    smd_display_hw_t *hw;
    uint32_t wait_value;
    int16_t wait_result;

    unit_num = *unit;

    /* Validate unit number - must be valid AND have a valid display type */
    valid = smd_$validate_unit(unit_num);
    if (valid >= 0) {
        /* Check if display type is non-zero */
        disp_type = SMD_$INQ_DISP_TYPE((uint16_t *)unit);
        if (disp_type == 0) {
            *status_ret = status_$display_invalid_unit_number;
            return;
        }
    } else {
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    /* Calculate unit offset for auxiliary data access */
    unit_offset = unit_num * SMD_DISPLAY_UNIT_SIZE;
    unit_aux = smd_$unit_rec(unit_num);
    hw = unit_aux->hw;

    /* Acquire the respond lock for synchronization */
    ML_$LOCK(SMD_RESPOND_LOCK);

    /* Check if already borrowed by this process */
    if (unit_aux->borrowed_asid != 0) {
        ML_$UNLOCK(SMD_RESPOND_LOCK);
        *status_ret = status_$display_already_borrowed_by_this_process;
        return;
    }

    /* If display is currently owned by another process, request borrow */
    if (unit_aux->owner_asid != 0) {
        /* Save current cursor event count */
        wait_value = hw->cursor_ec.count;

        /*
         * 0x00E6F618 "bset.b #0x7,(0x4c,A3)" sets bit 7 of the *byte* at
         * hw+0x4C, which on a big-endian m68k is bit 15 of the word there.
         * Expressed on the word so the C is endian-neutral.
         */
        hw->field_4c |= 0x8000u;

        /* Advance the borrow event count to signal owner */
        EC_$ADVANCE(&SMD_BORROW_EC);

        /* Wait for owner to respond (advance cursor_ec) */
        wait_result = EC_$WAIT_1(&hw->cursor_ec, wait_value + 1, NULL, 0);

        /* Check borrow response */
        /* 0x00E6F64C "lea (0x0,A5,D0w*0x1),A1" / 0x00E6F650
         * "tst.b (0x1d99,A1)" -> SMD_GLOBALS + 0x1D99 + unit, i.e.
         * response_pending[unit - 1]; "bpl" means "not granted". */
        if (SMD_GLOBALS.response_pending[unit_num - 1] >= 0) {
            /* Borrow request was denied */
            *status_ret = status_$display_borrow_request_denied_by_screen_manager;
            ML_$UNLOCK(SMD_RESPOND_LOCK);
            return;
        }
    }

    /* Set the borrowed_asid to current process */
    unit_aux->borrowed_asid = PROC1_$AS_ID;

    /* Release the respond lock */
    ML_$UNLOCK(SMD_RESPOND_LOCK);

    /* Map this display to current process's ASID */
    SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID] = unit_num;

    /* Mark display as borrowed (tracking_enabled byte used as borrowed flag) */
    hw->tracking_enabled = 0xFF;

    /* Initialize display state */
    smd_$init_display_state(*options, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* If options is non-negative, clear the keyboard cursor */
    if (*options >= 0) {
        SMD_$CLEAR_KBD_CURSOR(status_ret);
    }

    /* Reset tracking state for this unit */
    smd_$reset_tracking_state(unit_num, 0);

    /* If options is negative and display type supports it, clear the window */
    if (*options < 0) {
        uint16_t disp_type_bits = *((uint16_t *)hw);  /* Display type at hw+0 */

        /* Check if display type is one that needs window clearing */
        /* Bitmask 0xA86 = types 1, 2, 7, 9, 11 (bits 1,2,7,9,11 set) */
        if ((1 << (disp_type_bits & 0x1F)) & 0xA86) {
            /* Clear the window region at hw+0x4E (clip region) */
            smd_rect_t *clip_rect = (smd_rect_t *)((uint8_t *)hw + 0x4E);
            SMD_$CLEAR_WINDOW(clip_rect, status_ret);

            if (*status_ret != status_$ok) {
                /* Fatal error - crash system (0x00E6F6E8) */
                CRASH_SYSTEM(&smd_$borrow_display_err);
            }
        }
    }

    *status_ret = status_$ok;
}
