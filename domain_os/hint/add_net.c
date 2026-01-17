/*
 * HINT_$ADD_NET - Add network port to hint file
 *
 * Stores the network port in the hint file header.
 *
 * Original address: 0x00E49C76
 */

#include "hint/hint_internal.h"

void HINT_$ADD_NET(uint32_t net_port)
{
    hint_file_t *hintfile;

    /* Early exit if hint file not mapped */
    if (HINT_$HINTFILE_PTR == NULL) {
        return;
    }

    /* Acquire exclusion lock */
    ML_$EXCLUSION_START(&HINT_$EXCLUSION_LOCK);

    hintfile = HINT_$HINTFILE_PTR;

    /* Store the network port */
    hintfile->header.net_port = net_port;

    /* Copy network info from ROUTE_$PORTP + 0x2E */
    {
        uint32_t *net_info = (uint32_t *)(ROUTE_$PORTP + 0x2E);
        hintfile->header.net_info = *net_info;
    }

    /* Release exclusion lock */
    ML_$EXCLUSION_STOP(&HINT_$EXCLUSION_LOCK);
}
