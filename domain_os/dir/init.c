/*
 * DIR_$INIT - Initialize the directory subsystem
 *
 * Called during system boot to initialize directory services.
 * Clears global flags, initializes event counters, slot indices,
 * buffer pointers, free lists, mutexes, and calls DIR_$OLD_INIT.
 *
 * Original address: 0x00E3140C
 * Original size: 232 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$INIT - Initialize the directory subsystem
 *
 * Performs the following initialization:
 * 1. Clears global flags and bitmaps
 * 2. Loops 32 times initializing event counters and slot data
 * 3. Sets up free list heads for handle entries and request buffers
 * 4. Initializes exclusion mutexes
 * 5. Calls DIR_$OLD_INIT for legacy initialization
 *
 * The directory subsystem uses 32 slots for concurrent directory
 * operations, each with an event counter and associated data.
 */
void DIR_$INIT(void)
{
    int16_t i;

    /* Clear active slots bitmaps */
    DAT_00e7fc3c = 0;
    DAT_00e7fc34 = 0;

    /* Clear counters/flags */
    DAT_00e7f470 = 0;
    DAT_00e7fbf4 = 0;
    DAT_00e7f4b0 = 0;

    /* Initialize 32 event counters and associated slot data */
    for (i = 0; i < 32; i++) {
        EC_$INIT(&DIR_$WAIT_ECS[i]);
    }

    /* Initialize the wait-for-handle event counter */
    EC_$INIT(&DIR_$WT_FOR_HDNL_EC);

    /* Set up free list heads */
    DAT_00e7fc30 = NULL;  /* Handle entry free list */
    DAT_00e7fc38 = NULL;  /* Request buffer free list */

    /* Clear link buffer mutex owner */
    DAT_00e7fc40 = 0;

    /* Initialize exclusion mutexes */
    ML_$EXCLUSION_INIT(&DIR_$MUTEX);
    ML_$EXCLUSION_INIT(&DIR_$LINK_BUF_MUTEX);

    /* Call legacy initialization */
    DIR_$OLD_INIT();
}
