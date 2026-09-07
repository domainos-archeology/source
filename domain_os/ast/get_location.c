/*
 * AST_$GET_LOCATION - Get location info for an object by UID
 *
 * Retrieves the volume UID and object info for a given UID.
 * If the UID is NIL, returns an error. Otherwise looks up the AOTE
 * and returns the stored location information.
 *
 * Parameters:
 *   loc_rec - The 0x20-byte object-location record (UID at offset 8,
 *             flags byte at offset 0x1D); overwritten on success
 *   flags - Lookup flags (bit 0: force load if not cached)
 *   unused - 4-byte cell the routine never reads or writes
 *   location_out - Output: the object's location word (aote+0x08; see
 *             aote_t.location in ast/ast.h for the encoding).  `move.l
 *             (0x8,A0),(A1)` at 0x00E04766.
 *   status - Status return
 *
 * Original address: 0x00e046c8
 */

#include "ast/ast_internal.h"
#include "route/route.h"

void AST_$GET_LOCATION(file_$obj_loc_t *loc_rec, uint16_t flags,
                       uint32_t *unused, uint32_t *location_out,
                       status_$t *status)
{
    /* 0x00e04770 copies the record as 8 longwords, so keep a longword view. */
    uint32_t *uid_info = (uint32_t *)(void *)loc_rec;
    uid_t *uid = &loc_rec->uid;                 /* +0x08 (lea (0x8,A2),A0) */
    aote_t *aote;
    int i;

    /* Check for NIL UID */
    if (uid->high == UID_$NIL.high && uid->low == UID_$NIL.low) {
        *status = ast_$validate_uid(uid, 0x30F01);
        return;
    }

    ML_$LOCK(AST_LOCK_ID);

    /* Look up AOTE by UID */
    aote = ast_$lookup_aote_by_uid(uid);

    if (aote == NULL) {
        /* AOTE not cached - try to load it */
        aote = ast_$force_activate_segment(uid, 0, status, -((flags & 1) != 0));
        if (aote == NULL) {
            ML_$UNLOCK(AST_LOCK_ID);
            return;
        }
    } else {
        /* Mark AOTE as busy */
        aote->flags |= AOTE_FLAG_BUSY;
    }

    /* 0x00E04766: return the object's location word. */
    *location_out = aote->location;

    /* Copy object UID info (8 uint32_t = 32 bytes) */
    uint32_t *src = (uint32_t *)((char *)aote + 0x9C);
    uint32_t *dst = uid_info;
    for (i = 0; i < 8; i++) {
        dst[i] = src[i];
    }

    ML_$UNLOCK(AST_LOCK_ID);

    /* If route port not set, use default */
    if (uid_info[4] == 0) {
        uid_info[4] = ROUTE_$PORT;
    }

    *status = status_$ok;
}
