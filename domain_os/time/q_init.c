/*
 * TIME_$Q_INIT - Initialize the time-queue module
 *
 * A single `rts` (0x00E16C5C, 2 bytes): the module has nothing to set up.
 * Called by TIME_$INIT at 0x00E2FE7A.
 *
 * Original address: 0x00e16c5c
 */

#include "time/time_internal.h"

void TIME_$Q_INIT(void)
{
}
