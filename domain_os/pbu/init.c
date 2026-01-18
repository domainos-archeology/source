/*
 * PBU_$INIT - Initialize PBU subsystem
 *
 * On systems without PBU hardware, this is a no-op stub.
 *
 * Reverse engineered from Domain/OS at address 0x00e5910e
 */

#include "pbu/pbu.h"

/*
 * PBU_$INIT
 *
 * This function would normally initialize the PBU hardware
 * subsystem. On systems without PBU hardware, this is simply
 * a no-op.
 */
void PBU_$INIT(void)
{
    /* No-op on systems without PBU hardware */
    return;
}
