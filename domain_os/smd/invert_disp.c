/*
 * smd/invert_disp.c - SMD_$INVERT_DISP implementation
 *
 * Inverts a region of display memory by XORing with all 1s.
 * This is a low-level function that directly manipulates display memory.
 *
 * Original address: 0x00E70376
 */

#include "smd/smd_internal.h"

/* Offset to framebuffer within display memory region */
#define SMD_FRAMEBUFFER_OFFSET  0x19000

/* Number of longwords to invert (0x1C00 = 7168 longs = 28672 bytes) */
#define SMD_INVERT_COUNT        0x1C00

/*
 * SMD_$INVERT_DISP - Invert display region
 *
 * Inverts a fixed region of display memory starting at the specified
 * base address plus 0x19000 offset. This XORs 0x1C00 longwords (28KB)
 * which corresponds to a typical Apollo monochrome display buffer.
 *
 * Parameters:
 *   display_base - Base address of display memory
 *   display_info - Pointer to display info (unused in this implementation)
 *
 * Note: This function does not use the display_info parameter in the
 * original implementation. The parameter is present for calling
 * convention compatibility with SMD_$INVERT_S.
 *
 * Original address: 0x00E70376
 *
 * Assembly:
 *   00e70376    movea.l (0x4,SP),A0        ; A0 = display_base
 *   00e7037a    movea.l (0x8,SP),A1        ; A1 = display_info (unused)
 *   00e7037e    adda.l #0x19000,A0         ; A0 += framebuffer offset
 *   00e70384    move.w #0x1bff,D0w         ; D0 = count - 1
 *   00e70388    not.l (A0)+                ; invert longword, advance
 *   00e7038a    dbf D0w,0x00e70388         ; loop until count exhausted
 *   00e7038e    rts
 */
void SMD_$INVERT_DISP(uint32_t display_base, smd_display_info_t *display_info)
{
    volatile uint32_t *framebuffer;
    uint16_t count;

    (void)display_info; /* Unused in original */

    /* Calculate framebuffer address */
    framebuffer = (volatile uint32_t *)(display_base + SMD_FRAMEBUFFER_OFFSET);

    /*
     * Invert 0x1C00 longwords.
     * The original uses DBF which decrements and branches until -1,
     * so starting count of 0x1BFF gives 0x1C00 iterations.
     */
    for (count = SMD_INVERT_COUNT; count != 0; count--) {
        *framebuffer = ~(*framebuffer);
        framebuffer++;
    }
}
