/*
 * smd/lites.c - SMD_$LITES implementation
 *
 * Display status "lights" on the display.
 * This is a thin wrapper around the low-level DISP_LITES function.
 *
 * Original address: 0x00E1D8B8
 *
 * Assembly:
 *   00e1d8b8    link.w A6,0x0
 *   00e1d8bc    pea (A5)
 *   00e1d8be    lea (0xe2e060).l,A5      ; Load SMD base
 *   00e1d8c4    move.w (0xa,A6),-(SP)    ; Push param2 (y position)
 *   00e1d8c8    move.w (0x8,A6),-(SP)    ; Push param1 (pattern)
 *   00e1d8cc    jsr 0x00e1e9f4.l         ; Call DISP_LITES
 *   00e1d8d2    movea.l (-0x4,A6),A5
 *   00e1d8d6    unlk A6
 *   00e1d8d8    rts
 *
 * DISP_LITES draws a row of 16 status indicator blocks on the display.
 * Each bit in the pattern controls whether the corresponding block is
 * filled (bit set) or hollow (bit clear).
 */

#include "smd/smd_internal.h"

/* Forward declaration of low-level display function */
extern void DISP_LITES(uint16_t pattern, uint16_t y_pos);

/*
 * SMD_$LITES - Display status lights
 *
 * Draws 16 small status indicator blocks on the display at the specified
 * Y position. Each bit in the pattern corresponds to one block:
 *   - Bit 15 (MSB) = leftmost block
 *   - Bit 0 (LSB) = rightmost block
 *   - Bit set = filled/solid block
 *   - Bit clear = hollow/outlined block
 *
 * The blocks are 16 pixels high, arranged in a 4x4 grid, and appear
 * at fixed X positions based on the display type.
 *
 * Parameters:
 *   pattern - 16-bit pattern controlling which lights are lit
 *   y_pos   - Y coordinate (row offset, multiplied by 0x80 internally)
 *
 * Note: This function writes directly to display memory at 0x00FC0000.
 * It disables interrupts during the write operation.
 */
void SMD_$LITES(uint16_t pattern, uint16_t y_pos)
{
    DISP_LITES(pattern, y_pos);
}
