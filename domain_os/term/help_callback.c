/*
 * TERM_$HELP_CALLBACK - Help-key callback placeholder
 *
 * An empty procedure: the image holds a single `rts` (map: 0x00E7244E, the
 * word before TERM_$PCHIST_ENABLE at 0x00E72450).
 *
 * Original address: 0x00e7244e, 2 bytes
 *
 *   00e7244e  rts
 */

#include "term/term_internal.h"

void TERM_$HELP_CALLBACK(void)
{
    /* 0x00E7244E: rts */
}
