/*
 * VTOC_$SEARCH_VOLUMES - Search volumes for an object
 *
 * Searches volumes 1-5 for an object via VTOC_$LOOKUP.
 * Used during force-activation path for root objects.
 *
 * Parameters:
 *   uid_info - Pointer to UID info structure (vtoc_$lookup_req_t format)
 *   status   - Output status code (file_$object_not_found if not found)
 *
 * Original address: 0x00E01BEE
 * Original size: 100 bytes
 */

#include "vtoc/vtoc_internal.h"
#include "ast/ast.h"
#include "network/network.h"

/* External reference - network diskless flag */
extern int8_t NETWORK_$REALLY_DISKLESS;

/* Status codes */
#define file_$object_not_found 0x000F0001

void VTOC_$SEARCH_VOLUMES(void *uid_info, status_$t *status)
{
    vtoc_$lookup_req_t *req = (vtoc_$lookup_req_t *)uid_info;
    uint16_t vol_idx;
    uint16_t vol_flags;

    /* If really diskless, skip local search */
    if (NETWORK_$REALLY_DISKLESS >= 0) {
        /* Search volumes 1-5 */
        for (vol_idx = 1; vol_idx <= 5; vol_idx++) {
            /*
             * Check if volume index is valid (0-15) and not flagged
             * vol_flags at A5+0x420 contains bitmask of unavailable volumes
             */
            if (vol_idx > 0x0F) {
                /* Volume index out of range, try anyway */
            } else {
                vol_flags = DAT_00e1e0a0;  /* Volume info flags */
                if ((vol_flags & (1 << vol_idx)) != 0) {
                    /* Volume unavailable, skip */
                    continue;
                }
            }

            /* Set volume index in request and try lookup */
            req->vol_idx = (uint8_t)vol_idx;
            VTOC_$LOOKUP(req, status);

            if (*status == status_$ok) {
                return;  /* Found it */
            }

            /* If serious error (high bit set), validate UID and continue */
            if ((int16_t)*status < 0) {
                ast_$validate_uid((uid_t *)((char *)uid_info + 8), *status);
            }
        }
    }

    /* Not found on any volume */
    *status = file_$object_not_found;
}
