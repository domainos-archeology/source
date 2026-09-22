/*
 * KBD_$SET_KBD_MODE - Set a line's keyboard mode
 *
 * Only the status is written: the line and mode arguments are never read
 * (the 12-byte frame is allocated and left untouched).
 *
 * Parameters (frame 0x00E7251A):
 *   0x08 line_ptr - unused
 *   0x0C mode     - unused
 *   0x10 status   - cleared
 *
 * Original address: 0x00e72516, 14 bytes
 *
 *   00e72516  link.w A6,-0xc
 *   00e7251a  movea.l (0x10,A6),A0
 *   00e7251e  clr.l (A0)
 *   00e72520  unlk A6 / rts
 */

#include "kbd/kbd_internal.h"

void KBD_$SET_KBD_MODE(short *line_ptr, unsigned char *mode, status_$t *status)
{
    (void)line_ptr;
    (void)mode;

    /* 0x00E7251A..0x00E7251E */
    *status = status_$ok;
}
