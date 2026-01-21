/*
 * smd/putc.c - Put character to display (stub)
 *
 * This function is a stub in the analyzed binary - it contains only
 * an RTS (return from subroutine) instruction. Character output to
 * the display is handled through SMD_$WRITE_STRING instead.
 *
 * Original address: 0x00E1D89C
 */

#include "smd/smd_internal.h"

/*
 * SMD_$PUTC - Put character (stub)
 *
 * This is a no-op stub function. It was likely intended to output
 * a single character to the display but was never implemented or
 * was deprecated in favor of SMD_$WRITE_STRING.
 *
 * Parameters:
 *   c - Character to output (ignored)
 *
 * Notes:
 *   - Original is just a single RTS instruction
 *   - No callers found in the analyzed binary
 */
void SMD_$PUTC(uint16_t c)
{
    /* Stub - no implementation */
    (void)c;  /* Suppress unused parameter warning */
}
