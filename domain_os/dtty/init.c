/*
 * dtty/init.c - DTTY_$INIT (0x00E34BD0, 286 bytes)
 *
 * Initialise the Display TTY subsystem.
 *
 * Frame: `link.w A6,-0x44` / `movem.l {A2 D3 D2},-(SP)`.  Arguments:
 *   +0x08 mode  longword -> word  (A0, D2w)
 *   +0x0C ctrl  longword -> word  (A1, D0w)
 * A2 is set to 0xE2E00C, the DTTY module data block, and every global store
 * below is a displacement off it.
 *
 * Locals (all A6-relative; the whole body is 0x00E34BD0-0x00E34CEC):
 *   -0x2E  assoc_unit   word, set to 1 in BOTH display-type arms
 *   -0x2C  status       longword
 *   -0x28  window       smd_rect_t: x1 (-0x28), x2 (-0x26), y1 (-0x24),
 *                       y2 (-0x22).  The x2/y2 pair is written in the
 *                       display-type arm; the x1/y1 pair is cleared later,
 *                       at 0x00E34C92/0x00E34C96, only on the path that
 *                       actually reaches dtty_$clear_window.
 */

#include "dtty/dtty_internal.h"

/*
 * File-static constants living in this module's code region, immediately
 * after the function body.  All four are reached with `pea (d,PC)`.
 *
 *   0x00E34CEE  00 01                    -> the word 1, the display unit
 *                                           handed to SMD_$INQ_DISP_TYPE by
 *                                           `pea (0xf2,PC)` at 0x00E34BFA
 *   0x00E34CF0  "smd_$copy_font_to_md_hdm%"  (pea (0x18,PC) at 0x00E34CD6)
 *   0x00E34D0A  "$"                          (pea (0x80,PC) at 0x00E34C88,
 *                                             (0x58,PC) at 0x00E34CB0,
 *                                             (0x36,PC) at 0x00E34CD2)
 *   0x00E34D0C  "smd_$assoc%"                (pea (0x7e,PC) at 0x00E34C8C)
 *   0x00E34D18  "dtty_$clear_window%"        (pea (0x62,PC) at 0x00E34CB4)
 *
 * Image bytes, `gsk read 00e34cee 80`:
 *   00e34cee  00 01 73 6d 64 5f 24 63  6f 70 79 5f 66 6f 6e 74
 *   00e34cfe  5f 74 6f 5f 6d 64 5f 68  64 6d 25 24 24 00 73 6d
 *   00e34d0e  64 5f 24 61 73 73 6f 63  25 24 64 74 74 79 5f 24
 *   00e34d1e  63 6c 65 61 72 5f 77 69  6e 64 6f 77 25 24 4e 56
 *
 * The '%' is the Domain Pascal format-string terminator VFMT_$WRITE10 stops
 * at, and the '$' bytes that follow each odd-length literal (0x00E34D09,
 * 0x00E34D17, 0x00E34D2B) are the compiler's pad to the next even address.
 * The one-character cell at 0x00E34D0A is the `context` argument of
 * dtty_$report_error, which tests only its first byte (`cmpi.b #'$',(A2)`)
 * and prints nothing further when it is '$'.
 *
 * (bead source-4km0: these were previously C string literals without the '%'
 * terminator, and the unit cell was a stack local.)
 */
static const uint16_t dtty_$init_unit = 1;              /* 0x00E34CEE */
static const char dtty_$msg_copy_font[] =
    "smd_$copy_font_to_md_hdm%";                        /* 0x00E34CF0 */
static const char dtty_$msg_no_context[] = "$";         /* 0x00E34D0A */
static const char dtty_$msg_assoc[] = "smd_$assoc%";    /* 0x00E34D0C */
static const char dtty_$msg_clear_window[] =
    "dtty_$clear_window%";                              /* 0x00E34D18 */

/*
 * DTTY_$INIT - Initialize Display TTY subsystem
 *
 * Parameters:
 *   mode - Pointer to the mode word
 *   ctrl - Pointer to the control word (stored in DTTY_$CTRL)
 *
 * DTTY_$USE_DTTY is computed at 0x00E34C4C-0x00E34C60 as
 *
 *     (mode == 1) | ((mode == 0) & (status_reg bit 0))
 *
 * in Domain boolean bytes, and then `bmi.b 0x00E34CE4` LEAVES when the
 * result is true.  The SMD association / window clear / font load below is
 * therefore the DTTY-disabled path: when the PROM display TTY is in use,
 * DTTY_$INIT does nothing more.
 */
void DTTY_$INIT(int16_t *mode, uint16_t *ctrl)
{
    int16_t mode_val;              /* D2w */
    uint16_t disp_status;          /* D0w, the display status register */
    int8_t hw_present;             /* D1b, `sne` at 0x00E34C50 */
    int8_t mode_is_zero;           /* D3b, `seq` at 0x00E34C54 */
    int8_t mode_is_one;            /* D3b, `seq` at 0x00E34C5C */
    uint16_t assoc_unit;           /* (-0x2E,A6) */
    smd_rect_t window;             /* (-0x28,A6) */
    status_$t status;              /* (-0x2C,A6) */
    const char *error_func;

    /* 0x00E34BD8-0x00E34BE2 */
    mode_val = *mode;

    /* 0x00E34BEA move.w D0w,(0x2,A2) */
    DTTY_$CTRL = *ctrl;

    /* 0x00E34BEE st (0x8,A2) */
    DTTY_$USE_DTTY = (int8_t)0xFF;

    /* 0x00E34BF2 st (0x6,A2) - previously omitted (bead source-4km0) */
    DTTY_FLAG_06 = (int8_t)0xFF;

    /* 0x00E34BF6 clr.b (0x4,A2) - previously omitted (bead source-4km0) */
    DTTY_FLAG_04 = 0;

    /* 0x00E34BFA-0x00E34C06: the argument is the constant cell at 0x00E34CEE */
    DTTY_$DISP_TYPE = SMD_$INQ_DISP_TYPE((uint16_t *)&dtty_$init_unit);

    /* 0x00E34C08-0x00E34C16: switch on the display type; anything but 1 or 2
     * branches straight to the epilogue. */
    if (DTTY_$DISP_TYPE == DTTY_DISP_TYPE_15_INCH) {
        /* 0x00E34C1A-0x00E34C32: 15" portrait, 800 x 1024 */
        assoc_unit = 1;                                 /* 0x00E34C1A */
        disp_status = DTTY_DISP_STATUS_15();            /* 0x00E34C20 */
        window.x2 = 0x31F;                              /* 0x00E34C26, -0x26 */
        window.y2 = 0x3FF;                              /* 0x00E34C2C, -0x22 */
    }
    else if (DTTY_$DISP_TYPE == DTTY_DISP_TYPE_19_INCH) {
        /* 0x00E34C34-0x00E34C4A: 19" landscape, 1024 x 800 */
        assoc_unit = 1;                                 /* 0x00E34C34 */
        disp_status = DTTY_DISP_STATUS_19();            /* 0x00E34C3A */
        window.y2 = 0x31F;                              /* 0x00E34C40, -0x22 */
        window.x2 = 0x3FF;                              /* 0x00E34C46, -0x26 */
    }
    else {
        return;                                         /* 0x00E34C16 */
    }

    /*
     * 0x00E34C4C-0x00E34C60:
     *   btst.l #0,D0 / sne D1b     -> hw_present
     *   tst.w D2w    / seq D3b     -> mode_is_zero
     *   and.b D3b,D1b
     *   cmpi.w #1,D2w / seq D3b    -> mode_is_one
     *   or.b  D3b,D1b
     *   move.b D1b,(0x8,A2)
     */
    hw_present = (disp_status & 1u) ? (int8_t)0xFF : (int8_t)0x00;
    mode_is_zero = (mode_val == 0) ? (int8_t)0xFF : (int8_t)0x00;
    hw_present = (int8_t)(hw_present & mode_is_zero);
    mode_is_one = (mode_val == 1) ? (int8_t)0xFF : (int8_t)0x00;
    hw_present = (int8_t)(hw_present | mode_is_one);
    DTTY_$USE_DTTY = hw_present;

    /* 0x00E34C64 bmi.b 0x00E34CE4 - a TRUE flag ends the function here */
    if (DTTY_$USE_DTTY < 0) {
        return;
    }

    /* 0x00E34C66 clr.w (0x2,A2) */
    DTTY_$CTRL = 0;

    /*
     * 0x00E34C6A-0x00E34C7E: SMD_$ASSOC(&assoc_unit, &PROC1_$CURRENT,
     * &status).  The middle argument is the literal address 0xE20608 pushed
     * with `move.l #0xe20608,-(SP)`.
     */
    SMD_$ASSOC(&assoc_unit, &PROC1_$CURRENT, &status);

    /* 0x00E34C82 tst.l (-0x2c,A6) */
    if (status != status_$ok) {
        error_func = dtty_$msg_assoc;                   /* 0x00E34C8C */
        goto report_error;                              /* 0x00E34C90 */
    }

    /* 0x00E34C92/0x00E34C96: the x1/y1 pair is cleared only here */
    window.x1 = 0;
    window.y1 = 0;

    /* 0x00E34C9A-0x00E34CA8 dtty_$clear_window(&window, &status) */
    dtty_$clear_window(&window, &status);

    /* 0x00E34CAA tst.l (-0x2c,A6) */
    if (status != status_$ok) {
        error_func = dtty_$msg_clear_window;            /* 0x00E34CB4 */
        goto report_error;                              /* 0x00E34CB8 */
    }

    /* 0x00E34CBA-0x00E34CCA dtty_$load_font(&DTTY_$STD_FONT_P, &status);
     * the font argument is the literal address 0xE82744. */
    dtty_$load_font(&DTTY_$STD_FONT_P, &status);

    /* 0x00E34CCC tst.l (-0x2c,A6) */
    if (status != status_$ok) {
        error_func = dtty_$msg_copy_font;               /* 0x00E34CD6 */
        goto report_error;                              /* falls through */
    }

    return;                                             /* 0x00E34CD0 */

report_error:
    /* 0x00E34CDA-0x00E34CE2: the status longword is pushed last, so it is
     * the first argument; the context string is always the "$" cell. */
    dtty_$report_error(status, error_func, dtty_$msg_no_context);
}
