/*
 * DISK_$INIT - Disk subsystem initialization
 *
 * Initializes the disk subsystem by:
 * 1. Initializing the main disk event counter
 * 2. Initializing per-volume event counters (64 volumes)
 * 3. Initializing exclusion locks for synchronization
 */

#include "disk.h"

/* Base data area at 0xe7a1cc */
extern uint8_t DISK__DATA[];

/* Event counters at specific offsets */
/* Main EC at base */
/* Per-volume ECs at +0x1c intervals, starting at offset 0x1c */
/* Each volume has ECs at +0x378 and +0x384 from its base */

/* Exclusion locks */
extern void *ml__exclusion_t_00e7a274;  /* +0xa8 from base */
extern void *ml__exclusion_t_00e7a25c;  /* +0x90 from base */

/*
 * DISK_$INIT - Initialize the disk subsystem
 */
void DISK_$INIT(void)
{
    int16_t i;
    uint8_t *vol_ptr;

    /* Initialize main disk event counter at base */
    EC__INIT((void *)DISK__DATA);

    /* Initialize per-volume event counters
     * There are 64 volumes (0x40), each with two event counters
     * at offsets +0x378 and +0x384 from the volume entry base.
     * Volume entries start at offset 0x1c and are spaced 0x1c apart.
     */
    vol_ptr = DISK__DATA + 0x1c;  /* First volume entry */

    for (i = 0x3f; i >= 0; i--) {
        /* Initialize event counter at +0x378 */
        EC__INIT((void *)(vol_ptr + 0x378));

        /* Initialize event counter at +0x384 */
        EC__INIT((void *)(vol_ptr + 0x384));

        vol_ptr += 0x1c;  /* Next volume entry */
    }

    /* Initialize exclusion locks */
    ML_$EXCLUSION_INIT(&ml__exclusion_t_00e7a274);
    ML_$EXCLUSION_INIT(&ml__exclusion_t_00e7a25c);
}
