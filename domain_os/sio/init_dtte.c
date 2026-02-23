/*
 * SIO_$INIT_DTTE - Initialize a DTTE (Display Terminal Table Entry)
 *
 * Initializes three inline event counts within the DTTE structure
 * at offsets 0x00, 0x0C, and 0x18, then sets the discipline field
 * and clears the flags byte.
 *
 * The DTTE contains three ec_$eventcount_t structures (12 bytes each):
 *   - Offset 0x00: General event count (initialized last for ordering)
 *   - Offset 0x0C: Input event count (input_ec)
 *   - Offset 0x18: Output event count (output_ec)
 *
 * Parameters:
 *   dtte       - Pointer to DTTE entry to initialize
 *   discipline - Terminal discipline value (0=TTY, 2=console, etc.)
 *
 * Assembly register mapping:
 *   dtte       = A2 (from A6+0x08)
 *   discipline = D2w (from A6+0x0C, word)
 *
 * Original address: 0x00e32b76
 * Size: 66 bytes
 */

#include "sio/sio_internal.h"

void SIO_$INIT_DTTE(dtte_t *dtte, int16_t discipline)
{
    /*
     * Initialize the three inline event counts.
     * Order matches the assembly: 0x0C, 0x18, 0x00.
     * EC_$INIT sets value=0 and initializes empty waiter list.
     */
    EC_$INIT((ec_$eventcount_t *)((char *)dtte + 0x0C));
    EC_$INIT((ec_$eventcount_t *)((char *)dtte + 0x18));
    EC_$INIT((ec_$eventcount_t *)((char *)dtte));

    /* Clear terminal flags (offset 0x36) */
    dtte->flags = 0;

    /* Set terminal discipline (offset 0x34) */
    dtte->discipline = discipline;
}
