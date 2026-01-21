/*
 * smd/read_crsr_bitmap.c - SMD_$READ_CRSR_BITMAP implementation
 *
 * Reads a cursor bitmap from the cursor table.
 *
 * Original address: 0x00E6FD16
 */

#include "smd/smd_internal.h"

/*
 * SMD_$READ_CRSR_BITMAP - Read cursor bitmap
 *
 * Reads the cursor bitmap definition for the specified cursor number.
 * Returns the dimensions, hotspot, and bitmap data.
 *
 * Parameters:
 *   param1     - Unused (reserved)
 *   cursor_num - Pointer to cursor number (0-3)
 *   width_ret  - Pointer to receive width
 *   height_ret - Pointer to receive height
 *   hot_x_ret  - Pointer to receive hot spot X
 *   hot_y_ret  - Pointer to receive hot spot Y
 *   bitmap_ret - Pointer to receive bitmap data (32 bytes / 8 uint32_t)
 *   status_ret - Pointer to status return
 *
 * Original address: 0x00E6FD16
 *
 * Assembly:
 *   00e6fd16    link.w A6,-0x8
 *   00e6fd1a    movem.l {  A5 A4 A3 A2},-(SP)
 *   00e6fd1e    lea (0xe82b8c).l,A5
 *   00e6fd24    movea.l (0x24,A6),A1          ; A1 = status_ret
 *   00e6fd28    movea.l (0xc,A6),A0
 *   00e6fd2c    move.w (A0),D0w               ; D0 = cursor_num
 *   00e6fd2e    cmpi.w #0x3,D0w
 *   00e6fd32    ble.b 0x00e6fd3c
 *   00e6fd34    move.l #0x130023,(A1)         ; invalid cursor number
 *   00e6fd3a    bra.b 0x00e6fd86
 *   00e6fd3c    move.w D0w,D1w
 *   00e6fd3e    movea.l #0xe27366,A2          ; cursor ptable base
 *   00e6fd44    ext.l D1
 *   00e6fd46    lsl.l #0x2,D1                 ; cursor_num * 4
 *   00e6fd48    lea (0x0,A2,D1*0x1),A2        ; &ptable[cursor_num]
 *   00e6fd4c    movea.l (A2),A0               ; A0 = cursor data ptr
 *   00e6fd4e    movea.l (0x10,A6),A2          ; width_ret
 *   00e6fd52    move.w (A0),(A2)              ; *width_ret = cursor[0]
 *   00e6fd54    movea.l (0x14,A6),A3          ; height_ret
 *   00e6fd58    move.w (0x2,A0),(A3)          ; *height_ret = cursor[1]
 *   00e6fd5c    movea.l (0x18,A6),A4          ; hot_x_ret
 *   00e6fd60    move.w (0x4,A0),(A4)          ; *hot_x_ret = cursor[2]
 *   00e6fd64    move.w (0x2,A0),D1w           ; height
 *   00e6fd68    movea.l (0x1c,A6),A2          ; hot_y_ret
 *   00e6fd6c    subq.w #0x1,D1w               ; height - 1
 *   00e6fd6e    sub.w (0x6,A0),D1w            ; - hot_y_offset
 *   00e6fd72    move.w D1w,(A2)               ; *hot_y_ret
 *   00e6fd74    lea (0x8,A0),A3               ; &cursor[4] (bitmap data)
 *   00e6fd78    movea.l (0x20,A6),A4          ; bitmap_ret
 *   00e6fd7c    moveq #0x7,D1                 ; loop 8 times (32 bytes)
 *   00e6fd7e    move.l (A3)+,(A4)+            ; copy 4 bytes
 *   00e6fd80    dbf D1w,0x00e6fd7e
 *   00e6fd84    clr.l (A1)                    ; *status_ret = 0
 *   00e6fd86    movem.l (-0x18,A6),{  A2 A3 A4 A5}
 *   00e6fd8c    unlk A6
 *   00e6fd8e    rts
 */
void SMD_$READ_CRSR_BITMAP(void *param1,
                           int16_t *cursor_num,
                           uint16_t *width_ret,
                           uint16_t *height_ret,
                           uint16_t *hot_x_ret,
                           int16_t *hot_y_ret,
                           uint32_t *bitmap_ret,
                           status_$t *status_ret)
{
    int16_t cursor_idx;
    int16_t *cursor_data;
    int16_t i;
    uint32_t *src;

    (void)param1;  /* Unused parameter */

    cursor_idx = *cursor_num;

    /* Validate cursor number (0-3) */
    if (cursor_idx > 3) {
        *status_ret = status_$display_invalid_cursor_number;
        return;
    }

    /* Get cursor data pointer from cursor table */
    cursor_data = SMD_CURSOR_PTABLE[cursor_idx];

    /* Return dimensions and hotspot */
    *width_ret = cursor_data[0];
    *height_ret = cursor_data[1];
    *hot_x_ret = cursor_data[2];
    /* Hot Y is computed as (height-1) - stored_offset */
    *hot_y_ret = (cursor_data[1] - 1) - cursor_data[3];

    /* Copy bitmap data (8 uint32_t = 32 bytes = 16 int16_t of bitmap) */
    src = (uint32_t *)&cursor_data[4];
    for (i = 0; i < 8; i++) {
        bitmap_ret[i] = src[i];
    }

    *status_ret = status_$ok;
}
