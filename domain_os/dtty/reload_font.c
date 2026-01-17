/*
 * dtty/reload_font.c - DTTY_$RELOAD_FONT implementation
 *
 * Reloads the standard font into hidden display memory.
 *
 * Original address: 0x00E1D73A
 */

#include "dtty/dtty_internal.h"

/*
 * DTTY_$RELOAD_FONT - Reload the standard font
 *
 * Reloads the standard font into hidden display memory.
 * This is called when the font data may have been corrupted
 * or needs to be refreshed after display mode changes.
 *
 * The font is loaded using the internal dtty_$load_font helper
 * which calls SMD_$COPY_FONT_TO_MD_HDM.
 *
 * Assembly (0x00E1D73A):
 *   link.w  A6,#-0x4
 *   pea     (-0x4,A6)           ; push &status
 *   move.l  #0xe82744,-(SP)     ; push &DTTY_$STD_FONT_P
 *   bsr.w   dtty_$load_font
 *   unlk    A6
 *   rts
 */
void DTTY_$RELOAD_FONT(void)
{
    status_$t status;

    /* Load the standard font to hidden display memory */
    dtty_$load_font(&DTTY_$STD_FONT_P, &status);

    /* Note: Status is ignored here - if font loading fails,
     * there's no recovery mechanism at this level */
}
