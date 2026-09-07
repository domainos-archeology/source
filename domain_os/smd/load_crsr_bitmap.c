/*
 * smd/load_crsr_bitmap.c - SMD_$LOAD_CRSR_BITMAP implementation
 *
 * Loads a cursor bitmap into the cursor table.
 * Up to 4 cursors (0-3) can be defined.
 *
 * Original address: 0x00E6FBC6
 */

#include "smd/smd_internal.h"

/*
 * The two constant words this function passes by reference live in the code
 * region and are reached with pea (d,PC):
 *   0x00E6FC86 pea (-0x1c90,PC) -> 0x00E6FC88 - 0x1C90 = 0x00E6DFF8 (value 1)
 *   0x00E6FD00 pea (-0x1d0a,PC) -> 0x00E6FD02 - 0x1D0A = 0x00E6DFF8 (value 1)
 *   0x00E6FC96 pea (-0x236c,PC) -> 0x00E6FC98 - 0x236C = 0x00E6D92C (value 0)
 * They are SMD_SYNC_LOCK_DATA and SMD_ACQ_LOCK_DATA (smd_internal.h).
 */

/*
 * Cursor bitmap structure:
 *   Offset 0x00: width (int16_t)
 *   Offset 0x02: height (int16_t)
 *   Offset 0x04: hot_x (int16_t)
 *   Offset 0x06: hot_y_offset (int16_t) - computed as (height-1) - hot_y
 *   Offset 0x08: bitmap data (height * 2 bytes)
 */

/*
 * SMD_$LOAD_CRSR_BITMAP - Load cursor bitmap
 *
 * Loads a cursor bitmap definition for the specified cursor number.
 * The bitmap data is copied into the cursor table for the current
 * display unit.
 *
 * Parameters:
 *   param1     - Unused (reserved)
 *   cursor_num - Pointer to cursor number (0-3)
 *   width_ptr  - Pointer to cursor width (1-16)
 *   height_ptr - Pointer to cursor height (1-16)
 *   hot_x_ptr  - Pointer to hot spot X (0-15)
 *   hot_y_ptr  - Pointer to hot spot Y (0-15)
 *   bitmap     - Pointer to bitmap data (16 uint16_t values)
 *   status_ret - Pointer to status return
 *
 * Validation:
 *   - cursor_num must be 0-3
 *   - width, height must be 1-16
 *   - hot_x, hot_y must be 0-15
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_cursor_number - Invalid cursor number
 *   status_$display_invalid_scroll_displacement - Invalid dimensions/hotspot
 *
 * Original address: 0x00E6FBC6
 *
 * Assembly:
 *   00e6fbc6    link.w A6,-0x20
 *   00e6fbca    movem.l {  A5 A4 A3 A2 D7 D6 D5 D4 D3 D2},-(SP)
 *   00e6fbce    lea (0xe82b8c).l,A5
 *   00e6fbd4    move.l (0x24,A6),D7           ; D7 = status_ret
 *   00e6fbd8    movea.l (0xc,A6),A0
 *   00e6fbdc    movea.l (0x10,A6),A1
 *   00e6fbe0    move.w (A0),D0w               ; D0 = cursor_num
 *   00e6fbe2    move.w (A1),D4w               ; D4 = width
 *   00e6fbe4    movea.l (0x14,A6),A2
 *   00e6fbe8    movea.l (0x18,A6),A3
 *   00e6fbec    move.w (A2),D2w               ; D2 = height
 *   00e6fbee    movea.l (0x1c,A6),A4
 *   00e6fbf2    move.w (A3),D5w               ; D5 = hot_x
 *   00e6fbf4    move.w (A4),D6w               ; D6 = hot_y
 *   00e6fbf6    cmpi.w #0x3,D0w
 *   00e6fbfa    ble.b 0x00e6fc08
 *   00e6fbfc    movea.l D7,A0
 *   00e6fbfe    move.l #0x130023,(A0)         ; invalid cursor number
 *   00e6fc04    bra.w 0x00e6fd0c
 *   00e6fc08    cmpi.w #0x10,D6w              ; validate hot_y <= 16
 *   ...validation continues...
 */
void SMD_$LOAD_CRSR_BITMAP(void *param1,
                           int16_t *cursor_num,
                           int16_t *width_ptr,
                           int16_t *height_ptr,
                           int16_t *hot_x_ptr,
                           int16_t *hot_y_ptr,
                           int16_t *bitmap,
                           status_$t *status_ret)
{
    int16_t cursor_idx;
    int16_t width, height, hot_x, hot_y;
    smd_cursor_pattern_t *cursor_data;
    int8_t need_tracking_update;
    int16_t i;
    int16_t unit;
    smd_display_hw_t *hw;

    (void)param1;  /* Unused parameter */

    cursor_idx = *cursor_num;
    width = *width_ptr;
    height = *height_ptr;
    hot_x = *hot_x_ptr;
    hot_y = *hot_y_ptr;

    /* Validate cursor number (0-3) */
    if (cursor_idx > 3) {
        *status_ret = status_$display_invalid_cursor_number;
        return;
    }

    /* Validate dimensions and hotspot */
    if (hot_y > 16 || hot_y < 0 ||
        hot_x > 16 || hot_x < 0 ||
        height > 16 || height < 1 ||
        width < 1 || width > 16) {
        *status_ret = status_$display_invalid_scroll_displacement;
        return;
    }

    *status_ret = status_$ok;

    /*
     * 0x00e6fc4a "move.w (0x1d98,A5),(0x48,A5,D3w*0x1)": the unit that gets
     * bound to this ASID is SMD_GLOBALS.default_unit.  (Bead source-nuan: the
     * global the disassembly elsewhere names 0x00E84924 is this same word -
     * 0x00E82B8C + 0x1D98 - so there is nothing else it could be.)
     */
    unit = SMD_GLOBALS.default_unit;
    SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID] = (uint16_t)unit;

    /* 0x00e6fc54-0x00e6fc64 */
    hw = smd_$unit_rec(unit)->hw;

    /* 0x00e6fc6a-0x00e6fc76: the cursor pattern table is 0-based, base
     * 0x00E27366 indexed by cursor_num*4. */
    cursor_data = SMD_CURSOR_PTABLE[cursor_idx];

    /*
     * 0x00e6fc78-0x00e6fc82: "seq D3b" then "and.b (0x38,A0),D3b" - the flag
     * is the byte-wise AND of two Domain booleans, and the branch is bpl, so
     * the update only happens when the result is negative.
     */
    need_tracking_update =
        (int8_t)((cursor_idx == hw->cursor_number ? (uint8_t)0xFF : (uint8_t)0) &
                 (uint8_t)hw->cursor_visible);
    if (need_tracking_update < 0) {
        /* 0x00e6fc84-0x00e6fc8e */
        SMD_$ADD_TRK_RECT(&SMD_GLOBALS.kbd_cursor_track_rect,
                          (uint16_t *)&SMD_SYNC_LOCK_DATA, status_ret);
    }

    /* 0x00e6fc96 */
    SMD_$ACQ_DISPLAY((int16_t *)&SMD_ACQ_LOCK_DATA);

    /* 00e6fca0-00e6fcb2: width, height, hot_x, then height - hot_y - 1 */
    cursor_data->width = width;
    cursor_data->height = height;
    cursor_data->hot_x = hot_x;
    cursor_data->hot_y_adj = (int16_t)((height - 1) - hot_y);

    /* 00e6fcb6-00e6fcd0: `height` raster words from the caller's array */
    for (i = 0; i < height; i++) {
        cursor_data->bitmap[i] = (uint16_t)bitmap[i];
    }

    /* 00e6fcd4-00e6fcf2: clear bitmap[height .. 15] */
    for (i = height; i < 16; i++) {
        cursor_data->bitmap[i] = 0;
    }

    /* Release display lock */
    SMD_$REL_DISPLAY();

    /* 0x00e6fcfa-0x00e6fd08: if the cursor was visible, drop the tracking
     * rectangle again so it reappears. */
    if (need_tracking_update < 0) {
        SMD_$DEL_TRK_RECT(&SMD_GLOBALS.kbd_cursor_track_rect,
                          (uint16_t *)&SMD_SYNC_LOCK_DATA, status_ret);
    }
}
