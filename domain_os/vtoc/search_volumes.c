/*
 * VTOC_$SEARCH_VOLUMES - Search volumes for an object
 *
 * Tries VTOC_$LOOKUP on volume indices 1..6 in turn.
 * Used during force-activation path for root objects.
 *
 * Parameters:
 *   uid_info - Pointer to UID info structure (vtoc_$lookup_req_t format)
 *   status   - Output status code (file_$object_not_found if not found)
 *
 * Original address: 0x00E01BEE
 * Original size: 100 bytes
 *
 * The routine does not load A5 of its own: it inherits the AST_ module base
 * (0xE1DC80) from its caller, so the "(0x420,A5)" word it reads is
 * ast_$vol_info_count at 0xE1E0A0.
 */

#include "vtoc/vtoc_internal.h"
#include "ast/ast.h"
#include "file/file.h"      /* file_$object_not_found (0x000F0001) */
#include "network/network.h"

void VTOC_$SEARCH_VOLUMES(void *uid_info, status_$t *status)
{
    vtoc_$lookup_req_t *req = (vtoc_$lookup_req_t *)uid_info;
    uint16_t vol_idx;
    uint16_t vol_flags;
    int16_t remaining;

    /* 0x00E01BFE tst.b / bmi: a really diskless node searches nothing */
    if (NETWORK_$REALLY_DISKLESS >= 0) {
        /*
         * 0x00E01C06-0x00E01C3E: "moveq #0x5,D2" with a dbf makes SIX
         * passes, and the index in D3 starts at 1, so the volumes tried are
         * 1..6 - the same six the BAT and VTOC volume tables hold.
         */
        remaining = 5;
        vol_idx = 1;
        do {
            /*
             * 0x00E01C0A-0x00E01C16:
             *   moveq #0xf,D0 / cmp.w D3w,D0w / bcs   -> index > 15, skip the
             *                                            mask test entirely
             *   move.w (0x420,A5),D1w / btst.l D3,D1  -> dismount in progress
             * A set bit means the volume is being dismounted, and the pass is
             * abandoned.
             */
            if (vol_idx <= 0x0F) {
                vol_flags = ast_$vol_info_count;
                if ((vol_flags & (uint16_t)(1u << vol_idx)) != 0) {
                    /* volume unavailable, skip it */
                    goto next_volume;
                }
            }

            /* 0x00E01C18: move.b D3b,(0x1c,A2) - the low byte of the index */
            req->vol_idx = (uint8_t)vol_idx;

            /* 0x00E01C1C-0x00E01C26 */
            VTOC_$LOOKUP(req, status);

            /* 0x00E01C28 tst.l (A3) / beq: found it, return as is */
            if (*status == status_$ok) {
                return;
            }

            /*
             * 0x00E01C2C-0x00E01C3A: "tst.w (A3) / bpl".  The word test lands
             * on the HIGH half of the status longword, so the guard is bit 31
             * of the status, not its low word.  Only then is the UID handed
             * to ast_$validate_uid, with the status passed BY VALUE
             * ("move.l (A3),-(SP)") and the UID by reference
             * ("pea (0x8,A2)").
             */
            if ((int16_t)((uint32_t)*status >> 16) < 0) {
                ast_$validate_uid(&req->uid, (uint32_t)*status);
            }

next_volume:
            /* 0x00E01C3C addq.w #0x1,D3w / 0x00E01C3E dbf D2w */
            vol_idx++;
            remaining--;
        } while (remaining != -1);
    }

    /* 0x00E01C42: not found on any volume */
    *status = file_$object_not_found;
}
