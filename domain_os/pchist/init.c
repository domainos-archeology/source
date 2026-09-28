/*
 * PCHIST_$INIT - Initialise the PC-histogram module's exclusion lock
 *
 * Re-emitted from the image (0x00E32394..0x00E323A6, 20 bytes) and
 * verified; the previous body was faithful.
 *
 *   00e32398  move.l #0xe2c204,-(SP)     ; &PCHIST_$CONTROL (the lock is at +0)
 *   00e3239e  jsr ML_$EXCLUSION_INIT     ; the 4 bytes are left for unlk
 *
 * Sole caller OS_$INIT 0x00E34732.
 *
 * Original address: 0x00e32394
 */

#include "pchist/pchist_internal.h"

void PCHIST_$INIT(void)
{
    ML_$EXCLUSION_INIT(&PCHIST_$CONTROL.lock);
}
