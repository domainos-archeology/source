/*
 * KBD_$SET_KBD_MODE - Set keyboard mode
 *
 * Stub function - always returns success.
 *
 * Parameters:
 *   line_ptr - Pointer to terminal line number
 *   mode     - Pointer to keyboard mode value
 *   status   - Pointer to receive status code
 *
 * Original address: 0x00e72516
 */

#include "kbd/kbd_internal.h"

void KBD_$SET_KBD_MODE(short *line_ptr, unsigned char *mode, status_$t *status)
{
    (void)line_ptr;
    (void)mode;
    *status = status_$ok;
}
