/*
 * smd/borrow_display.c - SMD_$BORROW_DISPLAY (0x00E6F584, 374 bytes)
 *
 * Temporarily take a display unit away from the screen manager.
 *
 * Frame:  `link.w A6,-0xc` / `movem.l {A5 A4 A3 A2 D4 D3 D2},-(SP)`;
 *         `lea (0xe82b8c).l,A5` makes A5 the SMD module data base
 *         (&SMD_GLOBALS).  Arguments:
 *           +0x08 unit        longword -> word   (D3)
 *           +0x0C options     longword -> byte   (D4)
 *           +0x10 status_ret  longword           (A4)
 *
 * Address ranges quoted below are from the disassembly of the whole body,
 * 0x00E6F584-0x00E6F6F8.
 */

#include "smd/smd_internal.h"
#include "ml/ml.h"
#include "ec/ec.h"
#include "misc/crash_system.h"

/*
 * Status cell passed to CRASH_SYSTEM by `pea (0x16,PC)` at 0x00E6F6E4
 * (effective address 0x00E6F6E6 + 0x16 = 0x00E6F6FC).  The image bytes there
 * are `00 13 00 0e` (gsk read 00e6f6fc), i.e. module 0x13 code 0x0E,
 * "error borrowing display from screen manager" in the SR10.2 status
 * database.  It is a constant longword in the module's code region, not a
 * string and not NULL.
 */
static const status_$t smd_$borrow_display_err =
    status_$display_error_borrowing_from_screen_manager;

/*
 * SMD_$BORROW_DISPLAY - Temporarily borrow display
 *
 * Parameters:
 *   unit       - Pointer to the display unit number (word)
 *   options    - Pointer to the borrow options byte.  Domain boolean:
 *                negative (0xFF) selects the "full" borrow that clears the
 *                window; non-negative clears the keyboard cursor instead.
 *   status_ret - Status return
 */
void SMD_$BORROW_DISPLAY(int16_t *unit, int8_t *options, status_$t *status_ret)
{
    int8_t valid;                  /* D0b out of smd_$validate_unit */
    uint16_t disp_type;            /* D0w out of SMD_$INQ_DISP_TYPE */
    int8_t type_is_zero;           /* D2b, `seq D2b` at 0x00E6F5B8 */
    uint16_t unit_num;
    smd_display_unit_t *unit_rec;  /* A2, biased by -0xF4 */
    smd_display_hw_t *hw;          /* A3 */
    int32_t wait_value;            /* D2, saved at (-0x4,A6) */
    uint16_t disp_type_bits;       /* D2w reloaded at 0x00E6F6C6 */

    /*
     * 0x00E6F59E-0x00E6F5AC: Pascal result slot, then
     * smd_$validate_unit(*unit).  `tst.b D0b / bpl.b 0x00E6F5BE` takes the
     * ERROR arm when the returned Domain boolean is NOT negative, i.e. when
     * the unit is invalid.  (bead source-xqln: the C had this sense
     * inverted, so it queried the display type on exactly the units the
     * original rejects.)
     */
    valid = smd_$validate_unit((uint16_t)*unit);
    if (valid >= 0) {
        *status_ret = status_$display_invalid_unit_number; /* 0x00E6F5BE */
        return;
    }

    /*
     * 0x00E6F5AE-0x00E6F5BC: SMD_$INQ_DISP_TYPE(unit) - the unit POINTER is
     * pushed (`move.l D3,-(SP)`), with no Pascal result slot.  `tst.w D0w /
     * seq D2b` makes D2b 0xFF when the type is zero, and `tst.b D2b / bpl`
     * then falls into the same error arm.
     */
    disp_type = SMD_$INQ_DISP_TYPE((uint16_t *)unit);
    type_is_zero = (disp_type == 0) ? (int8_t)0xFF : (int8_t)0x00;
    if (type_is_zero < 0) {
        *status_ret = status_$display_invalid_unit_number; /* 0x00E6F5BE */
        return;
    }

    /*
     * 0x00E6F5C8-0x00E6F5DC: A2 = 0xE2E3FC + (word)(unit * 0x10C) - note this
     * site uses `mulu.w` where most SMD sites use `muls.w`; for the single
     * unit that exists (unit 1) the two agree.  A3 = (-0xF4,A2) = the unit's
     * hardware record pointer.
     */
    unit_num = (uint16_t)*unit;
    unit_rec = smd_$unit_rec((int16_t)unit_num);
    hw = unit_rec->hw;

    /* 0x00E6F5D2-0x00E6F5EA: ML_$LOCK(7) */
    ML_$LOCK(SMD_RESPOND_LOCK);

    /* 0x00E6F5EC tst.w (-0xee,A2) / beq.b 0x00E6F608 */
    if (unit_rec->borrowed_asid != 0) {
        ML_$UNLOCK(SMD_RESPOND_LOCK);                        /* 0x00E6F5F8 */
        *status_ret = status_$display_already_borrowed_by_this_process;
        return;                                              /* 0x00E6F604 */
    }

    /* 0x00E6F608 tst.w (-0xf0,A2) / beq.b 0x00E6F66C */
    if (unit_rec->owner_asid != 0) {
        /* 0x00E6F60E-0x00E6F614: D2 = hw->cursor_ec.count + 1, saved to
         * the local at (-0x4,A6). */
        wait_value = hw->cursor_ec.count + 1;

        /*
         * 0x00E6F618 `bset.b #0x7,(0x4c,A3)` sets bit 7 of the BYTE at
         * hw+0x4C, which on the big-endian m68k is bit 15 of the word there.
         * Expressed on the word so the C is endian-neutral.
         */
        hw->field_4c |= 0x8000u;

        /* 0x00E6F61E `pea (0xe2e408).l` / jsr EC_$ADVANCE */
        EC_$ADVANCE(&SMD_BORROW_EC);

        /*
         * 0x00E6F62C-0x00E6F644: EC_$WAIT with both 3-element arrays passed
         * BY VALUE, 24 bytes in all (`lea (0x18,SP),SP` cleans them up).
         * The pushes, from the last one (lowest address) upwards, are
         *   &hw->cursor_ec, 0, 0     (ecs, `pea (0x40,A3)` then two zeros -
         *                             the second zero comes from
         *                             `move.l (SP),-(SP)` duplicating the
         *                             first)
         *   wait_value, 0, 0         (vals)
         * The result in D0 is discarded.  (bead source-xqln: the C called
         * EC_$WAIT_1, a different entry point with a timeout argument.)
         */
        (void)EC_$WAIT((ec_$wait_ecs_t){{ &hw->cursor_ec, (void *)0, (void *)0 }},
                       (ec_$wait_vals_t){{ wait_value, 0, 0 }});

        /*
         * 0x00E6F648-0x00E6F654: `lea (0x0,A5,D0w*0x1),A1` /
         * `tst.b (0x1d99,A1)` -> &SMD_GLOBALS + 0x1D99 + unit, i.e.
         * response_pending[unit - 1].  `bmi` continues, so a NON-negative
         * byte means the screen manager did not grant the borrow.
         */
        if (SMD_GLOBALS.response_pending[unit_num - 1] >= 0) {
            /* 0x00E6F656: status first, then unlock */
            *status_ret = status_$display_borrow_request_denied_by_screen_manager;
            ML_$UNLOCK(SMD_RESPOND_LOCK);                    /* 0x00E6F65E */
            return;                                          /* 0x00E6F668 */
        }
    }

    /* 0x00E6F66C move.w PROC1_$AS_ID,(-0xee,A2) */
    unit_rec->borrowed_asid = PROC1_$AS_ID;

    /* 0x00E6F674-0x00E6F680 ML_$UNLOCK(7) */
    ML_$UNLOCK(SMD_RESPOND_LOCK);

    /* 0x00E6F682-0x00E6F68C move.w (A0),(0x48,A5,D2w*0x1) with D2w = asid*2 */
    SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID] = (uint16_t)*unit;

    /* 0x00E6F690 st (0x3c,A3) */
    hw->tracking_enabled = 0xFF;

    /*
     * 0x00E6F694-0x00E6F6A0: Pascal result slot, then
     * smd_$init_display_state(*options, status_ret) - the options BYTE is
     * pushed with `move.b (A1),-(SP)` and the status pointer with `pea (A4)`.
     */
    smd_$init_display_state(*options, status_ret);

    /* 0x00E6F6A2 tst.l (A4) / bne.b 0x00E6F6F0 - leaves without clr.l (A4) */
    if (*status_ret != status_$ok) {
        return;
    }

    /* 0x00E6F6A6-0x00E6F6B2 tst.b (A1) / bmi skips the call */
    if (*options >= 0) {
        SMD_$CLEAR_KBD_CURSOR(status_ret);
    }

    /*
     * 0x00E6F6B4-0x00E6F6BE: smd_$reset_display_globals(*unit, false).
     * `clr.w -(SP)` pushes the whole word for the `full` boolean, then
     * `move.w (A1),-(SP)` pushes the unit, so the unit is the first argument.
     */
    smd_$reset_display_globals(*unit, (boolean)0);

    /* 0x00E6F6C0-0x00E6F6C4 tst.b (A1) / bpl skips to the epilogue's clr.l */
    if (*options < 0) {
        /*
         * 0x00E6F6C6-0x00E6F6D0: `move.w (A3),D2w` reads the display type
         * word at hw+0x00, and `btst.l D2,D0` with D0 = 0xA86 tests bit
         * (D2 mod 32) of the constant, i.e. types 1, 2, 7, 9 and 11.
         */
        disp_type_bits = *((uint16_t *)hw);
        if ((0xA86u >> (disp_type_bits & 0x1F)) & 1u) {
            /*
             * 0x00E6F6D2-0x00E6F6DE: SMD_$CLEAR_WINDOW(&hw[0x4E], status) -
             * `pea (A4)` is pushed first, `pea (0x4e,A3)` second, so the
             * rectangle is the first argument.
             */
            smd_rect_t *clip_rect = (smd_rect_t *)((uint8_t *)hw + 0x4E);
            SMD_$CLEAR_WINDOW(clip_rect, status_ret);

            /* 0x00E6F6E0 tst.l (A4) / beq skips the crash */
            if (*status_ret != status_$ok) {
                CRASH_SYSTEM(&smd_$borrow_display_err);      /* 0x00E6F6E8 */
            }
        }
    }

    /* 0x00E6F6EE clr.l (A4) */
    *status_ret = status_$ok;
}
